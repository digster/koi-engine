// ModelLoader.cpp — the ONE translation unit that pulls in the third-party model
// loaders. It #defines their *_IMPLEMENTATION macros (so the function bodies are
// compiled here, exactly once) and is built with warnings OFF in CMake — we don't
// control that code, and it would otherwise drown our strict-warning build.
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

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
#include "renderer/Vertex.hpp"

namespace koi {
namespace {

// Build one koi::Vertex. Loaded models get a white vertex color (the shader does
// texture * color, so white = the material's texture shown unchanged).
Vertex makeVertex(float px, float py, float pz, float u, float v,
                  float nx, float ny, float nz) {
    return Vertex{{px, py, pz}, {1.0f, 1.0f, 1.0f}, {u, v}, {nx, ny, nz}};
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

std::shared_ptr<Mesh> loadObj(GpuRenderer& renderer, const char* path) {
    // tinyobjloader gives us flat attribute arrays plus faces that index position,
    // texcoord and normal SEPARATELY. The GPU wants ONE index per combined vertex,
    // so we de-duplicate each unique (v, vt, vn) triple into a single koi::Vertex.
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;  // tinyobjloader v1.0.6 reports warnings + errors in one string
    const bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, path,
                                     /*mtl_basedir=*/nullptr, /*triangulate=*/true);
    if (!ok) {
        KOI_ERROR("loadModel: failed to load OBJ '%s': %s", path, err.c_str());
        return nullptr;
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
        return nullptr;
    }
    if (!haveNormals) { computeSmoothNormals(vertices, indices); }

    KOI_INFO("Loaded model '%s' (%zu verts, %zu tris).", path, vertices.size(),
             indices.size() / 3);
    return renderer.createMesh(vertices, indices);
}

std::shared_ptr<Mesh> loadGltf(GpuRenderer& renderer, const char* path) {
    // cgltf parses .glb/.gltf into accessors (typed views into binary buffers). We
    // read the first mesh's first primitive — glTF already stores ONE index per
    // combined vertex, so no de-duplication is needed.
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        KOI_ERROR("loadModel: cgltf failed to parse '%s'.", path);
        return nullptr;
    }
    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
        KOI_ERROR("loadModel: cgltf failed to load buffers for '%s'.", path);
        cgltf_free(data);
        return nullptr;
    }
    if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0) {
        KOI_ERROR("loadModel: glTF '%s' has no mesh primitive.", path);
        cgltf_free(data);
        return nullptr;
    }

    const cgltf_primitive& prim = data->meshes[0].primitives[0];
    cgltf_accessor* posAcc = nullptr;
    cgltf_accessor* nrmAcc = nullptr;
    cgltf_accessor* uvAcc = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        const cgltf_attribute& a = prim.attributes[i];
        if (a.type == cgltf_attribute_type_position) { posAcc = a.data; }
        else if (a.type == cgltf_attribute_type_normal) { nrmAcc = a.data; }
        else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) { uvAcc = a.data; }
    }
    if (posAcc == nullptr) {
        KOI_ERROR("loadModel: glTF '%s' primitive has no POSITION.", path);
        cgltf_free(data);
        return nullptr;
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
        // glTF UV origin is top-left already (matches our textures) — no V flip.
        vertices[i] = makeVertex(p[0], p[1], p[2], uv[0], uv[1], n[0], n[1], n[2]);
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

    const bool haveNormals = (nrmAcc != nullptr);
    cgltf_free(data);  // the accessors are read out; the parsed data is no longer needed

    if (!haveNormals) { computeSmoothNormals(vertices, indices); }

    KOI_INFO("Loaded model '%s' (%zu verts, %zu tris).", path, vertices.size(),
             indices.size() / 3);
    return renderer.createMesh(vertices, indices);
}

bool endsWith(const std::string& s, const char* ext) {
    const size_t n = std::strlen(ext);
    return s.size() >= n && s.compare(s.size() - n, n, ext) == 0;
}

}  // namespace

std::shared_ptr<Mesh> loadModel(GpuRenderer& renderer, const char* path) {
    const std::string p = path;
    if (endsWith(p, ".obj")) { return loadObj(renderer, path); }
    if (endsWith(p, ".glb") || endsWith(p, ".gltf")) { return loadGltf(renderer, path); }
    KOI_ERROR("loadModel: unsupported model extension for '%s' (want .obj/.glb/.gltf).", path);
    return nullptr;
}

}  // namespace koi
