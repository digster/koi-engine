// ============================================================================
//  fullscreen.vert — the "fullscreen triangle" vertex shader (Step 10)
// ----------------------------------------------------------------------------
//  Post-processing means running a shader over the WHOLE rendered image. To do
//  that we need to draw something that covers every pixel of the screen, so the
//  fragment shader runs once per output pixel. The classic, buffer-free way is a
//  single triangle big enough that its inside covers the entire [-1,1] clip-space
//  square (the bits hanging off-screen are simply clipped away).
//
//  Why a triangle and not a quad? A quad is two triangles meeting along a diagonal;
//  the GPU would rasterize that shared edge twice and there's a subtle seam. One
//  oversized triangle has no interior edge, needs NO vertex buffer at all, and is
//  marginally faster — so it's the standard idiom for every screen-space pass.
//
//  We synthesize the 3 corners purely from gl_VertexIndex (0,1,2): the draw call is
//  just "draw 3 vertices, no buffers" (SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0)).
// ============================================================================
#version 450

// The interpolated texture coordinate handed to the fragment shader: where in the
// SOURCE image this output pixel should read from. [0,1] across the visible screen.
layout(location = 0) out vec2 vUV;

void main() {
    // Map the vertex index to the corners (0,0), (2,0), (0,2):
    //   idx 0 -> (0,0)   idx 1 -> (2,0)   idx 2 -> (0,2)
    // Bit tricks: (idx<<1)&2 is 0,2,0 ; idx&2 is 0,0,2.
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));

    // Stretch those to clip space: 0 -> -1, 2 -> +3. The triangle's corners are at
    // (-1,-1), (3,-1), (-1,3); its interior fully covers the visible [-1,1] square.
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);

    // Texture coordinates run 0..1 over the visible region. We FLIP Y because a
    // sampled texture's origin (0,0) is the TOP-left, while clip-space +Y is up —
    // the same convention the shadow lookup accounts for in triangle.frag. Without
    // this flip the post-processed image would be upside-down.
    vUV = vec2(p.x, 1.0 - p.y);
}
