// ModelLoader.cpp — the ONE translation unit that pulls in the third-party model
// loaders. It #defines their *_IMPLEMENTATION macros (so the function bodies are
// compiled here, exactly once) and is built with warnings OFF in CMake — we don't
// control that code, and it would otherwise drown our strict-warning build.
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

// stb_image's function bodies are compiled HERE (this is already the third-party TU,
// built with warnings off). It decodes the PNG/JPG bytes glTF stores for its textures
// (Step 16). GpuRenderer.cpp includes the same header WITHOUT this define, so it sees
// only the declarations and links against these bodies.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "renderer/ModelLoader.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Log.hpp"
#include "math/Vec.hpp"
#include "renderer/GpuRenderer.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/Tangents.hpp"
#include "renderer/Vertex.hpp"
#include "scene/Material.hpp"  // Step 16: glTF material import populates a koi::Material

namespace koi {
namespace {

// Build one koi::Vertex. Loaded models get a white vertex color (the shader does
// texture * color, so white = the material's texture shown unchanged). The tangent
// defaults to zero; it's either read from the file (glTF TANGENT) or filled by
// computeTangents() below.
Vertex makeVertex(float px, float py, float pz, float u, float v,
                  float nx, float ny, float nz,
                  float tx = 0.0f, float ty = 0.0f, float tz = 0.0f) {
    return Vertex{{px, py, pz}, {1.0f, 1.0f, 1.0f}, {u, v}, {nx, ny, nz}, {tx, ty, tz}};
}

// Derive a per-vertex TANGENT (needed to orient tangent-space normal maps, Step 13)
// from the mesh's positions + UVs when the file doesn't supply one. Mirrors
// computeSmoothNormals: accumulate each triangle's tangent onto its three vertices
// (so shared vertices average), then orthonormalize each against its normal. The pure
// math lives in renderer/Tangents.hpp (unit-tested headlessly).
void computeTangents(std::vector<Vertex>& verts, const std::vector<Uint32>& idx) {
    for (Vertex& vert : verts) { vert.tangent[0] = vert.tangent[1] = vert.tangent[2] = 0.0f; }
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const Vertex& A = verts[idx[i]];
        const Vertex& B = verts[idx[i + 1]];
        const Vertex& C = verts[idx[i + 2]];
        const Vec3 t = triangleTangent(
            {A.position[0], A.position[1], A.position[2]},
            {B.position[0], B.position[1], B.position[2]},
            {C.position[0], C.position[1], C.position[2]},
            {A.uv[0], A.uv[1]}, {B.uv[0], B.uv[1]}, {C.uv[0], C.uv[1]});
        for (Uint32 k : {idx[i], idx[i + 1], idx[i + 2]}) {
            verts[k].tangent[0] += t.x;
            verts[k].tangent[1] += t.y;
            verts[k].tangent[2] += t.z;
        }
    }
    for (Vertex& vert : verts) {
        const Vec3 tang = orthonormalizeTangent(
            {vert.tangent[0], vert.tangent[1], vert.tangent[2]},
            normalize(Vec3{vert.normal[0], vert.normal[1], vert.normal[2]}));
        vert.tangent[0] = tang.x;
        vert.tangent[1] = tang.y;
        vert.tangent[2] = tang.z;
    }
}

// Fill missing normals by averaging the surrounding face normals (smooth shading).
// Used when a model arrives without normals; cross(b-a, c-a) is area-weighted, so
// bigger triangles count more — a good cheap default.
void computeSmoothNormals(std::vector<Vertex>& verts, const std::vector<Uint32>& idx) {
    for (Vertex& vert : verts) { vert.normal[0] = vert.normal[1] = vert.normal[2] = 0.0f; }
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        Vertex& A = verts[idx[i]];
        Vertex& B = verts[idx[i + 1]];
        Vertex& C = verts[idx[i + 2]];
        const Vec3 a{A.position[0], A.position[1], A.position[2]};
        const Vec3 b{B.position[0], B.position[1], B.position[2]};
        const Vec3 c{C.position[0], C.position[1], C.position[2]};
        const Vec3 fn = cross(b - a, c - a);
        for (Uint32 k : {idx[i], idx[i + 1], idx[i + 2]}) {
            verts[k].normal[0] += fn.x;
            verts[k].normal[1] += fn.y;
            verts[k].normal[2] += fn.z;
        }
    }
    for (Vertex& vert : verts) {
        const Vec3 n = normalize(Vec3{vert.normal[0], vert.normal[1], vert.normal[2]});
        vert.normal[0] = n.x;
        vert.normal[1] = n.y;
        vert.normal[2] = n.z;
    }
}

