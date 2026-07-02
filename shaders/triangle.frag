// ============================================================================
//  triangle.frag — fragment shader (textured + multi-light PBR + shadows)
// ----------------------------------------------------------------------------
//  Runs once per covered pixel. The color comes from a TEXTURE (albedo) shaded by
//  an ARRAY of LIGHTS (Step 11), which we loop over and ACCUMULATE. Step 12 replaces
//  the ad-hoc Blinn-Phong shading with a physically-based **Cook-Torrance** BRDF
//  driven by the material's METALLIC and ROUGHNESS parameters:
//
//    * a microfacet SPECULAR term = D·G·F / (4·(N·V)(N·L)), where D (GGX) is the
//      distribution of mirror-facets, G (Smith) their self-shadowing, and F
//      (Fresnel) the angle-dependent reflectance.
//    * a Lambertian DIFFUSE term = albedo/π, kept only for non-metals and scaled so
//      the surface never reflects more light than it receives (energy conservation).
//
//  Each light's contribution is summed; ambient is added ONCE as a crude fill (real
//  ambient / metal reflections want image-based lighting — a later step, which is why
//  metals look dark here away from the highlights). Only the directional sun (light 0)
//  casts a shadow. The three BRDF terms mirror renderer/Pbr.hpp. See the D/G/F helpers
//  below and documentation/docs/13-pbr-materials.html.
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

// Per-object material (set 3, binding 1): x = metallic (0=dielectric, 1=metal),
// y = roughness (0=mirror-smooth, 1=matte). Repurposed Blinn-Phong's x/y lanes, so
// the buffer size is unchanged.
layout(set = 3, binding = 1) uniform MaterialUBO {
    vec4 material;
};

const float PI = 3.14159265359;

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

// --- Cook-Torrance BRDF terms (CPU twins in renderer/Pbr.hpp) ----------------

// D — GGX/Trowbridge-Reitz: the fraction of microfacets aligned with the halfway
// vector H. Peaks when nDotH = 1 (a mirror facet); rougher = a lower, wider peak.
float distributionGGX(float nDotH, float roughness) {
    float a     = roughness * roughness;
    float a2    = a * a;
    float d     = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

// One half of Smith's geometry term (Schlick-GGX), for a single direction (N·V or
// N·L). k is the direct-lighting roughness remap.
float geometrySchlickGGX(float nDotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotX / (nDotX * (1.0 - k) + k);
}

// G — Smith: microfacet self-shadowing on the way IN (light) and OUT (view).
float geometrySmith(float nDotV, float nDotL, float roughness) {
    return geometrySchlickGGX(nDotV, roughness) * geometrySchlickGGX(nDotL, roughness);
}

// F — Fresnel-Schlick: reflectance climbs from F0 (head-on) to 1 at grazing angles.
// Runs per RGB channel (F0 is a vec3: 0.04 for dielectrics, the albedo for metals).
vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3 albedo = texture(uTex, vUV).rgb * vColor;

    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(cameraPos.xyz - vWorldPos);
    float nDotV = max(dot(N, V), 0.0);

    float metallic  = clamp(material.x, 0.0, 1.0);
    float roughness = clamp(material.y, 0.04, 1.0);  // a floor avoids a zero-width, unstable highlight

    // Base reflectance F0: dielectrics reflect a dim ~4% (grey), metals reflect their
    // own albedo (tinted). `mix` interpolates for the rare in-between.
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Accumulate every active light's direct contribution (the reflectance equation).
    vec3 Lo = vec3(0.0);
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

        vec3  H     = normalize(L + V);
        float nDotL = max(dot(N, L), 0.0);

        // radiance = light color * intensity * distance/cone attenuation.
        vec3 radiance = lt.colorIntensity.rgb * lt.colorIntensity.a * atten;

        // Cook-Torrance specular = D·G·F / (4·(N·V)(N·L)).
        float D = distributionGGX(max(dot(N, H), 0.0), roughness);
        float G = geometrySmith(nDotV, nDotL, roughness);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3  specular = (D * G * F) / max(4.0 * nDotV * nDotL, 1e-4);

        // Energy conservation: light not reflected specularly (1 - F) is available to
        // diffuse — and metals (which absorb all refracted light) have no diffuse.
        vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);

        // The sun (a directional light at index 0) is the only shadow caster.
        float shadow = (i == 0 && type == 0) ? shadowFactor(nDotL) : 1.0;

        Lo += shadow * (kd * albedo / PI + specular) * radiance * nDotL;
    }

    // Ambient fill added ONCE — a crude stand-in for bounced/environment light
    // (proper ambient + metal reflections need IBL, a later step).
    vec3 color = ambient.rgb * albedo + Lo;

    outColor = vec4(color, 1.0);
}
