# /// script
# requires-python = ">=3.9"
# dependencies = ["pillow", "numpy"]
# ///
"""Generate the Step 13 texture maps for Koi's demo scene.

Run with uv (which resolves the inline dependencies above automatically):

    uv run tools/gen_textures.py

It writes three tiling 256x256 BMPs into ../assets, forming one coherent "tiled
panel" material so the same UVs drive every map:

  * tiles_normal.bmp  — a tangent-space NORMAL map. The tiles are flat plateaus with
                        beveled grooves at their borders; the encoded normals tilt
                        along the bevels, so raking light reveals real-looking relief
                        on otherwise flat geometry.
  * tiles_mr.bmp      — a packed metallic-roughness map (glTF convention: G = roughness,
                        B = metallic). Tile faces are smoother than the grooves, and the
                        tiles alternate metal/dielectric in a checkerboard — so a single
                        flat surface shows per-pixel variation in BOTH channels.
  * tiles_ao.bmp      — an ambient-occlusion map (grayscale); the grooves are darker,
                        as creases that bounced/ambient light struggles to reach.
  * tiles_albedo.bmp  — a plain base colour (warm light-grey plateaus, darker grooves).
                        Used where we want the relief / metallic checker to read clearly
                        rather than compete with a busy pattern.

BMP is used because the engine's loader is SDL_LoadBMP only (no PNG yet). These are
LINEAR data maps (not colour), which is consistent with the engine reading all
textures as linear today. Pillow writes a standard 24-bit BMP that SDL_LoadBMP reads.
"""

from __future__ import annotations

import pathlib

import numpy as np
from PIL import Image

SIZE = 256          # texture is SIZE x SIZE
CELL = 128          # one tile is CELL x CELL px (so 2x2 tiles, tiling seamlessly) —
                    # coarse enough that the relief + metal checker read clearly even
                    # when the floor tiles the texture several times.
BEVEL = 14          # width (px) of the sloped groove border around each tile
BUMP_STRENGTH = 6.0  # how strongly the bevels tilt the normal (higher = deeper relief)

ASSETS = pathlib.Path(__file__).resolve().parent.parent / "assets"


def height_field() -> np.ndarray:
    """A [0,1] height per texel: 1.0 on tile plateaus, ramping to 0.0 in the grooves.

    The height depends only on the distance to the nearest tile border (in x and y),
    computed with the tile period CELL — so the pattern repeats every CELL px and the
    image tiles seamlessly.
    """
    coords = np.arange(SIZE)
    # Distance from each coordinate to the nearest tile boundary along that axis.
    within = np.minimum(coords % CELL, (CELL - coords % CELL) % CELL)
    # Ramp 0..1 across the bevel; flat (1) once we're BEVEL px inside the tile.
    ramp = np.clip(within / BEVEL, 0.0, 1.0)
    # smoothstep for a rounded bevel rather than a hard crease.
    ramp = ramp * ramp * (3.0 - 2.0 * ramp)
    hx = ramp[np.newaxis, :]          # varies along x
    hy = ramp[:, np.newaxis]          # varies along y
    return np.minimum(hx, hy)         # grooves along both axes; plateau in the middle


def encode_normal_map(height: np.ndarray) -> Image.Image:
    """Convert a height field into a tangent-space normal map (OpenGL green-up)."""
    # Central differences with wraparound (np.roll) keep the map seamless. dh/dx and
    # dh/dy give the surface slope; the tangent-space normal is (-dx, -dy, 1/strength)
    # normalized, then packed from [-1,1] into [0,1] byte range.
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    nx = -dx * BUMP_STRENGTH
    ny = -dy * BUMP_STRENGTH
    nz = np.ones_like(height)
    inv_len = 1.0 / np.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny, nz = nx * inv_len, ny * inv_len, nz * inv_len
    rgb = np.stack([nx, ny, nz], axis=-1) * 0.5 + 0.5   # [-1,1] -> [0,1]
    return Image.fromarray((rgb * 255.0).round().astype(np.uint8), mode="RGB")


def encode_metal_rough(height: np.ndarray) -> Image.Image:
    """Packed metallic-roughness map: R unused, G = roughness, B = metallic."""
    coords = np.arange(SIZE)
    tile_x = (coords // CELL)[np.newaxis, :]
    tile_y = (coords // CELL)[:, np.newaxis]
    # Checkerboard of tiles: even tiles are metal, odd are dielectric — an obvious
    # per-pixel metallic split within one flat face.
    metallic = np.where(((tile_x + tile_y) % 2) == 0, 1.0, 0.0)
    metallic = np.broadcast_to(metallic, (SIZE, SIZE))
    # Grooves (low height) are rougher than the polished tile faces.
    roughness = 0.25 + (1.0 - height) * 0.65
    zero = np.zeros((SIZE, SIZE))
    rgb = np.stack([zero, roughness, metallic], axis=-1)
    return Image.fromarray((rgb * 255.0).round().astype(np.uint8), mode="RGB")


def encode_ao(height: np.ndarray) -> Image.Image:
    """Ambient-occlusion map (grayscale): darker in the grooves."""
    ao = 0.2 + 0.8 * height          # grooves ~0.2, plateaus 1.0
    g = (ao * 255.0).round().astype(np.uint8)
    return Image.fromarray(np.stack([g, g, g], axis=-1), mode="RGB")


def encode_albedo(height: np.ndarray) -> Image.Image:
    """Plain base colour: warm light-grey tile plateaus, a shade darker in the grooves."""
    # Lerp a groove colour → a plateau colour by the height, so the base colour alone
    # already hints at the tiling without hiding the maps' effects.
    groove = np.array([120.0, 116.0, 110.0])
    plate = np.array([205.0, 200.0, 190.0])
    h = height[:, :, np.newaxis]
    rgb = groove * (1.0 - h) + plate * h
    return Image.fromarray(rgb.round().astype(np.uint8), mode="RGB")


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)
    h = height_field()
    outputs = {
        "tiles_normal.bmp": encode_normal_map(h),
        "tiles_mr.bmp": encode_metal_rough(h),
        "tiles_ao.bmp": encode_ao(h),
        "tiles_albedo.bmp": encode_albedo(h),
    }
    for name, img in outputs.items():
        path = ASSETS / name
        img.save(path, format="BMP")
        print(f"wrote {path} ({img.size[0]}x{img.size[1]})")


if __name__ == "__main__":
    main()
