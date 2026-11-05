#version 460
// ssrecomp: the widescreen view's polygons on the graphics chip (GPU3D_WideGpu.h).
// Vertices are read from a buffer (no vertex attributes to set up, the same in OpenGL and
// deko3D): each holds its place in output pixels, the polygon's W (for perspective-correct
// colours and texture coordinates), its depth, colour and texture coordinates.

struct WideVertex
{
    float x, y, w, z;
    float r, g, b, s;
    float t;
    uint poly;
    uint edge, pad1; // edge: a float, the distance from the edge opposite this corner
};

layout (std430, binding = 0) readonly buffer Vertices
{
    WideVertex vtx[];
};

struct WidePolygon
{
    uint attr, texParam, texPalette, flags;
};

layout (std430, binding = 1) readonly buffer Polygons
{
    WidePolygon polys[];
};

layout (std140, binding = 0) uniform Frame
{
    uvec4 uSize;          // width, height, DISP3DCNT, alpha test reference
    vec4 uScale;          // 2 / width, 2 / height, direction of y (+1 deko3D, -1 OpenGL), depth range (0: 0 to 1, 1: -1 to 1)
    uvec4 uFog;           // fog colour, offset, shift, flags (bit 0 fog on, bit 1 fog colours, bit 2 no textures)
    uvec4 uToon[8];       // the toon table, four to an entry
    uvec4 uFogDensity[9]; // the fog density table (34 entries), four to an entry
};

layout (std140, binding = 1) uniform Draw
{
    uvec4 uDraw;          // first vertex, which pixels (0 all, 1 opaque only, 2 translucent only), depth bias, W-buffering
};

flat out uint vPoly;
// The polygon's attributes, for wide_fsh.glsl's WIDE_TEXBUF variant (read once a vertex, not
// once a pixel).
flat out uvec4 vPolyData;
out vec3 vColor;
out vec2 vTex;
noperspective out float vZLinear;
out float vZPersp;
// Distances to the three edges of the triangle, in pixels as an edge passes through them; an
// edge that is not one of the polygon's own (a fan's diagonal) is far away.
noperspective out vec3 vEdge;

void main()
{
    WideVertex v = vtx[uDraw.x + uint(gl_VertexID)];
    // Rows run from the top of the picture in memory with both APIs: OpenGL's start at the
    // bottom of the view, deko3D's at the top.
    vec2 ndc = vec2(v.x * uScale.x - 1.0, uScale.z * (1.0 - v.y * uScale.y));
    // The depth tested by the graphics chip's own depth test (written by the shader, the test
    // would come after the shading, and every hidden pixel of the city's buildings was shaded:
    // 15 ms a 720-line picture). It has to vary straight across the screen: the DS's Z does;
    // with W-buffering the DS stores W, which does not, but 1/W does, so 1/W is tested the
    // other way round (GPU3D_WideGpu.cpp). Equal-depth polygons are moved back by their
    // tolerance (uDraw.z).
    float bias = float(uDraw.z);
    float d = uDraw.w != 0u ? 1.0 / max(v.z - bias, 1.0) : max(v.z - bias, 0.0) / 16777216.0;
    d = clamp(d, 0.0, 1.0);
    if (uScale.w != 0.0) d = d * 2.0 - 1.0;
    gl_Position = vec4(ndc * v.w, d * v.w, v.w);
    vPoly = v.poly;
    WidePolygon p = polys[v.poly];
    vPolyData = uvec4(p.attr, p.texParam, p.texPalette, p.flags);
    vColor = vec3(v.r, v.g, v.b);
    vTex = vec2(v.s, v.t);
    // Z-buffered depth runs straight across the screen; W-buffered depth is W itself,
    // which perspective-correct interpolation gives (the vertices carry it scaled alike).
    vZLinear = v.z;
    vZPersp = v.z;
    vEdge = vec3(0.0);
    vEdge[gl_VertexID % 3] = uintBitsToFloat(v.edge);
}
