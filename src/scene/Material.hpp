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
//  Header-only: it's a plain data struct. We only store a shared_ptr<Texture>, so
//  a forward declaration is enough here — no need to pull in the SDL-heavy header.
// ============================================================================
#pragma once

#include <memory>

namespace koi {

class Texture;  // the albedo image; full definition in renderer/Texture.hpp

struct Material {
    std::shared_ptr<Texture> texture;        // the image sampled as the base color
    float                    shininess    = 32.0f;  // specular exponent — higher = tighter, glossier highlight
    float                    specStrength = 0.4f;   // how bright the specular highlight is
};

}  // namespace koi
