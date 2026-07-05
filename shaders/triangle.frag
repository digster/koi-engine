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
//  Each light's contribution is summed; the ambient term is then added ONCE. Only the
//  directional sun (light 0) casts a shadow. The three BRDF terms mirror renderer/Pbr.hpp.
//  See the D/G/F helpers below and documentation/docs/13-pbr-materials.html.
//
//  Step 15 upgrades that ambient term from a flat constant into IMAGE-BASED LIGHTING: the
//  surface is lit by the environment (the baked skybox) via a diffuse irradiance map and a
//  specular prefiltered map + BRDF LUT (the split-sum approximation). This is what finally
//  makes metals reflect their surroundings instead of looking dark. A runtime flag
//  (ambient.w) switches between IBL and the old flat fill. See documentation/docs/16-image-based-lighting.html.
//
//  Step 13 drives the material PER PIXEL from texture maps instead of scalars: an
//  albedo map, a packed metallic-roughness map (glTF G=roughness, B=metallic), an AO
//  map, and — the conceptually new part — a tangent-space NORMAL map. The normal map is
//  applied via the TBN basis built from the interpolated normal + tangent, adding fine
//  surface relief without extra geometry. See documentation/docs/14-texture-and-normal-maps.html.
// ============================================================================
#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in vec3 vWorldNormal;
layout(location = 4) in vec3 vWorldTangent;

// Fragment samplers (Step 13). Slots 0–3 are the per-material maps bound per draw;
// slot 4 is the shared shadow map bound once per frame:
//   0 uAlbedoMap      — base colour
//   1 uMetalRoughMap  — glTF packing: G = roughness, B = metallic
//   2 uNormalMap      — tangent-space normals (rgb, ×2−1 to decode)
//   3 uAoMap          — ambient occlusion (R channel)
//   4 uShadowMap      — the sun's depth map
// A material that omits a map gets a neutral 1×1 fallback (white, or a flat normal),
// so the maths below reduces to the scalar-only Step 12 look with no branching.
//
// Slots 5–7 are the shared IBL maps (Step 15), bound once per frame like the shadow map:
//   5 uIrradianceMap  — DIFFUSE environment light (the sky cosine-convolved), by normal
//   6 uPrefilterMap   — SPECULAR environment (the sky GGX-blurred), roughness in the mips
//   7 uBrdfLut        — the split-sum's environment-independent scale/bias, by (N·V,rough)
// Slot 8 is the per-material EMISSIVE map (Step 16), bound per draw like slots 0–3:
//   8 uEmissiveMap    — self-emitted colour, ADDED after shading (glTF emissive_texture)
layout(set = 2, binding = 0) uniform sampler2D uAlbedoMap;
layout(set = 2, binding = 1) uniform sampler2D uMetalRoughMap;
layout(set = 2, binding = 2) uniform sampler2D uNormalMap;
layout(set = 2, binding = 3) uniform sampler2D uAoMap;
layout(set = 2, binding = 4) uniform sampler2D uShadowMap;
layout(set = 2, binding = 5) uniform samplerCube uIrradianceMap;
layout(set = 2, binding = 6) uniform samplerCube uPrefilterMap;
layout(set = 2, binding = 7) uniform sampler2D   uBrdfLut;
layout(set = 2, binding = 8) uniform sampler2D   uEmissiveMap;

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

// Per-object material (set 3, binding 1). `material`: x = metallic FACTOR, y = roughness
// FACTOR — multipliers on the metallic-roughness MAP's channels since Step 13 (a white
// fallback leaves them unchanged); z is unused; w = OPACITY (Step 21, glTF baseColorFactor.a):
// the surface's alpha, multiplied by the albedo map's own alpha to form outColor.a. For an
// opaque material this is 1, and since the opaque pipeline has blending off it's ignored.
// `emissive` (Step 16): rgb = the emissive FACTOR (glTF emissiveFactor × emissive_strength),
// multiplying the emissive map; 0 = no glow.
layout(set = 3, binding = 1) uniform MaterialUBO {
    vec4 material;
    vec4 emissive;
};

const float PI = 3.14159265359;

// The prefilter cube stores roughness across its mip chain; this is the last usable mip
// index. MUST match kPrefilterMipLevels-1 in GpuRenderer.cpp (5 levels → LOD 4).
const float MAX_REFLECTION_LOD = 4.0;

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

