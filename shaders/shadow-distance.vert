// A lamp's cube of distances, one face at a time: every pixel keeps how far from the lamp the nearest
// surface in its direction is, divided by the lamp's reach (so 1.0 is "nothing there").
// The host prepends #version 130 or #version 330 core.
in vec3 aPos;
uniform mat4 uLightVP, uModel;
out vec3 vWorld;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    gl_Position = uLightVP * w;
}