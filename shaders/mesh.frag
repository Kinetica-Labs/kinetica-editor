// Lit surfaces (mesh.vert says how they are lit): a triplanar pattern or picture, a surface-gradient
// bump, a normal map, see-through glass and optional shadows.
// The host prepends #version 130 or #version 330 core.
in vec3 vN;
in vec3 vPosV;
in vec3 vColor;
in vec3 vMaterialPos, vMaterialNormal;
uniform int uTextureKind;
uniform float uTextureRepeats;
uniform sampler2D uAlbedo, uHeightMap;
uniform int uHeightMapEnabled;
uniform float uBumpHeight;
uniform sampler2D uNormalMap;
uniform int uNormalMapEnabled;
uniform mat4 uModel, uView;
uniform mat3 uTextureFrame;
// Material: project along three axes and blend by the surface normal.
float checker(vec2 uv) {
    vec2 cell = floor(uv * 2.0);
    return mix(0.08, 0.95, mod(cell.x + cell.y, 2.0));
}
// A speckle: each cell the body's colour times a grey from a hash of its index, no period (ke::TextureKind::Speckle).
float speckle(vec2 uv) {
    vec2 cell = floor(uv * 2.0);
    uint h = uint(int(cell.x)) * 73856093u ^ uint(int(cell.y)) * 19349663u ^ 0x9e3779b9u;
    h ^= h >> 16u; h *= 0x7feb352du; h ^= h >> 15u; h *= 0x846ca68bu; h ^= h >> 16u;
    return 0.08 + 0.87 * float(h >> 8u) / 16777216.0;
}
// A cell's grey in [0, 1), and value noise over the cells.
float cellValue(ivec2 c) {
    uint h = uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ 0x9e3779b9u;
    h ^= h >> 16u; h *= 0x7feb352du; h ^= h >> 15u; h *= 0x846ca68bu; h ^= h >> 16u;
    return float(h >> 8u) / 16777216.0;
}
float valueNoise(vec2 s) {
    vec2 i = floor(s), f = s - i, u = f * f * (3.0 - 2.0 * f);
    ivec2 c = ivec2(i);
    return mix(mix(cellValue(c), cellValue(c + ivec2(1, 0)), u.x), mix(cellValue(c + ivec2(0, 1)), cellValue(c + ivec2(1, 1)), u.x), u.y);
}
// A granite: four octaves of value noise and round grains dark and light (ke::TextureKind::Stone); uv in repeats, two cells each.
float stone(vec2 uv) {
    vec2 s = uv * 2.0;
    float n = 0.5 * valueNoise(s) + 0.25 * valueNoise(2.0 * s + vec2(17.3, 5.1)) + 0.125 * valueNoise(4.0 * s + vec2(3.7, 29.9)) +
              0.0625 * valueNoise(8.0 * s + vec2(41.2, 13.4));
    vec2 f8 = 8.0 * s, i8 = floor(f8);
    ivec2 c8 = ivec2(i8);
    float g = cellValue(c8 + ivec2(1013, 7919));
    float fleck = 0.0;
    if (g > 0.70) { // a round grain, its centre a hash within the cell, its edge soft
        vec2 d = f8 - i8 - vec2(0.3 + 0.4 * cellValue(c8 + ivec2(2027, 113)), 0.3 + 0.4 * cellValue(c8 + ivec2(331, 4093)));
        fleck = clamp((0.09 - dot(d, d)) / 0.03, 0.0, 1.0) * (g > 0.85 ? -0.30 : 0.18);
    }
    return clamp(0.66 + 0.9 * (n - 0.47) + fleck, 0.05, 1.0);
}
// The material's pattern at this point, once a pixel: a picture's colour (2), a checker's grey (1), or a grey the
// colour is multiplied by (3 speckle, 4 stone), projected along the three axes and blended by the surface normal
// (triplanar). A procedural projection whose weight is under 0.4 % is left out: a floor is one projection, not three -
// the granite is twenty hashes a projection, and a GPU's whole frame went to it. A picture keeps its three reads: a
// read inside a branch has no derivatives for its mip level.
vec3 surfacePattern() {
    vec3 n = pow(abs(vMaterialNormal), vec3(8.0));
    n /= max(n.x + n.y + n.z, 1e-8);
    vec3 p = vMaterialPos * uTextureRepeats;
    if (uTextureKind == 2)
        return n.x * texture(uAlbedo, p.zy + vec2(0.5)).rgb + n.y * texture(uAlbedo, p.xz + vec2(0.5)).rgb + n.z * texture(uAlbedo, p.xy + vec2(0.5)).rgb;
    float g = 0.0;
    if (n.x > 0.004) g += n.x * (uTextureKind == 4 ? stone(p.zy) : uTextureKind == 3 ? speckle(p.zy) : checker(p.zy));
    if (n.y > 0.004) g += n.y * (uTextureKind == 4 ? stone(p.xz) : uTextureKind == 3 ? speckle(p.xz) : checker(p.xz));
    if (n.z > 0.004) g += n.z * (uTextureKind == 4 ? stone(p.xy) : uTextureKind == 3 ? speckle(p.xy) : checker(p.xy));
    return vec3(g);
}
vec3 surfaceAlbedo(vec3 color, vec3 pattern) {
    return uTextureKind == 0 ? color : uTextureKind == 1 || uTextureKind == 2 ? pattern : color * pattern;
}
// Surface gradient: solve grad(h).dPdx = dhdx and grad(h).dPdy = dhdy.
float materialHeight(vec3 pattern) {
    if (uHeightMapEnabled != 0) {
        vec3 n = pow(abs(vMaterialNormal), vec3(8.0));
        n /= max(n.x + n.y + n.z, 1e-8);
        vec3 p = vMaterialPos * uTextureRepeats;
        vec3 h = n.x * texture(uHeightMap, p.zy + vec2(0.5)).rgb +
                 n.y * texture(uHeightMap, p.xz + vec2(0.5)).rgb +
                 n.z * texture(uHeightMap, p.xy + vec2(0.5)).rgb;
        return dot(h, vec3(0.2126, 0.7152, 0.0722));
    }
    return dot(surfaceAlbedo(vec3(0.5), pattern), vec3(0.2126, 0.7152, 0.0722)); // the pattern itself, its grey
}
vec3 bumpedNormal(vec3 normal, vec3 pattern) {
    if (uBumpHeight <= 0.0) return normal;
    float height = materialHeight(pattern);
    vec3 dx = dFdx(vPosV), dy = dFdy(vPosV);
    vec3 r1 = cross(dy, normal), r2 = cross(normal, dx);
    float det = dot(dx, r1);
    if (abs(det) < 1e-20) return normal;
    vec3 gradient = dFdx(height) * r1 + dFdy(height) * r2;
    return normalize(abs(det) * normal - sign(det) * uBumpHeight * gradient);
}
// A normal map (green up), read as the picture is - along the three axes of the material's frame. Each reading is
// turned from its own plane into the frame, leaning on the surface's own normal there, and the three are blended by
// the same weights as the picture; then out of the frame into the view, where the lights are.
vec3 mappedNormal(vec3 normal) {
    if (uNormalMapEnabled == 0) return normal;
    vec3 m = normalize(vMaterialNormal);
    vec3 w = pow(abs(m), vec3(8.0));
    w /= max(w.x + w.y + w.z, 1e-8);
    vec3 p = vMaterialPos * uTextureRepeats;
    vec3 tx = texture(uNormalMap, p.zy + vec2(0.5)).xyz * 2.0 - 1.0;
    vec3 ty = texture(uNormalMap, p.xz + vec2(0.5)).xyz * 2.0 - 1.0;
    vec3 tz = texture(uNormalMap, p.xy + vec2(0.5)).xyz * 2.0 - 1.0;
    tx = vec3(tx.xy + m.zy, abs(tx.z) * m.x);
    ty = vec3(ty.xy + m.xz, abs(ty.z) * m.y);
    tz = vec3(tz.xy + m.xy, abs(tz.z) * m.z);
    vec3 inFrame = normalize(tx.zyx * w.x + ty.xzy * w.y + tz.xyz * w.z);
    vec3 seen = normalize(mat3(uView) * mat3(uModel) * (transpose(uTextureFrame) * inFrame));
    return dot(normalize(vN), normal) < 0.0 ? -seen : seen; // a face seen from behind: turned as its own normal was
}
uniform vec3 uColor;
uniform int uVertexColor; // 1: the mesh's own colours (a model's materials) instead of uColor
uniform float uAlpha = 1.0; // < 1: glass (drawn blended, after everything opaque)
uniform int uLightCount;     // the scene's lights, up to 8: suns, lamps and spotlights
uniform vec3 uLightPos[8], uLightDir[8], uLightColor[8]; // uLightDir: where it shines; colour * intensity
uniform float uLightRange[8], uLightCosOuter[8], uLightCosInner[8]; // a lamp: cosines -2 and -1 (no cone)
uniform int uLightSun[8];    // 1: a sun - parallel light along uLightDir, no fading, no cone
uniform int uLightShadow[8]; // the shadow map it casts with (0 .. 3), -1: no shadow
out vec4 o;
#ifdef KE_SHADOWS
// The four shadow maps. GLSL 1.30 cannot pick a sampler by a variable index, so each one has its own
// name: a depth map for a sun or a spotlight, a cube of distances for a lamp.
uniform sampler2D uShadowMap0, uShadowMap1, uShadowMap2, uShadowMap3;
uniform samplerCube uShadowCube0, uShadowCube1, uShadowCube2, uShadowCube3;
uniform mat4 uShadowFromView[4]; // view space -> the depth map's [0, 1] cube (sun, spotlight)
uniform vec2 uShadowLens[4];     // a spotlight's near and far plane (to make its depth linear); 0 0: a sun
uniform float uShadowTexel[4];   // one texel of the depth map, in [0, 1] units
uniform vec3 uShadowLamp[4];     // a lamp's position in the world
uniform float uShadowFar[4];     // a lamp's cube holds distance / uShadowFar
uniform mat4 uViewToWorld;       // the lamps' cubes live in world space

