// A sun's or a spotlight's shadow map: the depth of every body as the light sees it (nothing else is
// written).
// The host prepends #version 130 or #version 330 core.
in vec3 aPos;
uniform mat4 uLightVP, uModel;
void main() {
    gl_Position = uLightVP * uModel * vec4(aPos, 1.0);
}