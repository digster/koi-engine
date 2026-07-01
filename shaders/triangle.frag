// ============================================================================
//  triangle.frag — fragment shader (textured + multi-light Phong + shadows)
// ----------------------------------------------------------------------------
//  Runs once per covered pixel. The color comes from a TEXTURE shaded by LIGHTS,
//  modulated per-object by a MATERIAL (Step 8). Step 11 upgrades the single
//  hardcoded "sun" into an ARRAY of lights we loop over and ACCUMULATE:
//
//    * DIRECTIONAL lights (the sun) shine with parallel rays — a direction, no
//      position, no distance falloff.
//    * POINT lights sit at a position and fall off with distance (attenuation).
//    * SPOT lights are point lights confined to a cone (a flashlight).
//
//  Each light's diffuse + specular is added together; ambient is added ONCE. Only
//  the directional sun (light 0) casts a shadow — shadowing every light needs many
//  shadow maps (cube maps / cascades), a later step. See docs/12-multiple-lights.html.
// ============================================================================
#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in vec3 vWorldNormal;

// Fragment samplers: the material's texture (slot 0) and the shadow map (slot 1).
layout(set = 2, binding = 0) uniform sampler2D uTex;
layout(set = 2, binding = 1) uniform sampler2D uShadowMap;

// Must match koi::MAX_LIGHTS (scene/Light.hpp): the uniform array is a FIXED size
// because the buffer's layout is fixed when the pipeline is built.
#define MAX_LIGHTS 8

// One light, packed into vec4s so its std140 layout is unambiguous (each field is
// 16-byte aligned — the C++ side in GpuRenderer mirrors this exactly). We fold
// small scalars into unused .w/.a lanes to keep the struct tight.
struct GpuLight {
    vec4 positionRange;   // xyz: world position (point/spot); w: range (falloff radius)
    vec4 directionType;   // xyz: direction it points (dir/spot); w: type (0=dir,1=point,2=spot)
    vec4 colorIntensity;  // rgb: color; a: intensity (scalar brightness)
    vec4 spotCutoffs;     // x: cos(inner cone half-angle); y: cos(outer)
};

// Per-frame lighting environment (set 3, binding 0). lightViewProj transforms world
// space into the SUN's clip space — the same matrix that built the shadow map.
layout(set = 3, binding = 0) uniform LightUBO {
    vec4     ambient;       // rgb: constant fill light (added once, everywhere)
    vec4     cameraPos;     // xyz: the eye, for specular
    mat4     lightViewProj; // world -> sun clip space (for shadow lookup)
    ivec4    lightCount;    // x: number of active lights in the array below
    GpuLight lights[MAX_LIGHTS];
};

// Per-object material (set 3, binding 1): x = shininess, y = specular strength.
layout(set = 3, binding = 1) uniform MaterialUBO {
    vec4 material;
};

layout(location = 0) out vec4 outColor;

// Returns 1.0 (fully lit) .. 0.0 (fully shadowed) for this fragment, using the sun's
// shadow map. `ndotl` (the surface-to-light alignment) scales the depth bias that
// prevents a surface from shadowing itself ("shadow acne") — steeply-lit surfaces
// need more.
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

// Distance attenuation (windowed inverse-square). CPU twin: koi::attenuation in
// scene/Light.hpp — keep the two in lock-step. ~1 at d=0, hits 0 exactly at range.
float attenuation(float dist, float range) {
    if (range <= 0.0) {
        return 0.0;
    }
    float ratio  = dist / range;
    float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    window *= window;
    return window / (dist * dist + 1.0);
}

void main() {
    vec3 albedo = texture(uTex, vUV).rgb * vColor;

    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(cameraPos.xyz - vWorldPos);

    float shininess    = material.x;
    float specStrength = material.y;

    // Ambient is unconditional and added ONCE (it stands in for bounced light that
    // reaches surfaces regardless of any direct source).
    vec3 result = albedo * ambient.rgb;

    // Accumulate every active light's direct contribution.
    int count = min(lightCount.x, MAX_LIGHTS);
    for (int i = 0; i < count; ++i) {
        GpuLight lt = lights[i];
        int  type  = int(lt.directionType.w);

        // Direction to the light (L) and how much of it reaches here (atten).
        vec3  L;
        float atten = 1.0;
        if (type == 0) {
            // Directional: parallel rays, no position, no falloff. lightDir stores
            // the direction the light TRAVELS, so L (toward the light) is its negative.
            L = normalize(-lt.directionType.xyz);
        } else {
            // Point/spot: L points from the surface toward the light's position, and
            // brightness falls off with distance.
            vec3 toLight = lt.positionRange.xyz - vWorldPos;
            float dist = length(toLight);
            L = (dist > 0.0) ? toLight / dist : vec3(0.0, 1.0, 0.0);
            atten = attenuation(dist, lt.positionRange.w);
            if (type == 2) {
                // Spot: further fade by the cone. Compare the cosine of the angle
                // between the spot's aim and the fragment against the cone cutoffs.
                float theta = dot(L, normalize(-lt.directionType.xyz));
                atten *= smoothstep(lt.spotCutoffs.y, lt.spotCutoffs.x, theta);
            }
        }

        // Blinn-Phong terms for THIS light.
        float ndotl    = max(dot(N, L), 0.0);
        vec3  H        = normalize(L + V);
        float specular = (ndotl > 0.0) ? pow(max(dot(N, H), 0.0), shininess) : 0.0;

        // The sun (a directional light at index 0) is the only shadow caster.
        float shadow = (i == 0 && type == 0) ? shadowFactor(ndotl) : 1.0;

        // radiance = color * intensity * distance/cone attenuation.
        vec3 radiance = lt.colorIntensity.rgb * lt.colorIntensity.a * atten;

        result += shadow * radiance * (albedo * ndotl + specStrength * specular);
    }

    outColor = vec4(result, 1.0);
}
