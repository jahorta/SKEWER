VARYING vec4 vertexColor;
VARYING vec2 triangleCoordinate;

vec3 adjustedColor(vec3 color)
{
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, materialSaturation);
    color = (color - vec3(0.5)) * materialContrast + vec3(0.5);
    return clamp(color * materialBrightness, vec3(0.0), vec3(1.0));
}

void MAIN()
{
    vec3 color = adjustedColor(vertexColor.rgb);
    if (edgesEnabled) {
        vec3 barycentric = vec3(
            triangleCoordinate.x,
            triangleCoordinate.y,
            1.0 - triangleCoordinate.x - triangleCoordinate.y);
        vec3 smoothed = smoothstep(
            vec3(0.0), fwidth(barycentric) * edgeWidth, barycentric);
        float edgeAmount = 1.0 - min(min(smoothed.x, smoothed.y), smoothed.z);
        color = mix(color, edgeColor.rgb, edgeAmount * edgeColor.a);
    }
    FRAGCOLOR = vec4(color, vertexColor.a * materialOpacity);
}
