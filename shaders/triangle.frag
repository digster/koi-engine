// ============================================================================
//  triangle.frag — fragment shader (textured + Phong lighting + shadows)
// ----------------------------------------------------------------------------
//  Runs once per covered pixel. The color comes from a TEXTURE shaded by a LIGHT
//  (Step 7), modulated per-object by a MATERIAL (Step 8). Step 9 adds SHADOWS: we
//  re-project this fragment's world position into the light's view (the same one
//  used to build the shadow map) and compare its distance-from-the-light against
//  the nearest surface the light saw there. If something was closer, this fragment
//  is occluded — in shadow — so we drop its diffuse + specular (keeping ambient,
//  so shadows darken rather than turn pure black).
// ============================================================================
#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in vec3 vWorldNormal;

// Fragment samplers: the material's texture (slot 0) and the shadow map (slot 1).
layout(set = 2, binding = 0) uniform sampler2D uTex;
layout(set = 2, binding = 1) uniform sampler2D uShadowMap;

// Per-frame light + camera (set 3, binding 0). lightViewProj transforms world space
// into the light's clip space — the same matrix that built the shadow map.
layout(set = 3, binding = 0) uniform LightUBO {
    vec4 lightDir;       // xyz: the direction the light TRAVELS
    vec4 lightColor;     // rgb
    vec4 ambient;        // rgb
    vec4 cameraPos;      // xyz: the eye, for specular
    mat4 lightViewProj;  // world -> light clip space (Step 9)
};

// Per-object material (set 3, binding 1): x = shininess, y = specular strength.
layout(set = 3, binding = 1) uniform MaterialUBO {
    vec4 material;
};

layout(location = 0) out vec4 outColor;

// Returns 1.0 (fully lit) .. 0.0 (fully shadowed) for this fragment. `ndotl` (the
// surface-to-light alignment) scales the depth bias that prevents a surface from
// shadowing itself ("shadow acne") — steeply-lit surfaces need more.
float shadowFactor(float ndotl) {
    vec4 lc = lightViewProj * vec4(vWorldPos, 1.0);
    vec3 proj = lc.xyz / lc.w;                 // ortho → w = 1
    vec2 uv = proj.xy * 0.5 + 0.5;             // NDC [-1,1] -> texture [0,1]
    uv.y = 1.0 - uv.y;                         // texture origin is top-left
    float current = proj.z;                    // this fragment's depth from the light (0..1)

    // Anything outside the light's box (or beyond its far plane) is treated as lit.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || current > 1.0) {
        return 1.0;
    }

    float bias = max(0.0015, 0.005 * (1.0 - ndotl));

    // 2x2 PCF: average four nearby shadow-map taps for a softer edge than a single
    // hard compare.
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float sum = 0.0;
    for (int y = 0; y <= 1; ++y) {
        for (int x = 0; x <= 1; ++x) {
            float closest = texture(uShadowMap, uv + vec2(float(x) - 0.5, float(y) - 0.5) * texel).r;
            sum += (current - bias > closest) ? 0.0 : 1.0;
        }
    }
    return sum * 0.25;
}

void main() {
    vec3 albedo = texture(uTex, vUV).rgb * vColor;

    vec3 N = normalize(vWorldNormal);
    vec3 L = normalize(-lightDir.xyz);
    vec3 V = normalize(cameraPos.xyz - vWorldPos);
    vec3 H = normalize(L + V);

    float shininess    = material.x;
    float specStrength = material.y;

    float diffuse  = max(dot(N, L), 0.0);
    float specular = (diffuse > 0.0) ? pow(max(dot(N, H), 0.0), shininess) : 0.0;

    // How much direct light reaches this fragment (1 = lit, 0 = occluded).
    float shadow = shadowFactor(diffuse);

    // Ambient is unconditional; diffuse + specular are gated by the shadow.
    vec3 lit = albedo * (ambient.rgb + shadow * lightColor.rgb * diffuse)
             + shadow * lightColor.rgb * (specStrength * specular);

    outColor = vec4(lit, 1.0);
}
