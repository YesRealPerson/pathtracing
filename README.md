# Project 3-1: Pathtracer

Notepad of useful commands because lol
./build/pathtracer.exe -r 800 600 ./dae/sky/CBempty.dae

## Leftover Claude code

| Location | Marker | What it is |
|---|---|---|
| [gpu_render.h:1-24](src/pathtracer/gpu_render.h#L1-L24) | TEMPORARY, needs editing | Whole file. Declares `gpu_render_normals()`. |
| [gpu_render.cu:38-79](src/pathtracer/gpu_render.cu#L38-L79) | TEMPORARY, edit or replace | `render_normals_kernel` (one thread per pixel, one ray through the pixel center, normal/debug shading) and the host wrapper (allocate output, launch 16x16 blocks, check errors, copy back). |
| [gpu_scene.h:89-93](src/pathtracer/gpu_scene.h#L89-L93) | to be deleted | `upload_dummy_bvh()` declaration. |
| [gpu_scene.cu:101-176](src/pathtracer/gpu_scene.cu#L101-L176) | unreviewed, to be deleted | `upload_dummy_bvh()`: copies primitives back to compute scene bounds, uploads one leaf covering every primitive. |
| [gpu_intersect.cuh:100-152](src/pathtracer/gpu_intersect.cuh#L100-L152) | unreviewed, to be deleted | `intersect_scene()` / `has_intersection_scene()`: brute-force loop over every leaf in the node array, no culling. |
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
| [gpu_intersect.cuh:93](src/pathtracer/gpu_intersect.cuh#L93) | `intersect_bbox`: placeholder returning `true`. Needed before real BVH traversal. |
| [application.cpp:644](src/application/application.cpp#L644) | Make a proper BVH: flatten `BVHAccel` into `GPUBVHNode`s, and upload primitives in BVH order (the leaves index into the array). |
| [raytraced_renderer.cpp:304](src/pathtracer/raytraced_renderer.cpp#L304) | Replace `gpu_render_normals` with the real path-tracing kernel. |

Not yet on the GPU: BSDFs (`host_bsdfs` is host-only), lights / `envLight` (misses are black), per-thread RNG, `ns_aa` sampling, bounces.

## Minor issues (not blocking)

- `intersect_triangle`: `1.0 / parallel` is a double literal, so it does a double-precision divide on the GPU. Use `1.0f`.
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
