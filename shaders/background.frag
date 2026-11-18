// background.frag: viewport shader source.
// The host prepends #version 130 or #version 330 core.
in vec2 uv;
out vec4 o;
void main() {
    vec3 top = vec3(0.17, 0.19, 0.23), bot = vec3(0.06, 0.07, 0.09);
    o = vec4(mix(bot, top, uv.y), 1.0);
}
