# /// script
# requires-python = ">=3.9"
# dependencies = ["pillow", "numpy"]
# ///
"""Generate the Step 14 skybox — a procedural DAY-SKY cubemap for Koi's demo scene.

Run with uv (which resolves the inline dependencies above automatically):

    uv run tools/gen_skybox.py

It writes SIX BMP faces into ../assets, one per side of a cube:

    sky_px.bmp  sky_nx.bmp   (+X, -X)
    sky_py.bmp  sky_ny.bmp   (+Y, -Y)
    sky_pz.bmp  sky_nz.bmp   (+Z, -Z)

WHY SIX FACES (and how they stay seamless)
------------------------------------------
A cubemap is sampled by a 3D *direction*, not a 2D coordinate: the GPU takes the
ray, finds which cube face it pierces, and reads a texel there. To author faces
that line up perfectly at their shared edges, we do the SAME thing in reverse. For
every texel we compute (sc, tc) in [-1, 1] and reconstruct the exact world-space
DIRECTION the GPU will later use to sample that texel — using the standard cube-map
face convention below — then evaluate one analytic sky function at that direction.
Because generation and sampling share the direction convention, neighbouring faces
agree along every edge automatically; there are no seams to hand-align.

Standard face → direction table (sc varies across columns, tc down rows):
    +X: ( +1, -tc, -sc)      -X: ( -1, -tc, +sc)
    +Y: (+sc,  +1, +tc)      -Y: (+sc,  -1, -tc)
    +Z: (+sc, -tc,  +1)      -Z: (-sc, -tc,  -1)

THE SKY ITSELF is a pure function of direction:
  * a vertical gradient (pale near the horizon → deeper blue at the zenith),
  * a muted ground tint below the horizon,
  * a sun disk + soft glow placed at the direction of the scene's sun. The engine's
    sun light travels along (-0.4, -1, -0.3), so the sun SITS at the opposite
    direction (+0.4, +1, +0.3) — sky highlight and scene shading therefore agree.

BMP is used because the engine's loader is SDL_LoadBMP only (no PNG yet); these are
treated as linear colour, consistent with how the engine reads textures today.
"""

from __future__ import annotations

import pathlib

import numpy as np
from PIL import Image

SIZE = 256  # texels per face edge

ASSETS = pathlib.Path(__file__).resolve().parent.parent / "assets"


def _normalize(v: np.ndarray) -> np.ndarray:
    """Normalize the last axis of an array of vectors (safe against zero length)."""
    length = np.sqrt(np.sum(v * v, axis=-1, keepdims=True))
    return v / np.maximum(length, 1e-8)


# The sun's DIRECTION IN THE SKY = opposite of the sun light's travel direction
# (DemoApp::setupLights uses sun.direction = (-0.4, -1.0, -0.3)).
SUN_DIR = _normalize(np.array([0.4, 1.0, 0.3], dtype=np.float64))

# Palette (linear-ish RGB in [0,1]). Tuned against the Step 10 post chain: the
# engine's BLOOM bright-pass treats luminance > 0.9 (after the shader's ×kSkyIntensity
# lift) as a glowing light source. So the sky body is kept deliberately BELOW that
# threshold — otherwise a pale horizon blooms and blows to white — while the SUN disk
# is kept near 1.0 so it (and only it) crosses the threshold and glows.
ZENITH = np.array([0.12, 0.28, 0.60])   # deep blue overhead
HORIZON = np.array([0.55, 0.68, 0.85])  # hazy band at eye level (kept sub-bloom)
GROUND = np.array([0.28, 0.26, 0.24])   # muted earth below the horizon
SUN_COL = np.array([1.00, 0.96, 0.86])  # warm white (the one thing meant to bloom)


def _smoothstep(e0: float, e1: float, x: np.ndarray) -> np.ndarray:
    """Hermite smoothstep: 0 below e0, 1 above e1, smooth in between."""
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def sky_color(dirs: np.ndarray) -> np.ndarray:
    """Evaluate the analytic day sky for an array of unit directions (…,3) → (…,3)."""
    y = dirs[..., 1]  # elevation: -1 at nadir, 0 at horizon, +1 at zenith

    # Sky dome gradient (above the horizon). The exponent pushes most of the pale
    # horizon colour down near y=0 and lets the blue build up toward the zenith.
    s = np.power(np.clip(y, 0.0, 1.0), 0.45)[..., np.newaxis]
    dome = HORIZON * (1.0 - s) + ZENITH * s

    # Ground (below the horizon), darkening gently toward the nadir.
    gfac = (0.55 + 0.45 * np.clip(1.0 + y, 0.0, 1.0))[..., np.newaxis]
    ground = GROUND * gfac

    # Blend ground → dome across a thin band at the horizon for a soft skyline.
    w = _smoothstep(-0.03, 0.03, y)[..., np.newaxis]
    base = ground * (1.0 - w) + dome * w

    # Sun: a soft-edged disk plus two glow lobes (tight core + broad halo). Kept
    # near 1.0 so that, once the shader multiplies the sky up into HDR, the disk
    # crosses the bloom threshold and blooms for free.
    cosang = np.clip(np.einsum("...i,i->...", dirs, SUN_DIR), -1.0, 1.0)
    disk = _smoothstep(np.cos(np.radians(2.6)), np.cos(np.radians(1.5)), cosang)
    glow = 0.45 * np.power(np.clip(cosang, 0.0, 1.0), 220.0) \
        + 0.14 * np.power(np.clip(cosang, 0.0, 1.0), 12.0)
    above = _smoothstep(-0.02, 0.04, y)  # never draw the sun below the horizon
    sun = SUN_COL * ((disk + glow) * above)[..., np.newaxis]

    return np.clip(base + sun, 0.0, 1.0)


# Each face maps its (SC, TC) grids to a direction per the standard table above.
def _face_dirs(name: str, SC: np.ndarray, TC: np.ndarray) -> np.ndarray:
    one = np.ones_like(SC)
    table = {
        "px": (one, -TC, -SC),
        "nx": (-one, -TC, SC),
        "py": (SC, one, TC),
        "ny": (SC, -one, -TC),
        "pz": (SC, -TC, one),
        "nz": (-SC, -TC, -one),
    }
    x, y, z = table[name]
    return _normalize(np.stack([x, y, z], axis=-1))


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)

    # Texel centres in [-1, 1]: column → sc, row → tc (row 0 is the top, tc=-1).
    axis = (np.arange(SIZE) + 0.5) / SIZE * 2.0 - 1.0
    SC, TC = np.meshgrid(axis, axis)  # SC[row,col]=sc[col], TC[row,col]=tc[row]

    for name in ("px", "nx", "py", "ny", "pz", "nz"):
        dirs = _face_dirs(name, SC, TC)
        rgb = sky_color(dirs)
        img = Image.fromarray((rgb * 255.0).round().astype(np.uint8), mode="RGB")
        path = ASSETS / f"sky_{name}.bmp"
        img.save(path, format="BMP")
        print(f"wrote {path} ({img.size[0]}x{img.size[1]})")


if __name__ == "__main__":
    main()
