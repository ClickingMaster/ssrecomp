#version 460
// ssrecomp: the widescreen view's polygons on the graphics chip (GPU3D_WideGpu.h). A pixel
// is worked out as GPU3D_Wide.cpp's own rasteriser does it: the texel decoded from the DS's
// texture memory, the alpha test, toon and highlight shading, decal and modulate. What
// happens to the picture (depth test, polygon IDs of translucent pixels, blending) is
// pipeline state set per draw.
//
// Out: colour (8 bits a channel; alpha holds (alpha + 1) / 32, which blends as the DS does
// and whose maximum gives the picture's alpha) and, in a second target, the depth and the
// fog flag, for the fog in the final pass.

struct WidePolygon
{
    uint attr, texParam, texPalette, flags; // flags: bit 0 W-buffer, bit 1 textured
};

#ifdef WIDE_EARLY
// A variant for draws whose pixels are never discarded (WideGpuDraw::Early): depth and stencil
// tested before shading.
layout (early_fragment_tests) in;
#endif

#ifdef WIDE_TEXBUF
// A variant for the Switch (deko3D): the texture and palette memory read as buffer textures,
// through the texture cache, rather than as storage buffers, which the Switch's graphics chip
// reads without its first-level cache; and the polygon's attributes from the vertices.
layout (binding = 0) uniform usamplerBuffer uTex8;  // the texture memory, a byte a texel
layout (binding = 1) uniform usamplerBuffer uTex16; // the same, 16 bits a texel
layout (binding = 2) uniform usamplerBuffer uPal16; // the palette memory, 16 bits a texel
// Textures decoded beforehand (GPU3D_WideGpu.h, WideGpuAtlas): colour (15 bits) | alpha << 16
// a texel, a polygon's texture from the texel its texPalette names (flags bit 2).
layout (binding = 3) uniform usamplerBuffer uAtlas;
#else
layout (std430, binding = 1) readonly buffer Polygons
{
    WidePolygon polys[];
};
layout (std430, binding = 2) readonly buffer TexMem
{
    uint texMem[]; // 512 KB
};
layout (std430, binding = 3) readonly buffer PalMem
{
    uint palMem[]; // 128 KB
};
#endif

layout (std140, binding = 0) uniform Frame
{
    uvec4 uSize;
    vec4 uScale;
    uvec4 uFog;
    uvec4 uToon[8];
    uvec4 uFogDensity[9];
};

layout (std140, binding = 1) uniform Draw
{
    uvec4 uDraw;
};

flat in uint vPoly;
flat in uvec4 vPolyData;
in vec3 vColor;
in vec2 vTex;
noperspective in float vZLinear;
in float vZPersp;

layout (location = 0) out vec4 oColor;
layout (location = 1) out uvec2 oAttr;

#ifdef WIDE_TEXBUF
uint Tex8(uint addr)
{
    return texelFetch(uTex8, int(addr & 0x7FFFFu)).r;
}

uint Tex16(uint addr)
{
    return texelFetch(uTex16, int((addr & 0x7FFFEu) >> 1)).r;
}

uint Pal16(uint addr)
{
    return texelFetch(uPal16, int((addr & 0x1FFFEu) >> 1)).r;
}
#else
uint Tex8(uint addr)
{
    addr &= 0x7FFFFu;
    return (texMem[addr >> 2] >> ((addr & 3u) * 8u)) & 0xFFu;
}

uint Tex16(uint addr)
{
    addr &= 0x7FFFEu;
    return (texMem[addr >> 2] >> ((addr & 2u) * 8u)) & 0xFFFFu;
}

uint Pal16(uint addr)
{
    addr &= 0x1FFFEu;
    return (palMem[addr >> 2] >> ((addr & 2u) * 8u)) & 0xFFFFu;
}
#endif

int Wrap(int t, int size, bool repeat, bool mirror)
{
    if (!repeat) return clamp(t, 0, size - 1);
    int m = t & (size - 1);
    if (!mirror) return m;
    return (t & size) != 0 ? (size - 1) - m : m;
}