// A depth read from a spotlight's map (0 .. 1, crowded near the far plane) back as a distance along
// its axis: the inverse of the perspective depth, z = 2 n f / (f + n - (2 d - 1)(f - n)).
float linearDepth(float d, vec2 lens) {
    return 2.0 * lens.x * lens.y / (lens.y + lens.x - (2.0 * d - 1.0) * (lens.y - lens.x));
}

// How much of a sun or a spotlight reaches this point: its depth map holds, in every direction from
// the light, the depth of the nearest surface; the point is lit where it is not deeper. The fraction of the 3 x 3 texels around it
// that it is in front of softens the edge a texel wide (percentage-closer filtering). The bias keeps a lit face from shadowing itself: a little depth for a sun, a few
// centimetres (growing with distance) for a spotlight.
float litByMap(sampler2D map, int s) {
    vec4 h = uShadowFromView[s] * vec4(vPosV, 1.0);
    vec3 p = h.xyz / h.w;
    if (h.w <= 0.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    bool spot = uShadowLens[s].y > 0.0;
    float here = spot ? linearDepth(p.z, uShadowLens[s]) : p.z;
    float bias = spot ? 0.01 + 0.01 * here : 0.003;
    float lit = 0.0;
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j) {
            float stored = texture(map, p.xy + vec2(i, j) * uShadowTexel[s]).r;
            if (spot) stored = linearDepth(stored, uShadowLens[s]);
            lit += here - bias <= stored ? 1.0 : 0.0;
        }
    return lit / 9.0;
}