LoadedModel loadObj(GpuRenderer& renderer, const char* path) {
    // tinyobjloader gives us flat attribute arrays plus faces that index position,
    // texcoord and normal SEPARATELY. The GPU wants ONE index per combined vertex,
    // so we de-duplicate each unique (v, vt, vn) triple into a single koi::Vertex.
    // We import geometry only — the .mtl material is left to the caller (material
    // stays null in the returned LoadedModel).
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;  // tinyobjloader v1.0.6 reports warnings + errors in one string
    const bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, path,
                                     /*mtl_basedir=*/nullptr, /*triangulate=*/true);
    if (!ok) {
        KOI_ERROR("loadModel: failed to load OBJ '%s': %s", path, err.c_str());
        return {};
    }
    if (!err.empty()) { KOI_WARN("loadModel('%s'): %s", path, err.c_str()); }

    const bool haveNormals = !attrib.normals.empty();
    std::vector<Vertex> vertices;
    std::vector<Uint32> indices;
    std::unordered_map<uint64_t, Uint32> seen;  // packed (v,vt,vn) -> our index

    for (const tinyobj::shape_t& shape : shapes) {
        for (const tinyobj::index_t& ix : shape.mesh.indices) {
            // Pack the three sub-indices into a key (+1 so an absent -1 becomes 0;
            // each fits in 21 bits, plenty for our models).
            const uint64_t key = (static_cast<uint64_t>(ix.vertex_index + 1) << 42) |
                                 (static_cast<uint64_t>(ix.texcoord_index + 1) << 21) |
                                 (static_cast<uint64_t>(ix.normal_index + 1));
            auto it = seen.find(key);
            if (it != seen.end()) {
                indices.push_back(it->second);
                continue;
            }
            const int vi = ix.vertex_index;
            float u = 0.0f, v = 0.0f;
            if (ix.texcoord_index >= 0) {
                u = attrib.texcoords[2 * static_cast<size_t>(ix.texcoord_index) + 0];
                // OBJ's V origin is bottom-left; our textures are top-left (like
                // Vulkan/Metal), so flip V.
                v = 1.0f - attrib.texcoords[2 * static_cast<size_t>(ix.texcoord_index) + 1];
            }
            float nx = 0.0f, ny = 0.0f, nz = 0.0f;
            if (ix.normal_index >= 0) {
                nx = attrib.normals[3 * static_cast<size_t>(ix.normal_index) + 0];
                ny = attrib.normals[3 * static_cast<size_t>(ix.normal_index) + 1];
                nz = attrib.normals[3 * static_cast<size_t>(ix.normal_index) + 2];
            }
            const auto idx32 = static_cast<Uint32>(vertices.size());
            vertices.push_back(makeVertex(
                attrib.vertices[3 * static_cast<size_t>(vi) + 0],
                attrib.vertices[3 * static_cast<size_t>(vi) + 1],
                attrib.vertices[3 * static_cast<size_t>(vi) + 2], u, v, nx, ny, nz));
            seen.emplace(key, idx32);
            indices.push_back(idx32);
        }
    }

    if (vertices.empty()) {
        KOI_ERROR("loadModel: OBJ '%s' produced no geometry.", path);
        return {};
    }
    if (!haveNormals) { computeSmoothNormals(vertices, indices); }
    // OBJ has no tangents, so always derive them from positions + UVs (after the
    // normals exist, since the tangent is orthonormalized against the normal).
    computeTangents(vertices, indices);

    KOI_INFO("Loaded model '%s' (%zu verts, %zu tris).", path, vertices.size(),
             indices.size() / 3);
    return LoadedModel{renderer.createMesh(vertices, indices), /*material=*/nullptr};
}

// --- glTF material import (Step 16) -----------------------------------------
// Decode one glTF image into a GPU Texture. A glTF image is either EMBEDDED in the
// binary blob (a buffer_view — the .glb case, decoded straight from memory) or an
// EXTERNAL file (a uri, resolved relative to the .gltf). `srgb` marks COLOUR images
// (base-colour, emissive) so the GPU un-gammas them to linear when sampled; DATA maps
// (metallic-roughness, normal, AO) stay linear.
std::shared_ptr<Texture> loadGltfImage(GpuRenderer& renderer, const cgltf_image* image,
                                       const std::string& gltfDir, bool srgb) {
    if (image == nullptr) { return nullptr; }

    if (image->buffer_view != nullptr) {
        // Embedded: cgltf_buffer_view_data hands us a pointer that already accounts for
        // the view's offset (and any extension override). Decode those bytes in place.
        const uint8_t* bytes = cgltf_buffer_view_data(image->buffer_view);
        if (bytes == nullptr) {
            KOI_WARN("loadGltfImage: image buffer not loaded; skipping a texture.");
            return nullptr;
        }
        int w = 0, h = 0, channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(
            bytes, static_cast<int>(image->buffer_view->size), &w, &h, &channels, /*desired=*/4);
        if (pixels == nullptr) {
            KOI_WARN("loadGltfImage: embedded image decode failed: %s", stbi_failure_reason());
            return nullptr;
        }
        std::shared_ptr<Texture> tex = renderer.createTextureFromRGBA(
            pixels, static_cast<Uint32>(w), static_cast<Uint32>(h), srgb);
        stbi_image_free(pixels);
        return tex;
    }

    if (image->uri != nullptr) {
        // base64 data: URIs are a documented deferral (the .glb verification asset uses
        // buffer_views, and real .gltf files ship external image files).
        if (std::strncmp(image->uri, "data:", 5) == 0) {
            KOI_WARN("loadGltfImage: data-URI images are not supported yet; skipping a texture.");
            return nullptr;
        }
        const std::string full = gltfDir + image->uri;  // resolve relative to the glTF
        return renderer.loadTexture(full.c_str(), srgb);
    }
    return nullptr;
}

