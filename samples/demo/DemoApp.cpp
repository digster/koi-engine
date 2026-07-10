#include "DemoApp.hpp"

#include <SDL3/SDL.h>

#include <algorithm>  // std::max — size the HUD panel to its widest line
#include <cmath>   // std::cos, std::sin — cone cosines + the orbiting light
#include <cstdio>  // std::snprintf — format the HUD's live readouts
#include <memory>
#include <string>

#include "core/Engine.hpp"
#include "core/Log.hpp"
#include "math/Mat4.hpp"             // perspective/radians for the planted capture frustum
#include "renderer/GpuRenderer.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/ModelLoader.hpp"
#include "renderer/Primitives.hpp"
#include "scene/Material.hpp"
#include "scene/Node.hpp"

namespace koi {

namespace {
// The colour the screen is cleared to for pixels the scene doesn't cover. A calm
// dark teal — distinctive enough that you can tell the GPU clear actually ran.
constexpr SDL_FColor kClearColor = {0.04f, 0.10f, 0.12f, 1.0f};

// Debug-overlay colours (Step 22). Bright + saturated so they read clearly even
// after the HDR scene is tone-mapped (the lines are drawn into the HDR target).
constexpr Vec3 kBoundsColor  = {0.15f, 1.0f, 0.30f};  // green AABBs
constexpr Vec3 kFrustumColor = {1.0f, 0.80f, 0.10f};  // amber camera frustum

// HUD colours (Step 23). The panel is translucent black (alpha < 1) so the scene
// shows through behind the text; the text is a near-white for legibility on it.
constexpr Vec4 kHudPanel = {0.0f, 0.0f, 0.0f, 0.45f};
constexpr Vec4 kHudText  = {0.92f, 0.96f, 1.0f, 1.0f};

// Recursively add a green AABB for every drawable node in the subtree. A node's
// world-space box is its mesh's model-space bounds (cached at upload, Step 20)
// pushed through the node's world matrix (Aabb::transformed, Step 19).
void addNodeBounds(DebugDraw& out, const Node& node, const Vec3& color) {
    if (node.mesh() != nullptr) {
        out.box(node.mesh()->localBounds().transformed(node.worldMatrix()), color);
    }
    for (const auto& child : node.children()) {
        addNodeBounds(out, *child, color);
    }
}
}  // namespace

// Out-of-line so the unique_ptr<Node> member sees the complete Node type (from
// scene/Node.hpp, included above) where it is constructed/destroyed.
DemoApp::DemoApp()  = default;
DemoApp::~DemoApp() = default;

bool DemoApp::onStart(Engine& engine) {
    // Build the world through the engine's renderer — the same surface any client
    // app would use. Then place the lights, and pose the scene (world transforms)
    // so a KOI_CAPTURE taken right after onStart renders the deterministic t=0 frame.
    if (!buildScene(engine.renderer())) {
        return false;
    }
    setupLights();
    sceneRoot_->updateWorldTransforms();

    // Step 22: with KOI_DEBUG_DRAW set, turn the debug overlays on from the start so
    // a single headless KOI_CAPTURE frame shows them (no key presses possible there).
    // The frozen frustum is a SHORT-far (z→6) copy of the camera's own frustum, so it
    // reads as a compact amber wireframe sitting in the scene rather than the full
    // 100-unit pyramid. Interactive runs leave the overlays off until you press G/F/L.
    if (SDL_getenv("KOI_DEBUG_DRAW") != nullptr) {
        debugBounds_ = debugLights_ = debugFrustum_ = true;
        frozenViewProj_ = perspective(radians(60.0f), 1280.0f / 720.0f, 0.1f, 6.0f)
                          * camera_.viewMatrix();
        haveFrozenFrustum_ = true;
        KOI_INFO("KOI_DEBUG_DRAW set — debug overlays enabled for this run.");
    }

    // Step 23: with KOI_HUD set, show the HUD from the start so a headless KOI_CAPTURE
    // frame includes it (no key presses possible there). Interactive runs start with
    // the HUD hidden until you press H.
    if (SDL_getenv("KOI_HUD") != nullptr) {
        showHud_ = true;
        KOI_INFO("KOI_HUD set — HUD enabled for this run.");
    }

    // Build the overlays once here too (not only in onUpdate): the headless
    // KOI_CAPTURE path renders straight after onStart WITHOUT an onUpdate tick, so
    // this is what makes the debug lines + HUD appear in a captured t=0 frame. (Empty
    // and harmless when their toggles are off.)
    buildDebugLines();
    buildHud();

    KOI_INFO("Controls: WASD move, E/Q up/down, mouse look, Esc to quit.");
    KOI_INFO("Post-processing: 1=tone-map 2=bloom 3=FXAA 4=vignette, [ / ] exposure.");
    KOI_INFO("Lights: 5=point lights 6=spot 7=sun. Environment: 8=skybox 9=IBL.");
    KOI_INFO("Culling/transparency: 0=frustum culling, T=back-to-front sort.");
    KOI_INFO("Debug draw: G=bounds L=light icons F=freeze+show the camera frustum.");
    KOI_INFO("HUD: H=toggle the on-screen text overlay (FPS, camera, toggles).");
    return true;
}

bool DemoApp::buildScene(GpuRenderer& renderer) {
    // Two shared meshes, uploaded to the GPU once and reused by every node that
    // wants them. The cube is the RGB color cube from earlier steps; the plane is
    // a flat ground. shared_ptr lets many nodes point at the same geometry.
    std::shared_ptr<Mesh> cube  = makeCubeMesh(renderer);
    std::shared_ptr<Mesh> plane = makePlaneMesh(renderer);
    if (!cube || !plane) {
        KOI_ERROR("buildScene: failed to create one or more meshes.");
        return false;
    }

    // Albedo textures, resolved relative to the executable (same convention as the
    // shaders). They become the base-colour images of the materials below.
    const std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    auto checkerTex = renderer.loadTexture((base + "assets/uv_grid.bmp").c_str());
    auto dotsTex    = renderer.loadTexture((base + "assets/dots.bmp").c_str());
    if (!checkerTex || !dotsTex) {
        KOI_ERROR("buildScene: failed to load one or more textures.");
        return false;
    }

    // Step 13: the "tiled panel" map set (generated by tools/gen_textures.py) — a
    // normal map (beveled tile grooves), a packed metallic-roughness map (grooves
    // rougher; tiles alternate metal/dielectric), and an AO map (dark grooves). We
    // layer these over the existing albedo images to drive the surface PER PIXEL.
    auto tileNormal = renderer.loadTexture((base + "assets/tiles_normal.bmp").c_str());
    auto tileMr     = renderer.loadTexture((base + "assets/tiles_mr.bmp").c_str());
    auto tileAo     = renderer.loadTexture((base + "assets/tiles_ao.bmp").c_str());
    auto tileAlbedo = renderer.loadTexture((base + "assets/tiles_albedo.bmp").c_str());
    if (!tileNormal || !tileMr || !tileAo || !tileAlbedo) {
        KOI_ERROR("buildScene: failed to load one or more texture maps.");
        return false;
    }

    // Step 14: the environment SKYBOX. Six procedural day-sky faces (generated by
    // tools/gen_skybox.py) are uploaded into one cube texture the renderer owns and
    // draws behind the scene. Order is SDL's cube layer order: +X,-X,+Y,-Y,+Z,-Z.
    // The sky is optional — if it fails to load we log and carry on (the scene still
    // renders against the flat clear colour).
    if (!renderer.loadCubemap({base + "assets/sky_px.bmp", base + "assets/sky_nx.bmp",
                               base + "assets/sky_py.bmp", base + "assets/sky_ny.bmp",
                               base + "assets/sky_pz.bmp", base + "assets/sky_nz.bmp"})) {
        KOI_WARN("buildScene: skybox cubemap failed to load — continuing without a sky.");
    }

    // Materials now vary appearance PER OBJECT and, where maps are attached, PER PIXEL.
    // Designated initializers keep the six fields readable (and silence
    // -Wmissing-field-initializers). A material with no maps falls back to the neutral
    // 1x1 textures in the renderer, rendering exactly as it did in Step 12:
    //   * floorMat — the showcase for RELIEF: the tile normal map gives the big flat
    //                plane real bumps under the raking sun, the AO map darkens the
    //                grooves, and the MR map varies roughness per pixel. Kept a
    //                DIELECTRIC (metallic factor 0) so the large surface isn't a dark
    //                metal (metals need IBL to look right — a later step).
    //   * hubMat   — the showcase for PER-PIXEL METALLIC + RELIEF: the full map set
    //                over a PLAIN tile albedo (so nothing competes with the effects),
    //                metallic factor 1, so the MR map's metal/dielectric checkerboard
    //                and the normal map's beveled tiles both show on the cube's faces.
    //   * cubeMat  — map-less (the fallback path): a plain mid-roughness dielectric,
    //                proving map-less materials are unchanged from Step 12.
    auto floorMat = std::make_shared<Material>(Material{
        .albedo = checkerTex, .metallic = 0.0f, .roughness = 1.0f,
        .metalRough = tileMr, .normalMap = tileNormal, .ao = tileAo});
    auto cubeMat  = std::make_shared<Material>(Material{
        .albedo = dotsTex, .metallic = 0.0f, .roughness = 0.55f});
    auto hubMat   = std::make_shared<Material>(Material{
        .albedo = tileAlbedo, .metallic = 1.0f, .roughness = 1.0f,
        .metalRough = tileMr, .normalMap = tileNormal, .ao = tileAo});

    // Step 9: load two MODELS from files. sphere.glb exercises the cgltf loader; torus.obj
    // the tinyobjloader one. We take only their geometry (.mesh) and give them our OWN
    // showcase materials below — unlike the Damaged Helmet further down, which keeps the
    // material imported from its file (Step 16).
    LoadedModel sphereModel = loadModel(renderer, (base + "assets/sphere.glb").c_str());
    LoadedModel torusModel  = loadModel(renderer, (base + "assets/torus.obj").c_str());
    std::shared_ptr<Mesh> sphere = sphereModel.mesh;
    std::shared_ptr<Mesh> torus  = torusModel.mesh;
    if (!sphere || !torus) {
        KOI_ERROR("buildScene: failed to load one or more models.");
        return false;
    }
    // The loaded models flank the cubes and headline the two material extremes:
    //   * sphereMat — a smooth METAL (the checker as its reflected tint): the Step 15
    //                 SHOWCASE. Before IBL a metal was near-black away from the highlight;
    //                 now it mirrors the sky. Roughness is low (0.08) so that reflection is
    //                 crisp — toggle IBL with '9' to watch it go dark again.
    //   * torusMat  — a glossy DIELECTRIC: a tight white specular over a matte body.
    auto sphereMat = std::make_shared<Material>(Material{
        .albedo = checkerTex, .metallic = 1.0f, .roughness = 0.08f});
    auto torusMat  = std::make_shared<Material>(Material{
        .albedo = dotsTex, .metallic = 0.0f, .roughness = 0.25f});

    // The root is a pure GROUP node (no mesh/material): a stable parent for the world.
    sceneRoot_ = std::make_unique<Node>();

    // A ground plane (checker, matte), dropped a couple of units below the cubes so
    // they sit above it. (Its own local transform places it; the mesh is centered.)
    auto ground = std::make_unique<Node>(plane, floorMat);
    ground->transform().position = {0.0f, -2.0f, 0.0f};
    sceneRoot_->addChild(std::move(ground));

    // The HUB: a cube at the origin that spins about Y (animated in onUpdate), with the
    // extra-shiny material. Every node below is placed in the hub's space, so they
    // orbit it as it turns — the whole point of a scene graph.
    hub_ = sceneRoot_->addChild(std::make_unique<Node>(cube, hubMat));

    // Two small satellites (glossy dots) at fixed offsets in the hub's space. As the
    // hub spins, they sweep around it, though their OWN transforms never change.
    auto satA = std::make_unique<Node>(cube, cubeMat);
    satA->transform().position = {2.5f, 0.0f, 0.0f};
    satA->transform().scale    = {0.5f, 0.5f, 0.5f};
    hub_->addChild(std::move(satA));

    auto satC = std::make_unique<Node>(cube, cubeMat);
    satC->transform().position = {0.0f, 0.0f, 2.5f};
    satC->transform().scale    = {0.5f, 0.5f, 0.5f};
    hub_->addChild(std::move(satC));

    // A third satellite that ALSO spins (the "spinner"/pivot), carrying a
    // grandchild "moon" offset from it. Result: the moon orbits the spinner
    // (because the spinner turns) WHILE the spinner orbits the hub (because the
    // hub turns) — two levels of inherited motion, with no special code.
    auto pivot = std::make_unique<Node>(cube, cubeMat);
    pivot->transform().position = {-2.5f, 0.0f, 0.0f};
    pivot->transform().scale    = {0.6f, 0.6f, 0.6f};
    spinner_ = hub_->addChild(std::move(pivot));

    auto moon = std::make_unique<Node>(cube, cubeMat);
    moon->transform().position = {2.0f, 0.0f, 0.0f};
    moon->transform().scale    = {0.6f, 0.6f, 0.6f};
    spinner_->addChild(std::move(moon));

    // The two loaded models, flanking the cube hierarchy and resting on the floor.
    auto sphereNode = std::make_unique<Node>(sphere, sphereMat);
    sphereNode->transform().position = {-3.8f, -0.8f, 0.0f};
    sphereNode->transform().scale    = {1.2f, 1.2f, 1.2f};
    sceneRoot_->addChild(std::move(sphereNode));

    auto torusNode = std::make_unique<Node>(torus, torusMat);
    torusNode->transform().position      = {3.8f, -0.3f, 0.0f};
    torusNode->transform().scale         = {1.2f, 1.2f, 1.2f};
    torusNode->transform().rotation = Quat::fromEuler({radians(90.0f), 0.0f, 0.0f});  // stand the donut up
    sceneRoot_->addChild(std::move(torusNode));

    // Step 21: two translucent "glass" panes between the camera and the rest of the
    // scene, so the whole world is visible THROUGH them. Each is the flat plane mesh
    // stood upright (rotate +90° about X so it faces the camera) and shrunk to a small
    // pane. Their albedo is a 1×1 solid colour (a clean uniform tint — the plane's
    // vertex colour is uniform, unlike the RGB-cube), and their material is AlphaMode::
    // Blend with opacity 0.4, so the transparent pass composites them over the scene.
    // They OVERLAP on screen at DIFFERENT depths, which is the whole point: the nearer
    // pane must blend on top of the farther one (press 'T' to disable the back-to-front
    // sort and watch that ordering break). Parented to the root (not the spinning hub)
    // so the deterministic t=0 capture is stable.
    const Uint8 bluePixels[4]  = {40, 120, 255, 255};   // cool blue glass
    const Uint8 amberPixels[4] = {255, 150, 40, 255};   // warm amber glass
    auto blueTex  = renderer.createTextureFromRGBA(bluePixels,  1, 1, /*srgb=*/true, /*withMips=*/false);
    auto amberTex = renderer.createTextureFromRGBA(amberPixels, 1, 1, /*srgb=*/true, /*withMips=*/false);
    if (blueTex && amberTex) {
        auto blueGlassMat = std::make_shared<Material>(Material{
            .albedo = blueTex, .metallic = 0.0f, .roughness = 0.1f,
            .alphaMode = AlphaMode::Blend, .opacity = 0.4f});
        auto amberGlassMat = std::make_shared<Material>(Material{
            .albedo = amberTex, .metallic = 0.0f, .roughness = 0.1f,
            .alphaMode = AlphaMode::Blend, .opacity = 0.4f});

        // Amber sits FARTHER from the camera (smaller z); blue sits NEARER. With sorting
        // on, amber draws first and blue composites over it where they overlap.
        auto amberPane = std::make_unique<Node>(plane, amberGlassMat);
        amberPane->transform().position = {0.5f, 0.8f, 5.6f};
        amberPane->transform().rotation = Quat::fromEuler({radians(90.0f), 0.0f, 0.0f});
        amberPane->transform().scale    = {0.14f, 0.14f, 0.14f};
        sceneRoot_->addChild(std::move(amberPane));

        auto bluePane = std::make_unique<Node>(plane, blueGlassMat);
        bluePane->transform().position = {-0.5f, 1.0f, 6.4f};
        bluePane->transform().rotation = Quat::fromEuler({radians(90.0f), 0.0f, 0.0f});
        bluePane->transform().scale    = {0.14f, 0.14f, 0.14f};
        sceneRoot_->addChild(std::move(bluePane));
    } else {
        KOI_WARN("buildScene: failed to create glass-pane textures — skipping transparency demo.");
    }

    // Step 16: the HERO — the Khronos "Damaged Helmet", a real production glTF PBR asset
    // (downloaded at configure time; see CMakeLists.txt). Loading it exercises the glTF
    // material import end-to-end: its base-colour, metallic-roughness, normal, AO and
    // EMISSIVE maps all come straight out of the .glb, decoded via stb_image. Unlike the
    // sphere/torus above we keep the FILE's material (helmet.material), not one we author.
    // The asset is optional — if it didn't download we log and carry on.
    //
    // We import raw primitive geometry and ignore glTF NODE transforms (a documented
    // deferral), and this model is authored Z-up, so we rotate it upright ourselves and
    // give it a 3/4 turn so the lit visor + emissive vents face the camera.
    LoadedModel helmet = loadModel(renderer, (base + "assets/DamagedHelmet.glb").c_str());
    if (helmet.mesh && helmet.material) {
        auto helmetNode = std::make_unique<Node>(helmet.mesh, helmet.material);
        helmetNode->transform().position      = {0.0f, 0.4f, 4.2f};
        helmetNode->transform().scale         = {1.6f, 1.6f, 1.6f};
        // Z-up asset → stand upright (~72° about X, a touch under 90° so the visor tilts
        // toward the elevated camera) + a 3/4 turn so the glowing eyes read as the hero.
        helmetNode->transform().rotation = Quat::fromEuler({radians(72.0f), radians(-22.0f), 0.0f});
        sceneRoot_->addChild(std::move(helmetNode));
        KOI_INFO("Scene built: ground + cube hierarchy + 3 loaded models "
                 "(sphere.glb, torus.obj, DamagedHelmet.glb w/ imported PBR material).");
    } else {
        KOI_WARN("buildScene: DamagedHelmet.glb not loaded — continuing without it "
                 "(re-run CMake configure with network access to fetch the asset).");
        KOI_INFO("Scene built: ground + cube hierarchy + 2 loaded models (sphere.glb, torus.obj).");
    }
    return true;
}

void DemoApp::setupLights() {
    lights_.clear();

    // Light 0 — the SUN: a directional light (parallel rays, no distance falloff).
    // This is the ONLY shadow caster — by convention it lives at index 0, which the
    // renderer/shader rely on. Step 12 note: the PBR shading divides diffuse by π and
    // conserves energy, so intensities are larger than the Blinn-Phong era to land at
    // a comparable brightness.
    Light sun;
    sun.type      = LightType::Directional;
    sun.direction = {-0.4f, -1.0f, -0.3f};  // the direction the rays TRAVEL
    sun.color     = {1.0f, 1.0f, 1.0f};
    sun.intensity = 3.0f;
    lights_.push_back(sun);

    // Light 1 — a warm POINT light off to the right, pooling orange light on the
    // torus and the floor. A point light falls off with distance (see `range`), so
    // nearby surfaces glow while far ones barely register — unlike the sun.
    Light warm;
    warm.type      = LightType::Point;
    warm.position  = {3.6f, 1.2f, 2.2f};
    warm.color     = {1.0f, 0.45f, 0.15f};
    warm.intensity = 24.0f;
    warm.range     = 9.0f;
    lights_.push_back(warm);

    // Light 2 — a cool blue POINT light that ORBITS the scene (animated in onUpdate),
    // so its colored pool sweeps across every surface and the multi-light effect is
    // unmistakable in motion. Its starting position also reads in the t=0 capture.
    Light cool;
    cool.type      = LightType::Point;
    cool.position  = {-3.6f, 1.3f, 2.2f};
    cool.color     = {0.25f, 0.5f, 1.0f};
    cool.intensity = 24.0f;
    cool.range     = 9.0f;
    lights_.push_back(cool);

    // Light 3 — a SPOT light overhead, aimed down at the hub, casting a soft-edged
    // cone of greenish light on the cubes and a ring on the floor. Cutoffs are
    // stored as COSINES of the cone half-angles; because cos DECREASES with angle,
    // the inner (full-bright) cutoff cosine is LARGER than the outer (zero) one.
    Light spot;
    spot.type           = LightType::Spot;
    spot.position       = {0.0f, 4.5f, 1.0f};
    spot.direction      = {0.0f, -1.0f, -0.15f};  // mostly straight down
    spot.color          = {0.7f, 1.0f, 0.7f};
    spot.intensity      = 40.0f;
    spot.range          = 16.0f;
    spot.innerCutoffCos = std::cos(radians(14.0f));
    spot.outerCutoffCos = std::cos(radians(22.0f));
    lights_.push_back(spot);

    KOI_INFO("Lights: 1 directional sun (shadow caster) + 2 point + 1 spot.");
}

void DemoApp::onUpdate(Engine& /*engine*/, float dt) {
    // Continuous input: read the CURRENT keyboard state (not key-down events) so held
    // keys produce smooth, repeat-rate-independent movement.
    camera_.processKeyboard(SDL_GetKeyboardState(nullptr), dt);

    // Smooth the frame rate for a steady HUD readout: a raw 1/dt jitters every frame,
    // so blend it into an exponential moving average (90% history, 10% new sample).
    if (dt > 0.0f) {
        const float instantaneous = 1.0f / dt;
        fps_ = (fps_ > 0.0f) ? (fps_ * 0.9f + instantaneous * 0.1f) : instantaneous;
    }

    // Animate the scene: spin the hub (its satellites orbit with it) and the inner
    // pivot (its moon orbits it). A quaternion has no per-axis angle to bump, so we
    // keep the running Y angle ourselves (radians, scaled by dt for frame-rate
    // independence) and rebuild the rotation from axis-angle each frame. Rebuilding
    // fresh also avoids the slow drift off unit length that accumulating quaternion
    // multiplies would cause.
    hubSpin_     += 0.6f * dt;
    spinnerSpin_ += 1.5f * dt;
    hub_->transform().rotation     = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, hubSpin_);
    spinner_->transform().rotation = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, spinnerSpin_);

    // Orbit the cool point light (index 2) around the scene so its colored pool
    // sweeps across the surfaces — the clearest way to SEE a moving light.
    lightOrbit_ += 0.8f * dt;
    if (lights_.size() > 2) {
        lights_[2].position = {3.6f * std::cos(lightOrbit_), 1.3f,
                               3.6f * std::sin(lightOrbit_)};
    }

    // Propagate transforms down the tree (parent → child → grandchild) so every
    // node's cached world matrix is current before the engine draws this frame.
    sceneRoot_->updateWorldTransforms();

    // Rebuild this frame's debug overlay AFTER the world transforms are current, so
    // the AABBs and light crosses sit at their up-to-date world positions.
    buildDebugLines();

    // Rebuild the 2D HUD (camera position + toggle states reflect this frame).
    buildHud();
}