// SoftRenderer::TextureLookup: colour (15 bits) and alpha (0-31) of texel (s, t).
void Lookup(uint texParam, uint texPal, int s, int t, out uint color, out uint alpha)
{
    uint vramaddr = (texParam & 0xFFFFu) << 3;
    int width = 8 << ((texParam >> 20) & 7u);
    uint alpha0 = (texParam & (1u << 29)) != 0u ? 0u : 31u;
    uint pos = uint(t * width + s);
    color = 0u;
    alpha = 31u;
    switch ((texParam >> 26) & 7u)
    {
    case 1u: // A3I5
    {
        uint pixel = Tex8(vramaddr + pos);
        color = Pal16((texPal << 4) + ((pixel & 0x1Fu) << 1));
        alpha = ((pixel >> 3) & 0x1Cu) + (pixel >> 6);
        break;
    }
    case 2u: // 4 colours
    {
        uint pixel = (Tex8(vramaddr + (pos >> 2)) >> ((uint(s) & 3u) << 1)) & 3u;
        color = Pal16((texPal << 3) + (pixel << 1));
        alpha = pixel == 0u ? alpha0 : 31u;
        break;
    }
    case 3u: // 16 colours
    {
        uint pixel = Tex8(vramaddr + (pos >> 1));
        pixel = (uint(s) & 1u) != 0u ? pixel >> 4 : pixel & 0xFu;
        color = Pal16((texPal << 4) + (pixel << 1));
        alpha = pixel == 0u ? alpha0 : 31u;
        break;
    }
    case 4u: // 256 colours
    {
        uint pixel = Tex8(vramaddr + pos);
        color = Pal16((texPal << 4) + (pixel << 1));
        alpha = pixel == 0u ? alpha0 : 31u;
        break;
    }
    case 5u: // compressed
    {
        uint addr = vramaddr + uint(t & 0x3FC) * uint(width >> 2) + uint(s & 0x3FC) + uint(t & 3);
        addr &= 0x7FFFFu;
        uint slot1addr = 0x20000u + ((addr & 0x1FFFCu) >> 1);
        if (addr >= 0x40000u) slot1addr += 0x10000u;
        uint val = (addr >= 0x20000u && addr < 0x40000u) ? 0u : (Tex8(addr) >> (2u * (uint(s) & 3u)));
        uint palinfo = Tex16(slot1addr);
        uint paloffset = (palinfo & 0x3FFFu) << 2;
        uint base = texPal << 4;
        uint mode = palinfo >> 14;
        uint c0 = Pal16(base + paloffset), c1 = Pal16(base + paloffset + 2u);
        switch (val & 3u)
        {
        case 0u:
            color = c0;
            break;
        case 1u:
            color = c1;
            break;
        case 2u:
            if (mode == 1u)
                color = ((c0 & 0x1Fu) + (c1 & 0x1Fu)) >> 1 | ((((c0 & 0x3E0u) + (c1 & 0x3E0u)) >> 1) & 0x3E0u) |
                    ((((c0 & 0x7C00u) + (c1 & 0x7C00u)) >> 1) & 0x7C00u);
            else if (mode == 3u)
                color = ((c0 & 0x1Fu) * 5u + (c1 & 0x1Fu) * 3u) >> 3 | ((((c0 & 0x3E0u) * 5u + (c1 & 0x3E0u) * 3u) >> 3) & 0x3E0u) |
                    ((((c0 & 0x7C00u) * 5u + (c1 & 0x7C00u) * 3u) >> 3) & 0x7C00u);
            else
                color = Pal16(base + paloffset + 4u);
            break;
        default:
            if (mode == 2u)
                color = Pal16(base + paloffset + 6u);
            else if (mode == 3u)
                color = ((c0 & 0x1Fu) * 3u + (c1 & 0x1Fu) * 5u) >> 3 | ((((c0 & 0x3E0u) * 3u + (c1 & 0x3E0u) * 5u) >> 3) & 0x3E0u) |
                    ((((c0 & 0x7C00u) * 3u + (c1 & 0x7C00u) * 5u) >> 3) & 0x7C00u);
            else
            {
                color = 0u;
                alpha = 0u;
            }
            break;
        }
        break;
    }
    case 6u: // A5I3
    {
        uint pixel = Tex8(vramaddr + pos);
        color = Pal16((texPal << 4) + ((pixel & 7u) << 1));
        alpha = pixel >> 3;
        break;
    }
    case 7u: // direct colour
    {
        color = Tex16(vramaddr + (pos << 1));
        alpha = (color & 0x8000u) != 0u ? 31u : 0u;
        break;
    }
    default:
        break;
    }
}

