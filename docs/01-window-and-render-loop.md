# Window & the Render Loop

> **Goal of Step 0:** open a window and clear it to a solid color, every frame,
> using SDL3's modern GPU API.
>
> That sounds almost too simple to bother with — but "clear the screen" already
> drives the *entire* modern GPU pipeline. Once you understand the machinery
> here, everything later (triangles, textures, 3D, lighting) is just *adding
> draw commands* inside a structure you already know.

This page is written for someone new to graphics programming. We'll build up the
concepts first, then walk through the actual Koi Engine code line by line.

---

## 1. The mental model: you don't draw, you *describe* and *submit*

If you've done any 2D drawing before (HTML canvas, old-school OpenGL), you're
used to "imperative" drawing: you call `drawRectangle(...)` and a rectangle
appears *right now*.

Modern GPUs don't work like that, and for a good reason: the **CPU** (your
program) and the **GPU** (the graphics chip) are two separate processors running
at the same time. If the CPU had to wait for the GPU to finish each little
command, both would spend most of their time idle.

So the modern model is:

1. The CPU **records** a list of commands ("clear this image", "draw these
   triangles") into a **command buffer**.
2. The CPU **submits** the whole buffer to the GPU.
3. The GPU executes it **on its own time**, while the CPU is already busy
   building the *next* frame.

Think of it like a chef (CPU) writing a full ticket of orders and handing it to
the kitchen (GPU), rather than standing over the stove for each dish.

Keep this picture in mind — every API call below is part of "write the ticket,
hand it over."

---

## 2. The cast of characters

### The GPU device

The **device** is your program's handle to the graphics card. Creating it makes
SDL pick a **backend** — the actual low-level graphics API for your platform:

| Platform | Backend the device uses |
|----------|------------------------|
| macOS    | **Metal** |
| Windows  | Direct3D 12 |
| Linux    | Vulkan |

The whole point of SDL's GPU API is that **we write our engine once** and SDL
maps it onto whichever backend is available. When you run Koi Engine on this Mac,
the logs say `GPU device ready (backend: metal)` — that's SDL choosing Metal for
you, with no Metal-specific code on our side.

### The window and its swapchain

A **window** is just a rectangle of pixels the OS gives us. But we don't draw to
the window *directly*. Instead, when we "claim" the window for the GPU device,
SDL creates a **swapchain** for it.

A swapchain is a small set of images (usually 2 or 3) that we cycle through:

```
        ┌─────────────┐        ┌─────────────┐
        │  Image A    │        │  Image B    │
        │ (on screen) │        │ (we draw    │
        │             │        │  into this) │
        └─────────────┘        └─────────────┘
              ▲                       │
              │   when we're done,    │
              └───  they swap  ◄──────┘
```

While the monitor displays image A, we draw the next frame into image B. When
we're done, they **swap** (hence "swap-chain"): B goes on screen, and we start
drawing into A. This is **double buffering**, and it's why you never see a
half-drawn frame flicker on screen.

"Acquiring" a swapchain image each frame can also **wait** until the display is
ready for a new one — that's **vsync**, and it naturally paces our loop to the
monitor's refresh rate (≈60 frames per second) instead of spinning at thousands
of useless frames per second. (You can see this in our logs: 120 frames take
about 2 seconds.)

### The command buffer

As described in §1, the **command buffer** is the to-do list we record GPU
commands into and submit as a batch. We get a fresh one each frame.

### The render pass

A **render pass** is a bracket — `Begin ... End` — that declares:

- **Which image(s)** we're rendering into (this frame's swapchain image), and
- **What to do with the existing contents** before and after we draw:
  - `load_op = CLEAR` — wipe the image to a chosen color *before* drawing. (This
    is the "clear the screen" everything sits on top of.)
  - `store_op = STORE` — *keep* the result afterward so it can be displayed.

In Step 0 the render pass contains **no draw calls** — the clear *is* the whole
frame. In Step 1 we'll start issuing draw commands between `Begin` and `End`.

### Present

After we submit the command buffer, SDL queues the finished image to be shown on
screen at the next swap. We don't call a separate "present" function — submitting
a command buffer that rendered to the swapchain image takes care of it.

---

## 3. The frame, end to end

Putting the cast together, **one frame** of Koi Engine looks like this:

```
  acquire command buffer        ← get a fresh to-do list
        │
  acquire swapchain image       ← (may wait for vsync) the image we draw into
        │
  begin render pass             ← target = that image, load_op = CLEAR
        │
   (Step 1+: draw calls here)
        │
  end render pass
        │
  submit command buffer         ← hand the list to the GPU; queues the present
```

The CPU then loops straight back to the top and starts the next frame while the
GPU is still working on this one.

---

## 4. The code

Now let's map those concepts onto the actual source. Three files matter here.

### `Engine` — the loop ([src/core/Engine.cpp](../src/core/Engine.cpp))

The engine runs the **game loop**: handle input, render a frame, repeat.

```cpp
while (running_) {
    processEvents();                    // drain OS/input events
    renderer_->renderFrame(kClearColor); // record + submit one frame
}
```

`processEvents()` pulls every queued event with `SDL_PollEvent` and reacts:
`SDL_EVENT_QUIT` (close button) and `Esc` stop the loop; `SDL_EVENT_WINDOW_RESIZED`
is just logged for now (the swapchain resizes itself).

### `GpuRenderer` — device setup ([src/renderer/GpuRenderer.cpp](../src/renderer/GpuRenderer.cpp))

The constructor builds the two long-lived GPU objects from §2:

```cpp
// 1. Create the device, declaring which shader formats we may supply later.
device_ = SDL_CreateGPUDevice(
    SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL,
    /*debug_mode=*/true, nullptr);

// 2. Claim the window — this creates its swapchain.
SDL_ClaimWindowForGPUDevice(device_, window_);
```

The shader-format flags are a *promise* for later (we have no shaders yet in Step
0); listing all three keeps the engine cross-platform. `debug_mode = true` turns
on the backend's validation layer — extra checks and error messages that are
invaluable while learning, which we'll switch off in Release builds.

### `GpuRenderer::renderFrame` — one frame

This is §3 in code. Read it next to the diagram above:

```cpp
// (1) Fresh command buffer — our to-do list for this frame.
SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);

// (2) The image to draw into. May block until vsync hands one back.
SDL_GPUTexture* swapchainTexture = nullptr;
SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &swapchainTexture, nullptr, nullptr);

if (swapchainTexture != nullptr) {       // null when minimized — just skip
    // (3) Describe the render pass: clear this image to clearColor, keep result.
    SDL_GPUColorTargetInfo colorTarget = {};
    colorTarget.texture     = swapchainTexture;
    colorTarget.clear_color = clearColor;
    colorTarget.load_op     = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op    = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, nullptr);
    // (Step 1+: bind a pipeline and draw here.)
    SDL_EndGPURenderPass(pass);
}

// (4) Submit — execute on the GPU and queue the image for display.
SDL_SubmitGPUCommandBuffer(cmd);
```

A couple of details worth noticing, because they're easy to get wrong:

- **The minimized-window case.** When the window is minimized there's no image to
  draw into, so `swapchainTexture` comes back `null`. That is *not* an error — we
  skip the render pass but **still submit** the command buffer, otherwise it would
  leak. (A command buffer must always be submitted or cancelled.)
- **Teardown order.** In the destructor we `SDL_ReleaseWindowFromGPUDevice`
  *before* `SDL_DestroyGPUDevice`. Releasing the window first detaches the
  swapchain; destroying the device also waits for the GPU to finish any in-flight
  frame, so nothing is torn down while still in use.

---

## 5. Try it yourself

The fastest way to build intuition is to poke at it:

1. **Change the clear color.** Edit `kClearColor` in
   [src/core/Engine.cpp](../src/core/Engine.cpp) (RGBA floats in `0..1`) and
   rebuild. Make it animate over time as an extra challenge (hint: feed
   `SDL_GetTicks()` into a sine wave).
2. **Watch the backend.** Run with the GPU debug layer on (it already is in debug
   builds) and read the startup logs — confirm it says `metal`.
3. **Cap the frames.** `KOI_MAX_FRAMES=300 ./build/koi-engine` and time it — you
   should see it tied to your refresh rate via vsync.

---

## 6. What's next

➡️ **Step 1 — First Triangle.** We keep this exact frame structure but add a
**graphics pipeline** and our first **shaders** (small programs that run on the
GPU). That introduces the shader toolchain: we'll author shaders once and compile
them to the format each backend needs (SPIR-V for Vulkan, MSL for Metal) using
`SDL_shadercross`. The triangle is the "hello world" of 3D graphics — and the
moment the screen stops being a single flat color.