void DemoApp::buildDebugLines() {
    // Immediate mode: throw away last frame's lines and re-declare what to draw now.
    debug_.clear();

    // Green AABB around every drawable node — makes Step 20's per-mesh bounds (and
    // what frustum culling tests) visible.
    if (debugBounds_ && sceneRoot_) {
        addNodeBounds(debug_, *sceneRoot_, kBoundsColor);
    }

    // Amber wireframe of the frozen camera frustum — the volume Step 20 culls
    // against. Frozen (key F) so you can fly out of it and watch it stay put.
    if (debugFrustum_ && haveFrozenFrustum_) {
        debug_.frustum(frozenViewProj_, kFrustumColor);
    }

    // A cross at each ENABLED positioned light, tinted its own colour; spot lights
    // also get a short ray along their aim. Directional lights have no position, so
    // they're skipped (the sun is "everywhere").
    if (debugLights_) {
        for (const Light& light : lights_) {
            if (!light.enabled || light.type == LightType::Directional) {
                continue;
            }
            debug_.cross(light.position, 0.5f, light.color);
            if (light.type == LightType::Spot) {
                debug_.ray(light.position, light.direction, 1.5f, light.color);
            }
        }
    }
}

void DemoApp::buildHud() {
    // Immediate mode, like buildDebugLines: discard last frame's HUD and re-declare it.
    hud_.clear();
    if (!showHud_) {
        return;  // hidden — leave the HUD empty so the renderer skips the overlay pass
    }

    const float scale   = 2.0f;                                 // 16px-tall glyphs
    const float lineH   = static_cast<float>(kGlyphPx) * scale;  // one text row
    const float pad     = 6.0f;                                  // panel inner margin
    const float lineGap = 2.0f;                                  // gap between rows

    // Format the live readouts. snprintf into fixed buffers keeps this allocation-free.
    const Vec3 cam = camera_.position();
    char title[48];
    char fpsLine[48];
    char camLine[64];
    char toggleLine[80];
    char helpLine[64];
    std::snprintf(title, sizeof(title), "Koi Engine - Step 23 HUD");
    std::snprintf(fpsLine, sizeof(fpsLine), "FPS %3.0f  (%.2f ms)",
                  fps_, fps_ > 0.0f ? 1000.0f / fps_ : 0.0f);
    std::snprintf(camLine, sizeof(camLine), "Cam %6.2f %6.2f %6.2f", cam.x, cam.y, cam.z);
    std::snprintf(toggleLine, sizeof(toggleLine), "G bounds:%-3s  L lights:%-3s  F frustum:%-3s",
                  debugBounds_ ? "on" : "off", debugLights_ ? "on" : "off",
                  debugFrustum_ ? "on" : "off");
    std::snprintf(helpLine, sizeof(helpLine), "WASD move   mouse look   H hud");

    const char* lines[] = {title, fpsLine, camLine, toggleLine, helpLine};
    constexpr int kLineCount = 5;

    // Size a translucent backing panel to the widest line so the text stays readable
    // over any scene. Draw the panel FIRST (behind) then the text — draw order is the
    // only "layering" a flat 2D overlay has.
    float maxW = 0.0f;
    for (const char* s : lines) {
        maxW = std::max(maxW, Hud::textWidth(s, scale));
    }
    const float panelW = maxW + pad * 2.0f;
    const float panelH = kLineCount * lineH + (kLineCount - 1) * lineGap + pad * 2.0f;
    hud_.rect(0.0f, 0.0f, panelW, panelH, kHudPanel);

    float y = pad;
    for (const char* s : lines) {
        hud_.text(pad, y, scale, kHudText, s);
        y += lineH + lineGap;
    }
}

