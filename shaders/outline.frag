// Selection outlines: the selected (red channel), hovered (green) and role-less (blue) objects are
// drawn flat into a mask; this pass lights the pixels just outside each silhouette - within uRadius
// pixels of the mask but not in it - so every shape gets its own contour.
// The host prepends #version 130 or #version 330 core.
in vec2 uv;
out vec4 o;
uniform sampler2D uMask;
uniform vec2 uTexel;
uniform float uRadius;
uniform vec4 uSelected, uHover, uGhost;
void main() {
    vec3 inside = texture(uMask, uv).rgb;
    vec3 near = inside;
    for (int i = 0; i < 12; ++i) {
        float a = 6.2831853 * float(i) / 12.0;
        vec2 d = vec2(cos(a), sin(a)) * uTexel;
        near = max(near, texture(uMask, uv + d * uRadius).rgb);
        near = max(near, texture(uMask, uv + d * (0.5 * uRadius)).rgb);
    }
    vec3 edge = clamp(near - inside, 0.0, 1.0);
    vec4 c = vec4(0.0);
    if (edge.b > 0.0) c = vec4(uGhost.rgb, uGhost.a * edge.b);
    if (edge.g > 0.0) c = vec4(uHover.rgb, uHover.a * edge.g);
    if (edge.r > 0.0) c = vec4(uSelected.rgb, uSelected.a * edge.r);
    if (c.a <= 0.003) discard;
    o = c;
}
