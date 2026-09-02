#version 460
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() {
    vec2 d = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(d, d);
    if (r2 > 1.0) discard;
    float a = exp(-r2 * 3.5) * (1.0 - r2);
    outColor = vec4(vColor.rgb * a, a);     // additive blend (ONE, ONE)
}