void DemoApp::onEvent(Engine& engine, const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            // In SDL3, event.key.key holds the virtual keycode. ESC quits; the number
            // keys toggle each post-processing effect / light group and the brackets
            // nudge exposure, so the reader can watch what every effect does.
            switch (event.key.key) {
                case SDLK_ESCAPE:
                    KOI_INFO("Escape pressed — shutting down.");
                    engine.requestQuit();
                    break;
                case SDLK_1:
                    post_.tonemap = !post_.tonemap;
                    KOI_INFO("Tone-mapping: %s", post_.tonemap ? "on" : "off");
                    break;
                case SDLK_2:
                    post_.bloom = !post_.bloom;
                    KOI_INFO("Bloom: %s", post_.bloom ? "on" : "off");
                    break;
                case SDLK_3:
                    post_.fxaa = !post_.fxaa;
                    KOI_INFO("FXAA: %s", post_.fxaa ? "on" : "off");
                    break;
                case SDLK_4:
                    post_.vignette = !post_.vignette;
                    KOI_INFO("Vignette: %s", post_.vignette ? "on" : "off");
                    break;
                case SDLK_LEFTBRACKET:  // '[' — darker
                    post_.exposure = SDL_max(0.1f, post_.exposure / 1.25f);
                    KOI_INFO("Exposure: %.2f", post_.exposure);
                    break;
                case SDLK_RIGHTBRACKET:  // ']' — brighter
                    post_.exposure = SDL_min(8.0f, post_.exposure * 1.25f);
                    KOI_INFO("Exposure: %.2f", post_.exposure);
                    break;
                case SDLK_5: {
                    // Toggle the two colored POINT lights together (indices 1-2).
                    const bool on = (lights_.size() > 1) ? !lights_[1].enabled : false;
                    if (lights_.size() > 1) {
                        lights_[1].enabled = on;
                    }
                    if (lights_.size() > 2) {
                        lights_[2].enabled = on;
                    }
                    KOI_INFO("Point lights: %s", on ? "on" : "off");
                    break;
                }
                case SDLK_6:
                    // Toggle the SPOT light (index 3).
                    if (lights_.size() > 3) {
                        lights_[3].enabled = !lights_[3].enabled;
                        KOI_INFO("Spot light: %s", lights_[3].enabled ? "on" : "off");
                    }
                    break;
                case SDLK_7:
                    // Toggle the directional SUN (index 0). With it off, the point/spot
                    // lights (which cast no shadow) light the scene alone.
                    if (!lights_.empty()) {
                        lights_[0].enabled = !lights_[0].enabled;
                        KOI_INFO("Sun: %s", lights_[0].enabled ? "on" : "off");
                    }
                    break;
                case SDLK_8: {
                    // Step 14: toggle the SKYBOX so its effect is easy to see (with it
                    // off, the background falls back to the flat clear colour).
                    const bool on = !engine.renderer().skyboxEnabled();
                    engine.renderer().setSkyboxEnabled(on);
                    KOI_INFO("Skybox: %s", on ? "on" : "off");
                    break;
                }
                case SDLK_9: {
                    // Step 15: toggle IMAGE-BASED LIGHTING. With it off, the ambient
                    // term falls back to the flat constant fill (the Step 12 look), so
                    // the metal sphere goes dark — a clean A/B for what the environment
                    // actually contributes.
                    const bool on = !engine.renderer().iblEnabled();
                    engine.renderer().setIblEnabled(on);
                    KOI_INFO("Image-based lighting: %s", on ? "on" : "off");
                    break;
                }
                case SDLK_0: {
                    // Step 20: toggle FRUSTUM CULLING. It's on by default and invisible
                    // in a scene where everything is on-screen — but the renderer logs
                    // the drawn/culled counts, so turning it off (then flying so objects
                    // leave the view) shows the count it was saving. Purely a perf switch:
                    // what remains on screen is identical either way.
                    const bool on = !engine.renderer().frustumCullingEnabled();
                    engine.renderer().setFrustumCullingEnabled(on);
                    KOI_INFO("Frustum culling: %s", on ? "on" : "off");
                    break;
                }
                case SDLK_T: {
                    // Step 21: toggle back-to-front sorting of the translucent panes.
                    // With it OFF they draw in scene order, so where the two overlap the
                    // blending composites in the WRONG order — the visible artifact that
                    // makes the case for the painter's-algorithm sort.
                    const bool on = !engine.renderer().transparentSortEnabled();
                    engine.renderer().setTransparentSortEnabled(on);
                    KOI_INFO("Transparent sorting (back-to-front): %s", on ? "on" : "off");
                    break;
                }
                case SDLK_G:
                    // Step 22: toggle the per-node AABB wireframes — the bounds Step 20
                    // culls with, finally drawn on screen.
                    debugBounds_ = !debugBounds_;
                    KOI_INFO("Debug bounds: %s", debugBounds_ ? "on" : "off");
                    break;
                case SDLK_F: {
                    // Step 22: freeze + show the CAMERA FRUSTUM. Snapshotting the last
                    // frame's view-projection (the exact matrix the culler used) lets you
                    // then fly out of it and watch what's culled — the payoff of Step 20
                    // made visible. Pressing F again hides it; pressing once more re-freezes
                    // at the new camera pose.
                    debugFrustum_ = !debugFrustum_;
                    if (debugFrustum_) {
                        frozenViewProj_    = engine.renderer().lastCameraViewProjection();
                        haveFrozenFrustum_ = true;
                    }
                    KOI_INFO("Debug frustum: %s", debugFrustum_ ? "frozen + shown" : "off");
                    break;
                }
                case SDLK_L:
                    // Step 22: toggle the light-position crosses (spot lights also get a
                    // direction ray), each tinted its light's colour.
                    debugLights_ = !debugLights_;
                    KOI_INFO("Debug light icons: %s", debugLights_ ? "on" : "off");
                    break;
                case SDLK_H:
                    // Step 23: toggle the 2D HUD (FPS, camera position, toggle states).
                    showHud_ = !showHud_;
                    KOI_INFO("HUD: %s", showHud_ ? "on" : "off");
                    break;
                default:
                    break;
            }
            break;

        case SDL_EVENT_MOUSE_MOTION:
            // Relative mouse mode reports motion as deltas (xrel/yrel). We feed them
            // straight into the camera as a yaw/pitch change — FPS look.
            camera_.addMouseLook(event.motion.xrel, event.motion.yrel);
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            // The swapchain (and the depth texture) auto-resize, and the renderer
            // recomputes the aspect ratio from the swapchain size each frame, so
            // there's nothing to do here but log it.
            KOI_DEBUG("Window resized to %dx%d", event.window.data1, event.window.data2);
            break;

        default:
            break;
    }
}

