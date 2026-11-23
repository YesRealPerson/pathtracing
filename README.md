# Project 3-1: Pathtracer

Notepad of useful commands because lol
./build/pathtracer.exe -r 800 600 ./dae/sky/CBempty.dae

## Acknowledgements
Claude was used for a lot of debugging, and pointing out design flaws when it saw them while debugging 💀. Also it made the CMake file because I don't know how to do that.

## Important files
### gpu_scene
GPU versions for the following
1. Primitives
2. BVH Nodes
3. BSDFs
4. Lights
6. Camera
    - Introduces a to_gpu method for Camera to make GPUCamera structs.
7. Scenes

Regarding GPU scenes the GPUScene struct consolidates GPU primitives and GPU BVH nodes.

Primitives, BSDFs, and lights get to_gpu functions that turn them into their GPU equivalents. *(See the upload function)*

GPU kernels refer to this struct via a GPUSceneView which holds pointers to key information regarding the scene such as the list of primitives and the list of bvh nodes.

See the comment starting around the copy constructor for more information as to why we pass in a view rather than just the struct itself.
### gpu_render
The GPU render file holds GPU equivalents for a lot of existing CPU functions.

- `Camera::generate_ray`
- sampling functions
    - `UniformGirdSampler2D` (area lights)
    - `UniformSphereSampler3D` ()
    - `CosineWeightedHemisphereSampler3D` (diffuse BSDF `sample_f`)
    - `UniformHemisphereSampler3D` (emission BSDF `sample_f`)
- `sample_L`
    - Switches between types of lights and runs GPU equivalent `sample_L` functions.
- `bsdf_f`
    - Switches between BSDF types and runs GPU equivalent `f` functions.
- `curand_setup`
    - Calls `curand_init` with subsequence based of thread id (which is just its absolute position in the image)
- `render`
    - GPU kernel launch point
    - Mallocs space for `Vector3D`s and random generators per pixel
    - Sets up random states per pixel (see above)
    - Launches the kernel with enough 16x16 blocks to cover the image space
    - Copies result of kernel to memory
- `render_kernel`
    - Currently just zero and one bounce lighting
    - Ultimately very similar to CPU code just with GPU equivalents used instead.


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
3. Global Illumination
    - IDK For now

# CLAUDE README STUFF (not human written)

## Leftover Claude code

| Location | Marker | What it is |
|---|---|---|
| [gpu_scene.h:137-141](src/pathtracer/gpu_scene.h#L137-L141) | to be deleted | `upload_dummy_bvh()` declaration. |
| [gpu_scene.cu:120-195](src/pathtracer/gpu_scene.cu#L120-L195) | unreviewed, to be deleted | `upload_dummy_bvh()`: copies primitives back to compute scene bounds, uploads one leaf covering every primitive. |
| [gpu_intersect.cuh:100-152](src/pathtracer/gpu_intersect.cuh#L100-L152) | unreviewed, to be deleted | `intersect_scene()` / `has_intersection_scene()`: brute-force loop over every leaf in the node array, no culling. `render_kernel` uses both. |

## Depreciated
`raytrace_tile` / `worker_thread` are still defined but unused

## GPU TODOs

| Location | TODO |
|---|---|
| [gpu_intersect.cuh:75](src/pathtracer/gpu_intersect.cuh#L75) | `intersect_sphere`: not written, so spheres are invisible (no hits, no shadows). |
| [gpu_intersect.cuh:96](src/pathtracer/gpu_intersect.cuh#L96) | `intersect_bbox`: placeholder returning `true`. Needed before real BVH traversal. |
| [application.cpp:644](src/application/application.cpp#L644) | Make a proper BVH: flatten `BVHAccel` into `GPUBVHNode`s, and upload primitives in BVH order (the leaves index into the array). |
| [raytraced_renderer.cpp:312](src/pathtracer/raytraced_renderer.cpp#L312) | `render_kernel` only does zero + one bounce. `max_ray_depth` and `samples_per_batch` are synced into `GPUScene` but nothing reads them yet (global illumination, adaptive sampling). |
| [raytraced_renderer.cpp:317](src/pathtracer/raytraced_renderer.cpp#L317) | Ray count metric. |

Not yet on the GPU:
- Spot, sphere, mesh and environment lights upload as `GPU_LIGHT_UNKNOWN` and `render_kernel` skips them. Rays that miss return black.
- Only `EMISSIVE` and `DIFFUSE` BSDFs exist on the GPU (`bsdf_f` returns 0 for anything else).

## Minor issues (not blocking)

- `render` seeds every pixel with the same fixed seed (184) on every call and never writes the RNG state back, so re-rendering gives identical noise. Fine for now; matters once batches/progressive rendering accumulate samples.
- `render_kernel`'s light-sample loop reuses `i` as its counter, shadowing the `ns_aa` loop's `i`. Harmless, but easy to trip on when adding bounces.
- `render` returns early when `num_nodes == 0`, leaving the previous contents of `sampleBuffer` in the frame.
