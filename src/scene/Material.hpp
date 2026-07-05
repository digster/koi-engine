// ============================================================================
//  Material.hpp — how a surface LOOKS (separate from its shape and placement)
// ----------------------------------------------------------------------------
//  Until Step 8 every object shared one global texture and one hardcoded
//  shininess, so everything looked the same. A Material fixes that: it bundles
//  the surface's APPEARANCE — which texture to sample and how shiny it is — so
//  different nodes can look different. This completes a clean separation that's
//  been forming as the engine grew; a drawn object is now three orthogonal things:
//
//      shape      → Mesh       (the geometry)
//      placement  → Transform  (where/oriented/scaled, on the Node)
//      appearance → Material   (what it looks like — this file)
//
//  A Material only DESCRIBES appearance; it owns no GPU state of its own beyond
//  shared references to Textures (the *sampler* — HOW to read a texture — stays
//  renderer-owned and shared). Materials are themselves shared (many cubes can use
//  one), so nodes hold a std::shared_ptr<Material>.
//
//  Step 12 (PBR) reduced appearance to the two parameters of the industry-standard
//  **metallic-roughness** model. Step 13 lets those parameters — and the surface
//  normal — vary PER PIXEL by driving them from TEXTURE MAPS instead of a single
//  scalar for the whole surface:
//
//    * albedo      — the base colour image (as before).
//    * metalRough  — a packed metallic-roughness map, glTF convention: the GREEN
//                    channel is roughness, the BLUE channel is metallic. One texture
//                    carries both, so a rusted-then-polished surface can vary across
//                    a single face.
//    * normalMap   — a tangent-space NORMAL map: each texel encodes a surface-normal
//                    perturbation, adding fine bumps/grooves without extra geometry.
//                    Requires the per-vertex tangent (see Vertex.hpp / Tangents.hpp).
//    * ao          — an ambient-occlusion map (RED channel): darkens the ambient fill
//                    in creases the direct lights don't reach.
//    * emissive    — a light-EMITTING colour map (Step 16): surfaces that glow on their
//                    own (a helmet's lit panels, a screen). It's ADDED after shading, so
//                    it stays bright regardless of scene lights, and — since it can push
//                    a pixel past the bloom threshold — it's what finally gives the Step
//                    10 bloom pass a real in-scene light source. Scaled by emissiveFactor.
//
//  The `metallic`/`roughness` scalars survive as multiplicative FACTORS (glTF's
//  metallicFactor / roughnessFactor): the shader computes `factor * sampledChannel`.
//  emissiveFactor is glTF's emissiveFactor (× KHR_materials_emissive_strength when
//  present); it defaults to 0, so a material with no emissive is unchanged. Any map may
//  be null — the renderer then binds a neutral 1×1 fallback (white for metalRough/ao/
//  emissive, a flat normal for normalMap), which makes the math collapse back to the
//  scalar-only Step 12 behaviour (emissive contributes nothing at factor 0) with no
//  shader branching.
//
//  These feed the Cook-Torrance BRDF in triangle.frag (mirrored, for tests, by the
//  pure helpers in renderer/Pbr.hpp).
//
//  Step 21 (transparency) adds an ALPHA MODE. Until now every material was opaque:
//  the depth buffer alone resolved visibility, so draw order didn't matter. A
//  BLEND material is *translucent* — the renderer composites it OVER whatever is
//  already on screen using `src·α + dst·(1-α)` (the "over" operator), which is
//  order-dependent, so blended objects must be drawn back-to-front (see
//  RenderQueue::partitionByBlend). `opacity` is that α (glTF's baseColorFactor.a):
//  1 = solid, 0 = invisible; the shader multiplies it by the albedo map's own alpha.
//  Opaque materials leave both at their defaults and render exactly as before.
//
//  Header-only: it's a plain data struct. We only store shared_ptr<Texture>s, so a
//  forward declaration is enough here — no need to pull in the SDL-heavy header.
// ============================================================================
#pragma once

#include <memory>

namespace koi {

class Texture;  // the map images; full definition in renderer/Texture.hpp

// How a material's alpha is interpreted (a subset of glTF's alphaMode). OPAQUE
// ignores alpha entirely (the depth buffer resolves visibility); BLEND composites
// the surface translucently and so must be sorted back-to-front. glTF's third mode,
// MASK (alpha-tested cutout), is a separate later step and isn't modelled yet.
enum class AlphaMode { Opaque, Blend };

struct Material {
    // Field ORDER matters: positional aggregate inits like `Material{tex, 0, 0.85f}`
    // rely on albedo/metallic/roughness staying first, so the new maps go last.
    std::shared_ptr<Texture> albedo;              // base colour image
    float                    metallic  = 0.0f;    // factor: 0 = dielectric, 1 = metal
    float                    roughness = 0.5f;    // factor: 0 = mirror-smooth, 1 = matte
    std::shared_ptr<Texture> metalRough;          // optional: G = roughness, B = metallic
    std::shared_ptr<Texture> normalMap;           // optional: tangent-space normals
    std::shared_ptr<Texture> ao;                  // optional: R = ambient occlusion
    std::shared_ptr<Texture> emissive;            // optional: emissive colour map (sRGB)
    // Emissive strength/tint (glTF emissiveFactor). Default 0 ⇒ no emission, so existing
    // materials render exactly as before. Kept as three floats (not a Vec3) so this
    // header stays include-light — it only forward-declares its dependencies.
    float                    emissiveFactor[3] = {0.0f, 0.0f, 0.0f};

    // Step 21: transparency. These two go LAST so the positional aggregate inits used
    // elsewhere (e.g. Material{tex, 0, 0.85f}) keep working unchanged. Defaults make a
    // material fully opaque, so nothing that omits them changes.
    AlphaMode                alphaMode = AlphaMode::Opaque;  // Opaque ⇒ blended pass skipped
    float                    opacity   = 1.0f;               // α for BLEND (glTF baseColorFactor.a)
};

}  // namespace koi