// Import a glTF PBR material into a koi::Material: the base-colour, metallic-roughness,
// normal, occlusion and emissive maps + the scalar factors. Colour maps (base-colour,
// emissive) load as sRGB; data maps stay linear. `mat` may be null (a primitive without
// a material) — we still return a valid white material so downstream code (which always
// samples an albedo map) has something to bind.
std::shared_ptr<Material> loadGltfMaterial(GpuRenderer& renderer, const cgltf_material* mat,
                                           const std::string& gltfDir) {
    auto material = std::make_shared<Material>();

    // Base-colour factor (linear); tints the surface and, absent a base-colour texture,
    // becomes a 1×1 solid so the albedo slot is never empty. Defaults to opaque white.
    float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    if (mat != nullptr && mat->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness& pbr = mat->pbr_metallic_roughness;
        for (int i = 0; i < 4; ++i) { baseColor[i] = pbr.base_color_factor[i]; }
        if (pbr.base_color_texture.texture != nullptr) {
            material->albedo = loadGltfImage(renderer, pbr.base_color_texture.texture->image,
                                             gltfDir, /*srgb=*/true);
        }
        // The scalars are FACTORS the shader multiplies by the MR map's B/G channels
        // (a white fallback map leaves them unchanged) — exactly the Step 13 convention.
        material->metallic  = pbr.metallic_factor;
        material->roughness = pbr.roughness_factor;
        if (pbr.metallic_roughness_texture.texture != nullptr) {
            material->metalRough = loadGltfImage(
                renderer, pbr.metallic_roughness_texture.texture->image, gltfDir, /*srgb=*/false);
        }
    }

    if (mat != nullptr) {
        if (mat->normal_texture.texture != nullptr) {
            material->normalMap = loadGltfImage(renderer, mat->normal_texture.texture->image,
                                                gltfDir, /*srgb=*/false);
        }
        if (mat->occlusion_texture.texture != nullptr) {
            material->ao = loadGltfImage(renderer, mat->occlusion_texture.texture->image,
                                         gltfDir, /*srgb=*/false);
        }
        if (mat->emissive_texture.texture != nullptr) {
            material->emissive = loadGltfImage(renderer, mat->emissive_texture.texture->image,
                                               gltfDir, /*srgb=*/true);
        }
        // Emissive factor (× the KHR_materials_emissive_strength extension when present).
        // Left at the Material default of 0 when the file has no emission.
        const float strength = mat->has_emissive_strength
                                   ? mat->emissive_strength.emissive_strength : 1.0f;
        for (int i = 0; i < 3; ++i) {
            material->emissiveFactor[i] = mat->emissive_factor[i] * strength;
        }
    }

    // recordNode samples material->albedo unconditionally, so it must never be null.
    // With no base-colour texture, synthesize a 1×1 solid from the (linear) factor.
    if (!material->albedo) {
        const auto toByte = [](float v) -> Uint8 {
            const float c = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
            return static_cast<Uint8>(c * 255.0f + 0.5f);
        };
        const Uint8 texel[4] = {toByte(baseColor[0]), toByte(baseColor[1]),
                                toByte(baseColor[2]), toByte(baseColor[3])};
        material->albedo = renderer.createTextureFromRGBA(texel, 1, 1, /*srgb=*/false,
                                                          /*withMips=*/false);
    }
    return material;
}

