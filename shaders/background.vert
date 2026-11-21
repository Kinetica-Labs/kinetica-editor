// Background: a full-screen gradient.
// The host prepends #version 130 or #version 330 core.
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.9999, 1.0);
}
