VARYING vec4 vertexColor;
VARYING vec2 triangleCoordinate;
VARYING vec3 fieldPosition;
VARYING float selectionAmount;

vec3 adjustedColor(vec3 color)
{
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, materialSaturation);
    color = (color - vec3(0.5)) * materialContrast + vec3(0.5);
    return clamp(color * materialBrightness, vec3(0.0), vec3(1.0));
}

float triangleEdgeAmount(vec3 barycentric, float width)
{
    vec3 smoothed = smoothstep(
        vec3(0.0), fwidth(barycentric) * width, barycentric);
    return 1.0 - min(min(smoothed.x, smoothed.y), smoothed.z);
}

void MAIN()
{
    vec3 color = adjustedColor(vertexColor.rgb);
    if (barrierPatternEnabled) {
        float stripeCoordinate = (fieldPosition.x - fieldPosition.z) / 8.0;
        float stripeDistance = abs(fract(stripeCoordinate) - 0.5);
        float stripeAntialias = max(fwidth(stripeCoordinate), 0.001);
        float stripeAmount = 1.0 - smoothstep(
            0.10, 0.10 + stripeAntialias, stripeDistance);
        const vec3 barrierInk = vec3(0.65098, 0.32549, 0.32549);
        color = mix(color, barrierInk, stripeAmount * 0.85);
    }
    vec3 barycentric = vec3(
        triangleCoordinate.x,
        triangleCoordinate.y,
        1.0 - triangleCoordinate.x - triangleCoordinate.y);
    if (edgesEnabled) {
        float edgeAmount = triangleEdgeAmount(barycentric, edgeWidth);
        color = mix(color, edgeColor.rgb, edgeAmount * edgeColor.a);
    }
    if (selectionAmount > 0.5) {
        float haloAmount = triangleEdgeAmount(barycentric, 3.0);
        float coreAmount = triangleEdgeAmount(barycentric, 1.75);
        color = mix(color, vec3(0.12549, 0.14118, 0.16863), haloAmount);
        color = mix(color, vec3(1.0), coreAmount);
    }
    FRAGCOLOR = vec4(color, vertexColor.a * materialOpacity);
}