LoadedModel loadGltf(GpuRenderer& renderer, const char* path) {
    // cgltf parses .glb/.gltf into accessors (typed views into binary buffers). We
    // read the first mesh's first primitive — glTF already stores ONE index per
    // combined vertex, so no de-duplication is needed.
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        KOI_ERROR("loadModel: cgltf failed to parse '%s'.", path);
        return {};
    }
    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
        KOI_ERROR("loadModel: cgltf failed to load buffers for '%s'.", path);
        cgltf_free(data);
        return {};
    }
    if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0) {
        KOI_ERROR("loadModel: glTF '%s' has no mesh primitive.", path);
        cgltf_free(data);
        return {};
    }

    const cgltf_primitive& prim = data->meshes[0].primitives[0];
    cgltf_accessor* posAcc = nullptr;
    cgltf_accessor* nrmAcc = nullptr;
    cgltf_accessor* uvAcc = nullptr;
    cgltf_accessor* tanAcc = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        const cgltf_attribute& a = prim.attributes[i];
        if (a.type == cgltf_attribute_type_position) { posAcc = a.data; }
        else if (a.type == cgltf_attribute_type_normal) { nrmAcc = a.data; }
        else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) { uvAcc = a.data; }
        else if (a.type == cgltf_attribute_type_tangent) { tanAcc = a.data; }
    }
    if (posAcc == nullptr) {
        KOI_ERROR("loadModel: glTF '%s' primitive has no POSITION.", path);
        cgltf_free(data);
        return {};
    }

    const size_t vcount = posAcc->count;
    std::vector<Vertex> vertices(vcount);
    for (size_t i = 0; i < vcount; ++i) {
        float p[3] = {0, 0, 0};
        cgltf_accessor_read_float(posAcc, i, p, 3);
        float n[3] = {0.0f, 1.0f, 0.0f};
        if (nrmAcc != nullptr) { cgltf_accessor_read_float(nrmAcc, i, n, 3); }
        float uv[2] = {0.0f, 0.0f};
        if (uvAcc != nullptr) { cgltf_accessor_read_float(uvAcc, i, uv, 2); }
        // glTF stores TANGENT as a vec4 (xyz + w handedness); we keep just the xyz
        // direction (see Tangents.hpp on the handedness simplification).
        float t[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (tanAcc != nullptr) { cgltf_accessor_read_float(tanAcc, i, t, 4); }
        // glTF UV origin is top-left already (matches our textures) — no V flip.
        vertices[i] = makeVertex(p[0], p[1], p[2], uv[0], uv[1], n[0], n[1], n[2],
                                 t[0], t[1], t[2]);
    }

    std::vector<Uint32> indices;
    if (prim.indices != nullptr) {
        indices.resize(prim.indices->count);
        for (size_t i = 0; i < prim.indices->count; ++i) {
            indices[i] = static_cast<Uint32>(cgltf_accessor_read_index(prim.indices, i));
        }
    } else {
        indices.resize(vcount);  // non-indexed: 0,1,2,...
        for (size_t i = 0; i < vcount; ++i) { indices[i] = static_cast<Uint32>(i); }
    }

    const bool haveNormals  = (nrmAcc != nullptr);
    const bool haveTangents = (tanAcc != nullptr);

    // Import the file's PBR material (Step 16) BEFORE freeing cgltf's data — the image
    // decode reads bytes straight out of the still-live glTF buffers. gltfDir lets an
    // external-texture .gltf resolve its image URIs relative to the model file.
    std::string gltfDir;
    { const std::string sp = path;
      const size_t slash = sp.find_last_of("/\\");
      gltfDir = (slash == std::string::npos) ? std::string() : sp.substr(0, slash + 1); }
    std::shared_ptr<Material> material = loadGltfMaterial(renderer, prim.material, gltfDir);

    cgltf_free(data);  // accessors read out + material imported; parsed data no longer needed

    if (!haveNormals) { computeSmoothNormals(vertices, indices); }
    // Derive tangents only when the file didn't ship them (they need valid normals to
    // orthonormalize against, so this runs after the normals are settled).
    if (!haveTangents) { computeTangents(vertices, indices); }

    KOI_INFO("Loaded model '%s' (%zu verts, %zu tris).", path, vertices.size(),
             indices.size() / 3);
    return LoadedModel{renderer.createMesh(vertices, indices), material};
}

bool endsWith(const std::string& s, const char* ext) {
    const size_t n = std::strlen(ext);
    return s.size() >= n && s.compare(s.size() - n, n, ext) == 0;
}

}  // namespace

LoadedModel loadModel(GpuRenderer& renderer, const char* path) {
    const std::string p = path;
    if (endsWith(p, ".obj")) { return loadObj(renderer, path); }
    if (endsWith(p, ".glb") || endsWith(p, ".gltf")) { return loadGltf(renderer, path); }
    KOI_ERROR("loadModel: unsupported model extension for '%s' (want .obj/.glb/.gltf).", path);
    return {};
}

}  // namespace koi
