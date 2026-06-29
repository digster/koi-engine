// ============================================================================
//  triangle.frag — fragment shader (Step 7: textured + Phong lighting)
// ----------------------------------------------------------------------------
//  Runs once per covered pixel. Its color now comes from a TEXTURE shaded by a
//  LIGHT. We compute the surface's base color (its "albedo" = texture × vertex
//  tint), then how brightly the light hits it using the classic Phong terms:
//
//    * AMBIENT  — a constant fill so faces away from the light aren't pure black
//                 (a cheap stand-in for light bouncing around the scene).
//    * DIFFUSE  — Lambert: brightness ∝ how directly the surface faces the light,
//                 max(dot(normal, lightDir), 0). A face square-on to the sun is
//                 fully lit; a grazing one is dim.
//    * SPECULAR — a tight highlight where the surface mirrors the light toward the
//                 eye. We use the Blinn-Phong "halfway vector" H = normalize(L+V)
//                 and raise dot(N, H) to a power (shininess) for a compact glint.
//
//  All of this happens in WORLD space, using the world position + world normal the
//  vertex shader handed us (interpolated across the triangle by the rasterizer).
// ============================================================================
#version 450

// Inputs from the vertex shader. Each "location" matches a vertex-shader "out".
layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in vec3 vWorldNormal;

// The texture (combined image+sampler) at FRAGMENT descriptor set 2, slot 0.
layout(set = 2, binding = 0) uniform sampler2D uTex;

// The light + camera, constant for the whole frame, at FRAGMENT uniform-buffer set
// 3 (samplers are set 2, uniform buffers set 3). Pushed once per frame with
// SDL_PushGPUFragmentUniformData(cmd, /*slot=*/0, ...). We use vec4s because std140
// rounds vec3 up to 16-byte alignment anyway — keeping the C++ struct simple.
layout(set = 3, binding = 0) uniform LightUBO {
    vec4 lightDir;    // xyz: the direction the light TRAVELS (e.g. sunlight going down)
    vec4 lightColor;  // rgb: the light's color/intensity
    vec4 ambient;     // rgb: constant fill light
    vec4 cameraPos;   // xyz: the eye position, for the specular view direction
};

layout(location = 0) out vec4 outColor;

// Tightness of the specular highlight and how strong it is. Constants for now; a
// real material system would make these per-object.
const float kShininess    = 32.0;
const float kSpecStrength = 0.4;

void main() {
    // Base surface color: the texture, tinted by the interpolated vertex color.
    vec3 albedo = texture(uTex, vUV).rgb * vColor;

    // Re-normalize the interpolated normal (interpolation shortens it).
    vec3 N = normalize(vWorldNormal);
    // Direction TOWARD the light is the opposite of the direction it travels.
    vec3 L = normalize(-lightDir.xyz);
    // Direction toward the camera, and the Blinn-Phong halfway vector.
    vec3 V = normalize(cameraPos.xyz - vWorldPos);
    vec3 H = normalize(L + V);

    float diffuse  = max(dot(N, L), 0.0);
    // Only add specular where the surface actually faces the light (diffuse > 0),
    // so back faces don't sprout highlights.
    float specular = (diffuse > 0.0) ? pow(max(dot(N, H), 0.0), kShininess) : 0.0;

    vec3 lit = albedo * (ambient.rgb + lightColor.rgb * diffuse)
             + lightColor.rgb * (kSpecStrength * specular);

    outColor = vec4(lit, 1.0);
}
