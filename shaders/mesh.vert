// Lit meshes: the robot's bodies and the scene's shapes; also see-through marks (uAlpha < 1).
// Lighting: the scene's own lights when it has any (Frame::lights: up to eight suns, lamps
// and spotlights, all in view space) - Lambert diffuse, a soft Blinn-Phong highlight, a lamp fading
// quadratically to nothing at its range, a spotlight's cone with a smooth edge - over a constant
// ambient. Up to four of them cast shadows (ViewportShadows.cpp): a sun or a spotlight from a depth
// map, a lamp from a cube of distances, each softened by a few neighbouring samples (PCF) - compiled
// in only while some light casts one (KE_SHADOWS). No lights: the viewer's own light from the top
// left and a head light.
// The host prepends #version 130 or #version 330 core.
in vec3 aPos;      // in the body's own frame, metres
in vec3 aNormal;
in vec3 aColor;    // a model's own colour a triangle (TriMesh::triangleColors), used where uVertexColor is 1
in vec2 aUV;       // a model's own texture coordinate a corner (TriMesh::uvs), used where uUvMap is 1
uniform mat4 uModel, uView, uProj;
uniform mat3 uTextureFrame;
uniform vec3 uTextureOffset;
out vec3 vMaterialPos, vMaterialNormal;
out vec3 vN;
out vec3 vPosV;
out vec3 vColor;
out vec2 vUV;
void main() {
    vec4 vp = uView * uModel * vec4(aPos, 1.0);
    vPosV = vp.xyz;
    vN = mat3(uView) * mat3(uModel) * aNormal;
    vColor = aColor;
    vUV = aUV;
    vMaterialPos = uTextureFrame * aPos + uTextureOffset;
    vMaterialNormal = uTextureFrame * aNormal;
    gl_Position = uProj * vp;
}