// 15-bit colour to 6 bits a channel, as the DS widens it.
uvec3 Widen(uint c)
{
    uvec3 v = uvec3((c << 1) & 0x3Eu, (c >> 4) & 0x3Eu, (c >> 9) & 0x3Eu);
    return v + uvec3(notEqual(v, uvec3(0u)));
}

uint ToonEntry(uint i)
{
    uvec4 e = uToon[i >> 2];
    uint j = i & 3u;
    return j == 0u ? e.x : j == 1u ? e.y : j == 2u ? e.z : e.w;
}

void main()
{
#ifdef WIDE_TEXBUF
    WidePolygon poly = WidePolygon(vPolyData.x, vPolyData.y, vPolyData.z, vPolyData.w);
#else
    WidePolygon poly = polys[vPoly];
#endif
    uint attr = poly.attr;
    uint dispCnt = uSize.z;
    uint polyAlpha = (attr >> 16) & 31u;
    uint blendMode = (attr >> 4) & 3u;
    bool highlight = blendMode == 2u && (dispCnt & 2u) != 0u;
    bool toon = blendMode == 2u && !highlight;
    // (Bit 2 of the fog flags: no textures, for measuring what they cost on the Switch.)
    bool textured = (poly.flags & 2u) != 0u && (uFog.w & 4u) == 0u;

    uvec3 texel = uvec3(0u);
    uint ta = 0u;
    if (textured)
    {
        uint texParam = poly.texParam;
        int width = 8 << ((texParam >> 20) & 7u), height = 8 << ((texParam >> 23) & 7u);
        // Texture coordinates are in sixteenths of a texel.
        int s = (int(floor(vTex.x + 32768.0)) - 32768) >> 4;
        int t = (int(floor(vTex.y + 32768.0)) - 32768) >> 4;
        s = Wrap(s, width, (texParam & (1u << 16)) != 0u, (texParam & (1u << 18)) != 0u);
        t = Wrap(t, height, (texParam & (1u << 17)) != 0u, (texParam & (1u << 19)) != 0u);
        uint tcolor;
#ifdef WIDE_TEXBUF
        if ((poly.flags & 4u) != 0u)
        {
            uint v = texelFetch(uAtlas, int(poly.texPalette + uint(t * width + s))).r;
            tcolor = v & 0x7FFFu;
            ta = v >> 16;
        }
        else
#endif
        Lookup(texParam, poly.texPalette, s, t, tcolor, ta);
        texel = Widen(tcolor);
    }

    uint alpha = polyAlpha;
    if (textured && (blendMode & 1u) == 0u) alpha = ((ta + 1u) * (polyAlpha + 1u) - 1u) >> 5;
    if (polyAlpha == 0u) alpha = 31u; // wireframe polygons are drawn filled here
    if (alpha <= uSize.w) discard;
    if (uDraw.y == 1u && alpha != 31u) discard;
    if (uDraw.y == 2u && alpha == 31u) discard;

    uvec3 v = uvec3(clamp(ivec3(vColor + 0.5), ivec3(0), ivec3(63)));
    uint rawR = v.r;
    if (highlight)
        v.g = v.b = v.r;
    else if (toon)
        v = Widen(ToonEntry(v.r >> 1));

    uvec3 c = v;
    if (textured)
    {
        if ((blendMode & 1u) != 0u)
        {
            // decal
            if (ta == 31u) c = texel;
            else if (ta != 0u) c = (texel * ta + v * (31u - ta)) >> 5;
        }
        else
            c = ((texel + 1u) * (v + 1u) - 1u) >> 6; // modulate
    }
    if (highlight) c = min(c + Widen(ToonEntry(rawR >> 1)), uvec3(63u));
    c = (c << 2) | (c >> 4);

    oColor = vec4(vec3(c) / 255.0, float(alpha + 1u) / 32.0);

    // (The depth tested is the graphics chip's, wide_vsh.glsl; this one is the fog's.)
    int z = (poly.flags & 1u) != 0u ? int(vZPersp) : int(vZLinear);
    oAttr = uvec2(uint(max(z, 0)), (attr >> 15) & 1u);
}