// Roughness-aware Fresnel for the AMBIENT/IBL term (Step 15). With no single light there's
// no H·V, so N·V stands in; the grazing reflectance is capped by (1-roughness) so rough
// surfaces don't gain an unrealistic bright rim from the environment. CPU twin:
// koi::fresnelSchlickRoughness in renderer/Pbr.hpp.
vec3 fresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness) {
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // Sample the base-colour map ONCE and keep the whole rgba: .rgb tints the surface
    // (× the vertex colour) as before, while .a feeds the transparency alpha below.
    vec4 albedoSample = texture(uAlbedoMap, vUV);
    vec3 albedo = albedoSample.rgb * vColor;

    // --- Normal mapping: perturb the geometric normal with the normal map ---------
    // Build the TBN basis at this fragment. The interpolated tangent may have drifted
    // slightly non-perpendicular to the normal, so re-orthonormalize it (Gram-Schmidt);
    // the bitangent is N×T (we store only a vec3 tangent — see Tangents.hpp). The map's
    // rgb is a unit normal packed into [0,1]; ×2−1 decodes it back to [-1,1], then the
    // TBN matrix rotates it from tangent space into world space. A flat fallback normal
    // (0,0,1) leaves N equal to the geometric normal.
    vec3 N = normalize(vWorldNormal);
    vec3 T = normalize(vWorldTangent - N * dot(N, vWorldTangent));
    vec3 B = cross(N, T);
    vec3 nTex = texture(uNormalMap, vUV).xyz * 2.0 - 1.0;
    N = normalize(mat3(T, B, N) * nTex);

    vec3 V = normalize(cameraPos.xyz - vWorldPos);
    float nDotV = max(dot(N, V), 0.0);

    // Per-pixel material: the packed metallic-roughness map (glTF G=roughness,
    // B=metallic) scaled by the material factors, and the AO map. White fallbacks make
    // these reduce to the scalar-only Step 12 values.
    vec3 mr = texture(uMetalRoughMap, vUV).rgb;
    float metallic  = clamp(material.x * mr.b, 0.0, 1.0);
    float roughness = clamp(material.y * mr.g, 0.04, 1.0);  // floor avoids a zero-width highlight
    float ao = texture(uAoMap, vUV).r;

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

    // --- Ambient: image-based lighting (Step 15) ------------------------------------
    // The environment (the baked skybox) supplies the ambient term now. ambient.w chooses
    // it at runtime: 1 → IBL, 0 → the old flat constant fill (a clean A/B, and the fallback
    // when no sky is baked). The branch tests a UNIFORM, so it's resolved the same way for
    // every fragment — safe for the cubemap's implicit-derivative sampling below.
    vec3 ambientLight;
    if (ambient.w > 0.5) {
        // The split-sum approximation, in two lookups:
        //  DIFFUSE — the irradiance map already integrated the whole hemisphere, so ONE
        //  sample by the surface normal gives the incoming diffuse light. Scale by albedo
        //  and kD, the fraction not reflected specularly and not absorbed by metal.
        //  SPECULAR — sample the roughness-blurred reflection (higher mip = rougher), then
        //  weight it by the BRDF LUT as F0·scale + bias (the environment-independent factor).
        vec3 F  = fresnelSchlickRoughness(nDotV, F0, roughness);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

        vec3 irradiance = texture(uIrradianceMap, N).rgb;
        vec3 diffuseIBL = irradiance * albedo;

        vec3 R           = reflect(-V, N);
        vec3 prefiltered = textureLod(uPrefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
        vec2 brdf        = texture(uBrdfLut, vec2(nDotV, roughness)).rg;
        vec3 specularIBL = prefiltered * (F0 * brdf.x + brdf.y);

        ambientLight = kD * diffuseIBL + specularIBL;
    } else {
        ambientLight = ambient.rgb * albedo;
    }

    // AO darkens the ambient in creases the direct lights can't reach; direct light (Lo) is
    // left untouched, since real occlusion of direct light comes from the shadow map.
    vec3 color = ambientLight * ao + Lo;

    // Emissive (Step 16): self-emitted light ADDED on top, so it stays bright regardless of
    // scene lighting. A white 1×1 fallback map + a zero factor means non-emissive materials
    // add nothing. Bright emissive pixels also cross the Step 10 bloom threshold and glow.
    color += texture(uEmissiveMap, vUV).rgb * emissive.rgb;

    // Alpha (Step 21): the material's opacity (material.w) times the albedo map's own
    // alpha. Opaque materials keep opacity 1 and their images are fully opaque, so this
    // is 1.0 — and the opaque pipeline has blending disabled anyway, so it's ignored.
    // Only the transparent pipeline (blend on, depth-write off) consumes this to
    // composite the surface OVER what's already in the HDR target.
    outColor = vec4(color, material.w * albedoSample.a);
}