// How much of a lamp reaches this point: the lamp's cube map holds, in every direction, the distance
// to the nearest surface (an omnidirectional shadow map); the point
// is lit where it is not farther. Five samples around the direction soften the edge.
float litByCube(samplerCube cube, int s) {
    vec3 w = (uViewToWorld * vec4(vPosV, 1.0)).xyz - uShadowLamp[s];
    float d = max(length(w), 1e-4);
    vec3 a = normalize(cross(w, abs(w.y) < 0.99 * d ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(w / d, a);
    float r = 0.012 * d, bias = 0.02 + 0.02 * d, lit = 0.0;
    vec3 taps[5] = vec3[5](vec3(0.0), a * r, -a * r, b * r, -b * r);
    for (int k = 0; k < 5; ++k) lit += d - bias <= texture(cube, w + taps[k]).r * uShadowFar[s] ? 1.0 : 0.0;
    return lit / 5.0;
}

// The shadow of light i at this point: 1 lit, 0 in shadow. Only the slots drawn this frame are
// compiled in, each with its own kind (KE_SLOTk_MAP: a sun's or a spotlight's depth map, KE_SLOTk_CUBE:
// a lamp's cube; Viewport::buildMeshShader): a lookup that is never used is not free - the software
// renderer runs every branch of a shader, and all eight made each frame ten times slower.
float shadowOf(int i) {
    int s = uLightShadow[i];
#if defined(KE_SLOT0_MAP)
    if (s == 0) return litByMap(uShadowMap0, 0);
#elif defined(KE_SLOT0_CUBE)
    if (s == 0) return litByCube(uShadowCube0, 0);
#endif
#if defined(KE_SLOT1_MAP)
    if (s == 1) return litByMap(uShadowMap1, 1);
#elif defined(KE_SLOT1_CUBE)
    if (s == 1) return litByCube(uShadowCube1, 1);
#endif
#if defined(KE_SLOT2_MAP)
    if (s == 2) return litByMap(uShadowMap2, 2);
#elif defined(KE_SLOT2_CUBE)
    if (s == 2) return litByCube(uShadowCube2, 2);
#endif
#if defined(KE_SLOT3_MAP)
    if (s == 3) return litByMap(uShadowMap3, 3);
#elif defined(KE_SLOT3_CUBE)
    if (s == 3) return litByCube(uShadowCube3, 3);
#endif
    return 1.0;
}
#else
// No light casts a shadow this frame: everything is lit, and the shadow code is left out entirely.
float shadowOf(int i) { return 1.0; }
#endif
// Lighting: diffuse and specular, then the scene lights or the built-in preview light.
vec3 oneLight(vec3 base, vec3 n, vec3 v, vec3 l, vec3 radiance) {
    float diff = max(dot(n, l), 0.0);
    float spec = diff > 0.0 ? pow(max(dot(n, normalize(l + v)), 0.0), 32.0) * 0.25 : 0.0;
    return (base * diff * 0.8 + vec3(spec)) * radiance;
}
vec3 sceneLights(vec3 base, vec3 n, vec3 v) {
    vec3 c = base * (0.18 + 0.1 * max(dot(n, v), 0.0)); // the ambient stays
    for (int i = 0; i < 8; ++i) {
        if (i >= uLightCount) break;
        vec3 l = -uLightDir[i];    // a sun: the same direction everywhere, never fading
        float fade = 1.0, cone = 1.0;
        if (uLightSun[i] == 0) {   // a lamp or a spotlight: from its position, fading with distance
            vec3 toLight = uLightPos[i] - vPosV;
            float d = length(toLight);
            l = toLight / max(d, 1e-5);
            float x = clamp(d / uLightRange[i], 0.0, 1.0);
            fade = (1.0 - x * x) * (1.0 - x * x);
            cone = smoothstep(uLightCosOuter[i], uLightCosInner[i], dot(-l, uLightDir[i]));
        }
        if (fade * cone > 0.0) c += oneLight(base, n, v, l, uLightColor[i]) * fade * cone * shadowOf(i);
    }
    return c;
}
void main() {
    vec3 n = normalize(vN);
    vec3 v = normalize(-vPosV);
    if (dot(n, v) < 0.0) n = -n;
    vec3 pattern = uTextureKind == 0 ? vec3(1.0) : surfacePattern();
    n = bumpedNormal(mappedNormal(n), pattern);
    vec3 base = surfaceAlbedo(uVertexColor == 1 ? vColor : uColor, pattern);
    vec3 l = normalize(vec3(0.35, 0.7, 0.6));
    float diff = max(dot(n, l), 0.0);
    float fill = max(dot(n, v), 0.0);
    vec3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), 48.0) * 0.3;
    vec3 c = base * (0.18 + 0.55 * diff + 0.35 * fill) + vec3(spec);
    if (uLightCount > 0) c = sceneLights(base, n, v);
    // Glass: the rim (grazing view) is brighter, the face almost clear (Schlick's Fresnel).
    float alpha = uAlpha < 1.0 ? uAlpha + (1.0 - uAlpha) * pow(1.0 - max(dot(n, v), 0.0), 4.0) : 1.0;
    o = vec4(c, alpha);
}