FrameView DemoApp::frameView() const {
    // Hand the engine a read-only snapshot of what to draw: our clear colour, the
    // camera's view + eye, the scene root, the light list, and the post settings.
    // `lights_` outlives this call (it's a member), so the span stays valid.
    return FrameView{
        .clearColor = kClearColor,
        .view       = camera_.viewMatrix(),
        .root       = sceneRoot_.get(),
        .cameraPos  = camera_.position(),
        .lights     = lights_,
        .post       = post_,
        // The debug lines built this frame in buildDebugLines(). `debug_` is a
        // member, so the span stays valid for this call (empty ⇒ no overlay).
        .debugLines = debug_.vertices(),
        // The HUD geometry built this frame in buildHud(); `hud_` is a member too
        // (empty ⇒ no overlay, drawn last so it stays crisp over the final image).
        .hudVertices = hud_.vertices(),
    };
}

void DemoApp::onShutdown() {
    // Release the scene BEFORE the engine destroys the GPU device: the meshes' buffers
    // and the materials' textures are freed through that device. The engine calls this
    // from run() while the renderer is still alive, so this ordering is guaranteed.
    sceneRoot_.reset();
    hub_     = nullptr;  // dangling now that the tree is gone
    spinner_ = nullptr;
}

}  // namespace koi
