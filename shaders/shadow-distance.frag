// shadow-distance.frag: viewport shader source.
// The host prepends #version 130 or #version 330 core.
in vec3 vWorld;
uniform vec3 uLamp;
uniform float uFar;
out vec4 o;
void main() {
    o = vec4(length(vWorld - uLamp) / uFar);
}