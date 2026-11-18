// Lines and flat triangles in one colour: glyphs, the floor grid, the gizmo, the outline mask. A point
// is four floats, x y z and one the shader ignores (the glyphs' format).
// The host prepends #version 130 or #version 330 core.
in vec3 aPos;
uniform mat4 uVP;
void main() {
    gl_Position = uVP * vec4(aPos, 1.0);
}
