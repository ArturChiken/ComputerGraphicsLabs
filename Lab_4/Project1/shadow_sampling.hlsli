// Included by the lighting shader and the GPU verification shader.
uint SelectCascade(float viewDepth)
{
    return min(uint(viewDepth > gCascadeSplits.x) + uint(viewDepth > gCascadeSplits.y)
        + uint(viewDepth > gCascadeSplits.z), 3u);
}

float SampleCascade(uint cascade, float3 worldPosition, float3 normal)
{
    float4 lightClip = mul(float4(worldPosition, 1), gShadowViewProjection[cascade]);
    float3 projected = lightClip.xyz / lightClip.w;
    float2 uv = projected.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0) || any(uv > 1) || projected.z < 0 || projected.z > 1) return 1;
    float slope = 1 - saturate(dot(normal, -normalize(gDirectionalLights[0].directionIntensity.xyz)));
    float depth = projected.z - gShadowConfig.z * (1 + 2 * slope);
    int radius = (int)gShadowParams.y;
    if (radius == 0)
    {
        int resolution = (int)round(1.0 / gShadowParams.z);
        int2 pixel = min(int2(uv * resolution), resolution - 1);
        return depth <= gShadowMaps.Load(int4(pixel, cascade, 0)) ? 1 : 0;
    }
    float visibility = 0;
    [loop]
    for (int y = -radius; y <= radius; ++y)
        [loop]
        for (int x = -radius; x <= radius; ++x)
            visibility += gShadowMaps.SampleCmpLevelZero(gShadowSampler,
                float3(uv + float2(x, y) * gShadowParams.z, cascade), depth);
    return visibility / ((radius * 2 + 1) * (radius * 2 + 1));
}

float DirectionalShadow(float3 worldPosition, float3 normal)
{
    if (gShadowParams.x < 0.5) return 1;
    // LH camera space: forward depth is positive z, not Euclidean distance.
    float viewDepth = mul(float4(worldPosition, 1), gView).z;
    if (viewDepth < gShadowConfig.x || viewDepth > gCascadeSplits.w) return 1;
    uint cascade = SelectCascade(viewDepth);
    float visibility = SampleCascade(cascade, worldPosition, normal);
    float start = cascade == 0 ? gShadowConfig.x : gCascadeSplits[cascade - 1];
    float blendWidth = max(0.001, (gCascadeSplits[cascade] - start) * gShadowConfig.y);
    if (cascade < 3 && viewDepth > gCascadeSplits[cascade] - blendWidth)
    {
        float next = SampleCascade(cascade + 1, worldPosition, normal);
        float blend = saturate((viewDepth - (gCascadeSplits[cascade] - blendWidth)) / blendWidth);
        visibility = lerp(visibility, next, blend);
    }
    float fade = smoothstep(gCascadeSplits.w * gShadowConfig.w, gCascadeSplits.w, viewDepth);
    return lerp(visibility, 1, fade);
}
