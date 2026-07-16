# /// script
# requires-python = ">=3.9"
# dependencies = ["pillow", "numpy", "pygltflib"]
# ///
"""Generate the Step 25 "multimesh" test model for Koi's glTF hierarchy loader.

Run with uv (which resolves the inline dependencies above automatically):

    uv run tools/gen_multimesh.py

It writes a single self-contained ../assets/multimesh.glb — a tiny scene deliberately
built to exercise EVERY new path in loadModelHierarchy (Step 25):

  * NODE HIERARCHY: a root "pivot" node with two children ("post" and "cap"). Their
    placements are relative to the pivot, so the whole thing moves/rotates as one.
  * NODE TRANSFORMS, both encodings: the pivot uses an explicit TRS rotation (~25° about
    Y); the "cap" uses a raw 4x4 MATRIX (translate + non-uniform scale), which forces the
    loader's Transform::fromMatrix decomposition path.
  * MULTI-PRIMITIVE / MULTI-MATERIAL: the "post" mesh has TWO primitives (a lower and an
    upper box) with two DIFFERENT materials — so the loader must fan one glTF mesh out
    into two child draw-nodes.
  * TEXTURE DEDUP: those two materials share the SAME checker image, so a correct import
    reports "1 unique texture" (the image is uploaded once, not twice). A third material
    (the metallic "cap") carries no texture at all.

The image is embedded in the .glb's binary blob (a bufferView), exercising the loader's
embedded-image decode. Geometry is plain boxes with per-face normals + UVs so the checker
texture and the lighting both read clearly.
"""

from __future__ import annotations

import io
import math
import pathlib

import numpy as np
import pygltflib
from PIL import Image as PilImage
from pygltflib import (
    GLTF2,
    Accessor,
    Attributes,
    Buffer,
    BufferView,
    GLTF2 as _GLTF2,  # noqa: F401  (kept explicit for readers scanning imports)
    Image as GltfImage,
    Material,
    Mesh,
    Node,
    PbrMetallicRoughness,
    Primitive,
    Sampler,
    Scene,
    Texture,
    TextureInfo,
)

ASSETS = pathlib.Path(__file__).resolve().parent.parent / "assets"
OUT = ASSETS / "multimesh.glb"


# ---------------------------------------------------------------------------
# Geometry: an axis-aligned box, 24 verts (per-face normals/UVs), 36 indices.
# Corners are wound counter-clockwise as seen from OUTSIDE — the glTF front-face
# convention — so the faces are visible with back-face culling on.
# ---------------------------------------------------------------------------
# Each face: outward normal + its 4 corner offsets (in ±1 units) in CCW order.
_FACES = [
    ((0, 0, 1),  [(-1, -1, 1),  (1, -1, 1),  (1, 1, 1),  (-1, 1, 1)]),   # +Z front
    ((0, 0, -1), [(1, -1, -1),  (-1, -1, -1),(-1, 1, -1), (1, 1, -1)]),  # -Z back
    ((-1, 0, 0), [(-1, -1, -1), (-1, -1, 1), (-1, 1, 1),  (-1, 1, -1)]), # -X left
    ((1, 0, 0),  [(1, -1, 1),   (1, -1, -1), (1, 1, -1),  (1, 1, 1)]),   # +X right
    ((0, 1, 0),  [(-1, 1, 1),   (1, 1, 1),   (1, 1, -1),  (-1, 1, -1)]), # +Y top
    ((0, -1, 0), [(-1, -1, -1), (1, -1, -1), (1, -1, 1),  (-1, -1, 1)]), # -Y bottom
]
_FACE_UVS = [(0, 0), (1, 0), (1, 1), (0, 1)]


def make_box(center, half):
    """Return (positions, normals, uvs, indices) for a box centred at `center`."""
    cx, cy, cz = center
    hx, hy, hz = half
    positions, normals, uvs, indices = [], [], [], []
    for normal, corners in _FACES:
        base = len(positions)
        for (ox, oy, oz), uv in zip(corners, _FACE_UVS):
            positions.append((cx + ox * hx, cy + oy * hy, cz + oz * hz))
            normals.append(normal)
            uvs.append(uv)
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, uvs, indices


# ---------------------------------------------------------------------------
# Binary blob builder: everything (vertex data, indices, and the PNG image)
# lives in ONE buffer; each bufferView is a slice into it, kept 4-byte aligned.
# ---------------------------------------------------------------------------
blob = bytearray()
buffer_views: list[BufferView] = []
accessors: list[Accessor] = []


def _pad4() -> None:
    while len(blob) % 4 != 0:
        blob.append(0)


def add_view(data: bytes, target=None) -> int:
    _pad4()
    offset = len(blob)
    blob.extend(data)
    buffer_views.append(BufferView(buffer=0, byteOffset=offset, byteLength=len(data), target=target))
    return len(buffer_views) - 1


def add_vec_accessor(values, ncomp: int, target: int, minmax: bool = False) -> int:
    arr = np.asarray(values, dtype=np.float32)
    view = add_view(arr.tobytes(), target=target)
    acc = Accessor(
        bufferView=view,
        componentType=pygltflib.FLOAT,
        count=len(arr),
        type={2: "VEC2", 3: "VEC3"}[ncomp],
    )
    if minmax:  # glTF requires POSITION accessors to carry min/max bounds.
        acc.min = arr.min(axis=0).tolist()
        acc.max = arr.max(axis=0).tolist()
    accessors.append(acc)
    return len(accessors) - 1


def add_index_accessor(indices) -> int:
    arr = np.asarray(indices, dtype=np.uint32)
    view = add_view(arr.tobytes(), target=pygltflib.ELEMENT_ARRAY_BUFFER)
    accessors.append(Accessor(
        bufferView=view,
        componentType=pygltflib.UNSIGNED_INT,
        count=len(arr),
        type="SCALAR",
    ))
    return len(accessors) - 1


