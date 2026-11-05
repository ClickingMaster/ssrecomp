#version 460
// ssrecomp, GPU3D_WideGpu.h: the picture in the form GPU3D_Wide.cpp's own rasteriser leaves
// it (XRGB, the top byte the alpha: 0 where nothing was drawn), with the fog.

layout (std140, binding = 0) uniform Frame
{
    uvec4 uSize;
    vec4 uScale;
    uvec4 uFog;           // fog colour, offset, shift, flags (bit 0 fog on, bit 1 fog colours)
    uvec4 uToon[8];
    uvec4 uFogDensity[9];
    uvec4 uEdge[2];       // the edge marking colours, four to an entry
    uvec4 uClear;         // the rear plane's polygon ID and depth, edge marking on
};

layout (binding = 0) uniform sampler2D uColor;
layout (binding = 1) uniform usampler2D uAttr;

layout (location = 0) out vec4 oColor;

uint Density(uint i)
{
    uvec4 e = uFogDensity[i >> 2];
    uint j = i & 3u;
    return j == 0u ? e.x : j == 1u ? e.y : j == 2u ? e.z : e.w;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 c = texelFetch(uColor, p, 0);
    uint stored = uint(c.a * 255.0 + 0.5);
    if (stored == 0u)
    {
        // A pixel left out between two polygons that meet (the graphics chip's rules for which
        // pixels an edge covers are not the DS's: dotted black lines along the seams of
        // SoulSilver's ground): the pixel beside it, when there is something drawn on both sides
        // of it, across or up and down. (Nothing drawn on one side is the end of the world.)
        ivec2 last = ivec2(uSize.xy) - 1;
        ivec2 l = clamp(p - ivec2(1, 0), ivec2(0), last), r = clamp(p + ivec2(1, 0), ivec2(0), last);
        ivec2 u = clamp(p - ivec2(0, 1), ivec2(0), last), d = clamp(p + ivec2(0, 1), ivec2(0), last);
        vec4 cl = texelFetch(uColor, l, 0), cr = texelFetch(uColor, r, 0);
        vec4 cu = texelFetch(uColor, u, 0), cd = texelFetch(uColor, d, 0);
        if (l != p && r != p && cl.a > 0.0 && cr.a > 0.0)
        {
            p = l;
            c = cl;
        }
        else if (u != p && d != p && cu.a > 0.0 && cd.a > 0.0)
        {
            p = u;
            c = cu;
        }
        else
        {
            oColor = vec4(0.0);
            return;
        }
        stored = uint(c.a * 255.0 + 0.5);
    }
    uvec3 rgb = uvec3(c.rgb * 255.0 + 0.5);
    // (alpha + 1) / 32 back to alpha, and to 8 bits.
    uint a = uint(float(stored) * 32.0 / 255.0 + 0.5) - 1u;
    uint a8 = (a << 3) | (a >> 2);

    uvec2 attr = texelFetch(uAttr, p, 0).xy;
    if (uClear.z != 0u && (attr.x & (3u << 30)) == (3u << 30))
    {
        // Edge marking, as the software renderer does it: an opaque polygon's pixel beside one of
        // another polygon ID that is further away (the picture's border is the rear plane).
        uint z = attr.x & 0xFFFFFFu, id = (attr.x >> 24) & 0x3Fu;
        ivec2 last = ivec2(uSize.xy) - 1;
        bool edge = false, inside = false;
        vec4 under = vec4(0.0);
        for (int i = 0; i < 4 && !edge; i++)
        {
            ivec2 q = p + (i == 0 ? ivec2(-1, 0) : i == 1 ? ivec2(1, 0) : i == 2 ? ivec2(0, -1) : ivec2(0, 1));
            uint nz = uClear.y, nid = uClear.x;
            if (q.x >= 0 && q.y >= 0 && q.x <= last.x && q.y <= last.y)
            {
                uint n = texelFetch(uAttr, q, 0).x;
                nz = n & 0xFFFFFFu;
                if ((n & (1u << 30)) != 0u) nid = (n >> 24) & 0x3Fu;
                under = texelFetch(uColor, q, 0);
                inside = true;
            }
            else
                inside = false;
            edge = nid != id && z < nz;
        }
        if (edge)
        {
            uint k = id >> 3;
            uvec4 e4 = uEdge[k >> 2];
            uint e = (k & 3u) == 0u ? e4.x : (k & 3u) == 1u ? e4.y : (k & 3u) == 2u ? e4.z : e4.w;
            uvec3 e6 = uvec3((e << 1) & 0x3Eu, (e >> 4) & 0x3Eu, (e >> 9) & 0x3Eu);
            e6 += uvec3(notEqual(e6, uvec3(0u)));
            rgb = (e6 << 2) | (e6 >> 4);
            if ((uSize.z & (1u << 4)) != 0u)
            {
                // With anti-aliasing the software renderer gives an edge pixel a coverage of 17/32,
                // the rest from the pixel beneath it: here the neighbour that made it an edge (the
                // picture's border has nothing, which leaves it that much less opaque).
                if (inside && under.a > 0.0)
                    rgb = (rgb * 17u + uvec3(under.rgb * 255.0 + 0.5) * 15u) >> 5;
                uint ua = inside && under.a > 0.0 ? uint(under.a * 32.0 + 0.5) - 1u : 0u;
                a = (a * 17u + ua * 15u) >> 5;
                a8 = (a << 3) | (a >> 2);
            }
        }
    }
    if ((uFog.w & 3u) == 3u && attr.y != 0u)
    {
        uint z = attr.x & 0xFFFFFFu, id, frac;
        if (z < uFog.y)
        {
            id = 0u;
            frac = 0u;
        }
        else
        {
            z -= uFog.y;
            z = (z >> 2) << uFog.z;
            id = z >> 17;
            if (id >= 32u)
            {
                id = 32u;
                frac = 0u;
            }
            else
                frac = z & 0x1FFFFu;
        }
        uint density = (Density(id) * (0x20000u - frac) + Density(id + 1u) * frac) >> 17;
        if (density >= 127u) density = 128u;
        if (density != 0u)
        {
            uint fc = uFog.x;
            uvec3 fog = uvec3((fc << 3) & 0xF8u, (fc >> 2) & 0xF8u, (fc >> 7) & 0xF8u);
            rgb = (fog * density + rgb * (128u - density)) >> 7;
        }
    }
    oColor = vec4(vec3(rgb) / 255.0, float(a8) / 255.0);
}
