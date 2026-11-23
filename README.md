# Project 3-1: Pathtracer

Notepad of useful commands because lol
./build/pathtracer.exe -r 800 600 ./dae/sky/CBempty.dae

## Acknowledgements
Claude was used for a lot of debugging, and pointing out design flaws when it saw them while debugging 💀.

## Important files
### gpu_scene
GPU versions for the following
1. Primitives
2. BVH Nodes
3. Camera
    - Introduces a to_gpu method for Camera to make GPUCamera structs.
4. Scenes

Regarding GPU scenes the GPUScene struct consolidates GPU primitives and GPU BVH nodes.

*(See the upload function)*

GPU kernels refer to this struct via a GPUSceneView which holds pointers to key information regarding the scene such as the list of primitives and the list of bvh nodes.

See the comment starting on line 67 for more information as to why we pass in a view rather than just the struct itself.
### gpu_render
Currently entirely AI just to get a visual output, sorry. We'll circle back to this later.

### gpu_intersect
GPU versions for the following
1. Ray
2. Intersect
3. intersect_triangle
4. intersect_sphere TODO
5. intersect_bbox TODO
6. intersect_scene TODO (claude dumb version for now, the GPU unironically just brute forces this shit though)

## Important Changes
1. Replaced CPU rendering code for GPU code (see changes to application.cpp and raytraced_renderer.cpp, namely set_up_pathtracer, raytrace_gpu, and start_raytracing)
2. Added createGPU functions for all primitives, see comment in gpu_scene.h:19 
    - Notably BVHAccelerator is a Primitive subclass which has a degenerate createGPU function
        - Bandaid but who cares

## Main TODOs
1. Construct proper BVH and BVH walking
2. Sphere intersection
3. BSDF f and sample_f
    - Add GPU version of BSDFs to gpu_scene
        - Forget the old idea of just doing it on host as a first step waste of time.
    - Add GPU versions of f, sample_f, and is_delta (probably into something like gpu_bsdf.cuh, analogous to gpu_intersect.cuh)
        - Requires GPU version of CosineWeightedHemisphereSampler3D
        - Right now only care about EmissionBSDF and Diffuse BSDF
        - Technically sample_f not needed until GI
4. Direct lighting
    - Zero bounce - L_out = emission
    - One bounce
        - Add GPU version of lights (see light.cpp)
        - Add GPU version of is_delta_light and sample_L
            - Requires UniformHemisphereSampler3D, UniformGridSampler2D
    - Rewrite gpu_render
5. Global Illumination
    - IDK For now

# CLAUDE README STUFF (not human written)

## Leftover Claude code

| Location | Marker | What it is |
|---|---|---|
| [gpu_render.h:1-24](src/pathtracer/gpu_render.h#L1-L24) | TEMPORARY, needs editing | Whole file. Declares `gpu_render_normals()`. |
| [gpu_render.cu:38-79](src/pathtracer/gpu_render.cu#L38-L79) | TEMPORARY, edit or replace | `render_normals_kernel` (one thread per pixel, one ray through the pixel center, normal/debug shading) and the host wrapper (allocate output, launch 16x16 blocks, check errors, copy back). |
| [camera.h:93-97](src/pathtracer/camera.h#L93-L97) | unreviewed, to be deleted | `to_gpu()` declaration. (The definition in `camera.cpp` is unmarked.) |
| [raytraced_renderer.h:103-109](src/pathtracer/raytraced_renderer.h#L103-L109) | unreviewed, to be deleted | `set_gpu_scene()`. |
| [CMakeLists.txt:69-71](CMakeLists.txt#L69-L71) | unreviewed, to be deleted | `gpu_render.cu` source entry. |
| [CMakeLists.txt:175-178](CMakeLists.txt#L175-L178) | unreviewed, to be deleted | `gpu_intersect.cuh` / `gpu_render.h` header entries. |


## Depreciated 
`raytrace_tile` / `worker_thread` are still defined but unused

## GPU TODOs

| Location | TODO |
|---|---|
| [gpu_intersect.cuh:72](src/pathtracer/gpu_intersect.cuh#L72) | `intersect_sphere`: not written, so spheres render black. |
| [raytraced_renderer.cpp:304](src/pathtracer/raytraced_renderer.cpp#L304) | Replace `gpu_render_normals` with the real path-tracing kernel. |

Not yet on the GPU: BSDFs (`host_bsdfs` is host-only), lights / `envLight` (misses are black), per-thread RNG, `ns_aa` sampling, bounces.

## Minor issues (not blocking)

- `intersect_triangle`: a NaN `t` passes `t < min_t || t > max_t`. `if (!(t >= r.min_t && t <= r.max_t))` rejects it.
- `GPUScene::free()` prints "cudaFree failed" at program exit once the CUDA runtime has shut down (`cudaErrorCudartUnloading`). Harmless.
- Every `R` press calls `get_static_scene()` (new allocation) and re-uploads to the GPU. The static-scene leak is from the starter code.

## Assignment TODOs (starter code)

| File | Remaining |
|---|---|
| `scene/sphere.cpp` | Part 1.4 (3 TODOs) |
| `scene/bbox.cpp` | Part 2.2 |
| `scene/bvh.cpp` | Part 2.1, Part 2.3 (x2) |
| `pathtracer/camera.cpp` | Part 1.1 comment (implementation present) |
| `pathtracer/pathtracer.cpp` | Parts 1.2, 3, 4, 5 |
| `pathtracer/bsdf.cpp` | Part 3.1 (x2) |
| `pathtracer/advanced_bsdf.cpp`, `scene/environment_light.cpp` | Project 3-2 |
