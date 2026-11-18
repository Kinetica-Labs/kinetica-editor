// lines.frag: one colour for the whole draw (Viewport::drawLines).
// The host prepends #version 130 or #version 330 core.
uniform vec4 uColor;
out vec4 o;
void main() {
    o = uColor;
}
