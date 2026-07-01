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
//  A Material only DESCRIBES appearance; it owns no GPU state of its own beyond a
//  shared reference to a Texture (the *sampler* — HOW to read a texture — stays
//  renderer-owned and shared). Materials are themselves shared (many cubes can use
//  one), so nodes hold a std::shared_ptr<Material>.
//
//  Step 12 (PBR): the ad-hoc Blinn-Phong knobs (shininess/specStrength) are replaced
//  by the two parameters of the industry-standard **metallic-roughness** model:
//
//    * metallic  — is this surface a METAL (1) or a non-metal / "dielectric" (0)?
//      Metals tint their reflection with the albedo and have NO diffuse colour;
//      dielectrics (plastic, wood, stone) reflect a dim, white-ish 4% and keep a
//      diffuse albedo. Real surfaces are one or the other, so this is usually 0 or 1.
//    * roughness — how MICROSCOPICALLY rough the surface is (0 = mirror-smooth, tight
//      bright highlight; 1 = matte, highlight spread out and dim). This is the single
//      most useful, intuitive material dial.
//
//  These feed the Cook-Torrance BRDF in triangle.frag (mirrored, for tests, by the
//  pure helpers in renderer/Pbr.hpp). The albedo texture is unchanged.
//
//  Header-only: it's a plain data struct. We only store a shared_ptr<Texture>, so
//  a forward declaration is enough here — no need to pull in the SDL-heavy header.
// ============================================================================
#pragma once

#include <memory>

namespace koi {

class Texture;  // the albedo image; full definition in renderer/Texture.hpp

struct Material {
    std::shared_ptr<Texture> texture;             // the image sampled as the base color (albedo)
    float                    metallic  = 0.0f;    // 0 = dielectric (non-metal), 1 = metal
    float                    roughness = 0.5f;    // 0 = mirror-smooth, 1 = fully matte
};

}  // namespace koi