def make_primitive(center, half, material_index: int) -> Primitive:
    """Upload one box's attributes+indices and return a glTF Primitive."""
    pos, nrm, uv, idx = make_box(center, half)
    attrs = Attributes(
        POSITION=add_vec_accessor(pos, 3, pygltflib.ARRAY_BUFFER, minmax=True),
        NORMAL=add_vec_accessor(nrm, 3, pygltflib.ARRAY_BUFFER),
        TEXCOORD_0=add_vec_accessor(uv, 2, pygltflib.ARRAY_BUFFER),
    )
    return Primitive(attributes=attrs, indices=add_index_accessor(idx), material=material_index)


def make_checker_png(size: int = 64, squares: int = 8) -> bytes:
    """A simple grey checkerboard, PNG-encoded — the shared base-colour texture."""
    tile = size // squares
    px = np.zeros((size, size, 4), dtype=np.uint8)
    for y in range(size):
        for x in range(size):
            light = ((x // tile) + (y // tile)) % 2 == 0
            v = 235 if light else 105
            px[y, x] = (v, v, v, 255)
    buf = io.BytesIO()
    PilImage.fromarray(px, "RGBA").save(buf, format="PNG")
    return buf.getvalue()


def trs_matrix_column_major(translate, scale):
    """A translate·scale matrix in glTF's column-major order (no rotation).

    Used for the "cap" node so the file carries a raw MATRIX transform, exercising the
    loader's decomposition path rather than explicit translation/rotation/scale.
    """
    tx, ty, tz = translate
    sx, sy, sz = scale
    return [
        sx, 0.0, 0.0, 0.0,   # column 0
        0.0, sy, 0.0, 0.0,   # column 1
        0.0, 0.0, sz, 0.0,   # column 2
        tx, ty, tz, 1.0,     # column 3 (translation)
    ]


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)

    # --- shared checker texture (embedded in the blob) ---------------------
    png = make_checker_png()
    img_view = add_view(png)  # no target: image data, not a vertex/index buffer
    images = [GltfImage(bufferView=img_view, mimeType="image/png")]
    samplers = [Sampler()]  # default filtering/wrap; the engine ignores samplers for now
    textures = [Texture(source=0, sampler=0)]

    # --- materials: two tinted+TEXTURED (sharing texture 0) + one metallic --
    checker = TextureInfo(index=0)
    materials = [
        Material(  # 0: reddish, textured — the post's lower box
            name="post_lower",
            pbrMetallicRoughness=PbrMetallicRoughness(
                baseColorTexture=checker, baseColorFactor=[0.85, 0.25, 0.20, 1.0],
                metallicFactor=0.0, roughnessFactor=0.75),
        ),
        Material(  # 1: greenish, textured — SAME image as material 0 (dedup)
            name="post_upper",
            pbrMetallicRoughness=PbrMetallicRoughness(
                baseColorTexture=checker, baseColorFactor=[0.30, 0.70, 0.35, 1.0],
                metallicFactor=0.0, roughnessFactor=0.55),
        ),
        Material(  # 2: blue metal, no texture — the cap
            name="cap_metal",
            pbrMetallicRoughness=PbrMetallicRoughness(
                baseColorFactor=[0.20, 0.35, 0.85, 1.0],
                metallicFactor=0.9, roughnessFactor=0.25),
        ),
    ]

    # --- meshes ------------------------------------------------------------
    # "post": ONE mesh with TWO primitives (materials 0 and 1) → the loader must
    # split it into two child draw-nodes.
    post_mesh = Mesh(name="post", primitives=[
        make_primitive(center=(0.0, 0.5, 0.0),  half=(0.5, 0.5, 0.5), material_index=0),
        make_primitive(center=(0.0, 1.5, 0.0),  half=(0.4, 0.5, 0.4), material_index=1),
    ])
    # "cap": a single wide, flat box (material 2).
    cap_mesh = Mesh(name="cap", primitives=[
        make_primitive(center=(0.0, 0.0, 0.0), half=(0.5, 0.5, 0.5), material_index=2),
    ])
    meshes = [post_mesh, cap_mesh]

    # --- nodes (the hierarchy) --------------------------------------------
    half_angle = math.radians(25.0) * 0.5  # a 25° turn about Y, as a quaternion
    pivot = Node(
        name="pivot",
        rotation=[0.0, math.sin(half_angle), 0.0, math.cos(half_angle)],  # xyzw
        children=[1, 2],
    )
    post = Node(name="post", translation=[0.0, 0.0, 0.0], mesh=0)
    cap = Node(  # raw MATRIX transform → decomposition path in the loader
        name="cap",
        matrix=trs_matrix_column_major(translate=(0.0, 2.35, 0.0), scale=(1.6, 0.35, 1.6)),
        mesh=1,
    )
    nodes = [pivot, post, cap]

    # --- assemble + save as .glb ------------------------------------------
    _pad4()
    gltf = GLTF2(
        scene=0,
        scenes=[Scene(nodes=[0])],
        nodes=nodes,
        meshes=meshes,
        materials=materials,
        textures=textures,
        images=images,
        samplers=samplers,
        accessors=accessors,
        bufferViews=buffer_views,
        buffers=[Buffer(byteLength=len(blob))],
    )
    gltf.set_binary_blob(bytes(blob))
    gltf.save(str(OUT))
    print(f"Wrote {OUT} "
          f"({len(nodes)} nodes, {len(meshes)} meshes, "
          f"{sum(len(m.primitives) for m in meshes)} primitives, "
          f"{len(materials)} materials, {len(textures)} texture).")


if __name__ == "__main__":
    main()
