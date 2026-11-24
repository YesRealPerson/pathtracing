# glTF 2.0 scenes (.glb / .gltf)

*(Claude-written, like the "CLAUDE README STUFF" section of the main README.)*

The path tracer loads glTF 2.0 scenes as well as COLLADA. It supports textures
(base color and emission, with `KHR_texture_transform`), point, spot and sun
lights in physical units, and the file's camera. Geometry is stored compactly
on the GPU: 16 bytes per triangle plus shared vertices.

Quick numbers:

| Scene | Load | GPU memory | Render |
|---|---|---|---|
| `glb/hideout_marathon.glb` (801k triangles, 7 textures used) | 0.45 s | 313 MB (258 MB of it textures) | 1504×800, 512 spp: 86 s |
| `big.glb` test (3.86M triangles) | 0.19 s | 195 MB | 800×600, 128 spp: 1.1 s |

## Usage

```
./build/pathtracer.exe -r 1504 800 ../glb/hideout_marathon.glb                    # window
./build/pathtracer.exe -f out.png -r 1504 800 -s 512 -m 6 -x 3 -T scene.glb        # windowless
```

The file extension picks the loader: `.glb` / `.gltf` (with its `.bin` next to
it) use the glTF loader, everything else uses the COLLADA parser. The launcher
GUI accepts both.

New command line options (they work for COLLADA too; the defaults change
nothing):

| Option | What |
|---|---|
| `-x <stops>` | Exposure: the image is scaled by 2^stops before gamma. glTF scenes in physical units usually want +2 to +4. |
| `-T` | Filmic (ACES) tone curve: bright areas roll off instead of clipping to white. |
| `-W <white>` | Highlight compression (extended Reinhard on luminance, hue kept): brightness `white` maps to white. `4` tames the hideout's canopy. Hotkeys `N` / `M` adjust it live. |
| `PT_DEBUG_ALBEDO=1` (env var) | Debug view: base color (textures applied) at the first hit, no lighting. Use with `-x -0.5 -m 1`. |

The loader prints what it found and warns about anything it skipped:

```
[glTF Loader]  |- 663 mesh nodes (599 instanced), 801370 triangles in scene (146280 unique), 107095 vertices
[glTF Loader]  |- materials: 28 diffuse, 0 mirror, 0 glass, 1 emissive; 26 with a base color texture, 3 emissive on a reflective surface
[glTF Loader]  |- textures: 29 textures, 7 of 11 images decoded (258 MB RGBA8) in 383 ms
[glTF Loader]  |- 13 lights (2 spot), camera: Camera.001
[GPUScene] Uploaded 801370 primitives (12.2 MB), 752648 vertices (23.0 MB), BVH (20.1 MB), 29 textures (258.0 MB) in 235 ms; total 313.3 MB
```

## The hideout scene, and why the original export doesn't look like the Blender render

`glb/hideout_marathon_blender.jpg` is a render of the original `.blend`. The
`.glb` can't reproduce it, in this renderer or any glTF viewer, because the
export lost most of the look:

- **About 20 of the scene's textures were dropped.** The `.blend` uses about
  30 images, and the `.glb` embeds 11. All the decal and UI atlases are
  missing, including `80AC8495`, which holds the floor graphics in the
  reference ("FRAME 0000198523 LOCK", "N73", grids). Blender's exporter only
  writes textures that are wired straight into standard material slots. These
  were mixed in through shader node setups that glTF can't represent.
- **The textures that were exported are channel-packed masks used as base
  color.** The main surface atlas (`80A45640`) is near-black (RGB 0–19), one
  canopy atlas is green-only, and `80A40510` keeps its shapes in alpha on an
  `OPAQUE` material.
- **The reference has volumetric fog**, which glTF can't store and this
  renderer doesn't support.

To check that the renderer reads the `.glb` correctly, I compared it with
**Blender importing the same `.glb`** (see [Checking against Blender](#checking-against-blender)):

- **Base color** (textures, atlas transforms, wrap modes) matches Blender's
  importer: mean linear difference 0.005. The differences are edge
  antialiasing, texture noise at 16 spp, and one mesh with no material.
- **Light units** match Cycles to within 1% (direct lighting on a diffuse wall).
- **Lit render:** brightness and color balance match Blender's render of the
  `.glb`. The canopy is bright cyan-white and lights the room. The remaining
  difference is Blender's glossy floor: the file has specular, clearcoat and
  IOR extensions, and this renderer is diffuse-only on the GPU.

The fix was a better export, not renderer work: see the next section.

## Baking the .blend for glTF (`tools/blender_bake_for_gltf.py`)

**Baking** makes Cycles evaluate each material's full node graph (masks,
decals, tints) and write the result into a plain texture. glTF only stores one
base-color and one emission image per material, so this is how a node-heavy
`.blend` survives export.

```
blender -b "Hideout (The Cradle).blend" --python tools/blender_bake_for_gltf.py -- ^
    glb/hideout_marathon_baked.glb [textures_dir] [--blend baked_copy.blend] [--scale 0.125]
```

The original `.blend` is never saved. Output goes to the `.glb`, the optional
texture folder, and the optional baked `.blend` copy. `--scale 0.125` makes a
quick low-res test (97 s instead of 7 min).

What it does:

1. Drops volume-only objects (fog boxes would export as opaque cubes) and
   applies modifiers into single-user meshes.
2. Groups objects by material. Big materials (>300 m²) are split over 2–4
   atlases. Atlas size follows surface area (512² to 8192²).
3. Makes a new non-overlapping UV map (`BakeUV`, smart project). Islands near
   the camera get up to 3× texel density, ones behind it less.
4. **Joins each group into one object.** Blender bakes every selected object in
   its own Cycles pass with a full scene sync (~3 s each), so a 228-object
   group took 12 minutes before this; now it's one call.
5. Bakes `DIFFUSE` color (8 samples), and `EMIT` if a 128² probe finds any
   emission. Emission is stored as an sRGB image plus a strength (the 99.95th
   percentile). Only the group being baked is renderable during its bake.
5b. **Transparency.** The decals are cut-outs. Inside the shared shader
   groups, a Transparent BSDF is mixed in by a thresholded texture channel.
   Cycles has no alpha bake, so the script rewires every node tree so that
   only transparency emits:
   - Transparent BSDF becomes white Emission;
   - a Principled with Alpha < 1 gets mixed with white Emission;
   - all other emission is zeroed.

   An `EMIT` bake then gives 1 − alpha, which goes into the color texture's
   alpha channel. The color is un-premultiplied, because the diffuse bake
   darkens partly transparent texels. Groups that probe fully opaque keep no
   alpha. On the hideout, 18 of the 32 groups have transparency.
5c. **Area lights.** glTF has none, so the exporter drops them. The script
   writes each visible one into the glTF scene's `extras` as
   `"pt_area_lights"`: 15 floats per light, which are center, two edges,
   emission direction and radiance, in Y-up world space. Radiance comes from
   Cycles' power as L = P / (π·A) × color. Disks and ellipses become
   rectangles of equal area. The loader turns them into the renderer's
   existing area lights. On the hideout these are the cyan fill panels.
   Without them the render lost most of its blue-green tint.
6. Swaps in plain Principled materials using those images, keeps `BakeUV` as
   the only UV map, and exports with cameras and lights. Textures with alpha
   are wired to Alpha, so the exporter writes them as PNG with
   `alphaMode: BLEND`. Everything else stays JPEG.

Hideout result (`glb/hideout_marathon_baked.glb`, 159 MB, 32 meshes,
766k triangles):

- The decals are back: the floor tile grids and text
  ("FRAME 0000198523 LOCK", "N73", "EB-790°E"), `B3.R` / `RLX` labels, pipe and
  wall logos all match the reference.
- **GPU memory is 2.85 GB**, almost all of it the 37 textures (2.79 GB as
  RGBA8). Fine on a 16 GB card. `--scale 0.5` would quarter it.
- Render: 1504×800, 256 spp, depth 6 in 78 s.
- With transparency baked: the floor decal planes (`80A7642A`–`F`, the large
  `ƎUЯ` / `oodp` logos) are cut out over the corrugated floor. Before, they
  were dark rectangles. The ceiling panels (`80A760FF`) are about 80% cut out
  and show the lit canopy. Before, they were black and white stripes.
- Still different from the reference: no fog, no AgX color grade (so less
  teal), and no glossy floor.

`glb/hideout_marathon_baked_lowres.glb` is the `--scale 0.125` test bake
(50 MB).

Command for a presentable image:

```
cd build
./pathtracer.exe -f ../out/hideout_baked.png -r 1504 800 -s 512 -m 6 -x 3 -T ../glb/hideout_marathon_baked.glb
```

## How it works

```
scene.glb
  │ cgltf parses JSON + buffers; stb_image decodes PNG/JPEG (in parallel)
  ▼
GLTF::load()                          src/scene/gltf/gltf_loader.cpp
  │ TRIMESH / CAMERA / LIGHT nodes + a TextureSet, all in Collada::SceneInfo
  ▼
Application::load()                   src/application/application.cpp
  │ TRIMESH → GLScene::TriMesh: bakes the world transform into one
  │           shared, immutable SceneObjects::MeshData (OpenGL preview reads it)
  ▼
GLTF::release()                       frees the parsed object-space copy
  ▼
get_static_scene()  (R key, or -f)    SceneObjects::Mesh *shares* the MeshData
  ▼
GPUScene::upload()                    src/pathtracer/gpu_scene.cu
  │ appends each mesh's vertex arrays, emits 16-byte indexed triangles,
  │ builds the BVH, uploads; textures upload once and stay on the GPU
  ▼
render_kernel                         src/pathtracer/gpu_render.cu
    traversal → finish_hit (normals, UVs) → shade_hit (textures) → lights
```

### Meshes

COLLADA meshes go through `GLScene::Mesh`, which builds a half-edge mesh for
MeshEdit. `HalfedgeMesh::build()` calls `exit(1)` on non-manifold edges,
inconsistent winding, or repeated vertices, which real scenes almost always
have. glTF meshes skip it:

- **[`TriMeshInfo`](../collada/trimesh_info.h)** (`Instance::TRIMESH`) holds
  object-space positions, normals, `TEXCOORD_0` and indices. There's one per
  glTF primitive, and instanced meshes share it.
- **[`GLScene::TriMesh`](../gl_scene/tri_mesh.cpp)** bakes the node transform
  into a world-space `MeshData`. Normals use the inverse transpose, and missing
  normals are computed as smooth, area-weighted vertex normals. It draws the
  preview with vertex arrays and can't be edited.
- **[`SceneObjects::Mesh`](../object.h)** shares that same `MeshData` through
  a `shared_ptr`, so vertices exist once on the host. The half-edge path now
  builds a `MeshData` too (that also fixed a leak of `new[]` arrays).

### Memory optimization

| | Before | After |
|---|---|---|
| GPU triangle | 80 B (6 `Vector3D` + type + BSDF id) | **16 B** (3 vertex indices + BSDF id) |
| GPU vertices | none (copied into every triangle) | 32 B each: position, normal, UV (UV only if some mesh has UVs) |
| Host copies of the mesh | `TriMesh` arrays + `SceneObjects::Mesh` copy (8-byte indices) | one shared `MeshData` |
| Host, GPU upload | a heap `Triangle` (~130 B) + `BBox` per triangle | none: reads the mesh arrays directly |
| Host, CPU BVH (`BVHAccel`) | always built, with another `Triangle` per triangle | built on first use only (BVH visualizer, autofocus) |
| BVH build | new `left` / `right` vectors at every node | in-place `std::partition` |
| Textures | n/a | only images used by base color / emission are decoded; host copy freed after upload; stay on GPU across re-renders |

Measured on `big.glb` (3.86M triangles): peak host RAM **2.2 GB → 556 MB**, run
time **6.8 s → 3.05 s**, GPU geometry **387 → 195 MiB** (triangles 294 → 59 MiB,
plus 44 MiB of vertices; the BVH is unchanged at 92 MiB). On the hideout,
skipping unused images halves texture memory (11 images would be 539 MB;
the 7 used are 258 MB).

Instances are still flattened (each instance has its own world-space copy of
its vertices). On the hideout that's 107k unique vertices becoming 753k, which
is only 23 MB.

### Textures

- **Decoding:** [stb_image](https://github.com/nothings/stb) handles PNG and
  JPEG, from the `.glb` buffer, a `data:` URI, or a file next to a `.gltf`.
  Images decode in parallel, one per core, and only images that some
  base-color or emissive slot uses are decoded. A failed decode falls back
  to white with a warning.
- **Storage:** one flat array of packed RGBA8 texels on the GPU, plus a
  `GPUTexture` table (offset, size, wrap mode) and a per-material `GPUTexRef`
  (texture index and a 2×3 UV matrix).
- **`KHR_texture_transform`:** stored as `uv' = T·R·S·uv` with offset,
  rotation and scale. The hideout uses offset and scale to pick regions of
  4096² atlases. Rotation follows the spec's matrix but no test file uses it.
- **Sampling:** bilinear, in linear space. sRGB is decoded with a 256-entry
  table in constant memory. Wrap modes are repeat, clamp and mirror, taken
  from the glTF sampler. There are no mipmaps: many samples per pixel already
  average the texture over each pixel.
- **Slots:** base color multiplies the reflectance, and emissive multiplies the
  emission. Metallic/roughness, normal, occlusion and clearcoat textures are
  not used (the GPU only has diffuse).

### Materials

The first matching rule wins:

| glTF material | BSDF |
|---|---|
| emission > 0 with no base-color or emissive texture | `EmissionBSDF` |
| `KHR_materials_transmission` > 0.5 | `GlassBSDF` (renders black on the GPU) |
| metallic ≥ 0.5, roughness ≤ 0.1, untextured | `MirrorBSDF` (renders black on the GPU) |
| everything else | `DiffuseBSDF(baseColor)` × base color texture |

A material that emits *and* has textures (screens, signs) becomes a diffuse
surface that also emits: `gpu_emission`, multiplied by the emissive texture.
Emission from glTF materials also counts when a bounce ray hits it, so
emissive surfaces light the scene. This is the per-material flag
`emit_indirect`. COLLADA emitters don't set it, because the CS184 scenes put
an area light on top of their emissive quads and counting both would double
the light.

**Alpha** (`alphaMode` `MASK` / `BLEND`) is tested while the BVH is traversed:
`alpha_skips_hit` in [gpu_intersect.cuh](../../pathtracer/gpu_intersect.cuh).
Coverage is the base color texture's alpha × `baseColorFactor.a`. `MASK`
skips the hit below `alphaCutoff`. `BLEND` skips it with probability
1 − alpha, using a hash of the ray and the triangle as the random number, so
the closest-hit and shadow queries for one ray agree. Camera, bounce and
shadow rays all see through cut-outs, so decals cast correct shadows. Opaque
materials pay one extra BSDF read per candidate hit, plus the texture lookup
only when the material has alpha.

### Lights

- **Units:** glTF intensities are photometric (candela for point and spot,
  lux for sun). The loader divides by **683 lm/W**, the same conversion
  Blender's importer uses, so they line up with emission strengths. I checked
  this against Cycles: direct lighting on a diffuse wall agrees to within 1%.
- **Falloff:** point and spot lights fall off as 1/d² and fade to zero at the
  glTF `range` (the spec's recommended window). COLLADA lights keep the old
  no-falloff behavior (`inverse_square` is off by default).
- **Spot lights** are now implemented on CPU and GPU (`GPU_LIGHT_SPOT`), with
  glTF's smooth falloff between the inner and outer cone angles. Before, they
  were stubs.
- **Transforms:** the loader computes world-space position and direction and
  gives the node an identity transform, because the GL light wrappers apply
  the node transform to direction vectors with w = 1, which would add the
  translation.

### Camera

The first perspective camera is used, and the view starts exactly at its pose.
`Application::resize()` now keeps the *file's* field of view at any `-r` or
window size (before, a smaller `-r` zoomed in). COLLADA scenes keep the old
behavior. With no camera in the file you get the usual bounding-box orbit.

## Renderer fixes found along the way (affect all scenes)

- **Double-sided surfaces:** the hit's shading and geometric normals are
  flipped to face the incoming ray. Shadow and bounce rays start along the
  geometric normal, and directions below the real surface are rejected. Before,
  surfaces seen from behind got no direct light, and light leaked through thin
  walls.
- **Black pixels (NaN):** `curand_uniform` can return exactly 1.0, which gives
  a cosine sample with pdf 0, and 0/0 put a NaN into the pixel. Those paths
  now stop.
- **Adaptive sampling stuck at black:** a pixel whose first 32 samples were
  all zero passed the convergence test (0 ≤ 0). All-zero pixels now use the
  rule of three: stop once 3/n ≤ tolerance, i.e. 60 samples at the default
  0.05. Truly black pixels still stop early, so render time is unchanged.
- **COLLADA output is unchanged:** `CBbunny.dae` differs from the old render
  by 0.05/255 on average.

## Files

| File | What |
|---|---|
| [cgltf.h](cgltf.h) | [cgltf](https://github.com/jkuhlmann/cgltf) v1.14 (MIT), unmodified |
| [stb_image.h](stb_image.h) | [stb_image](https://github.com/nothings/stb) v2.34 (public domain / MIT), unmodified |
| [gltf_loader.cpp](gltf_loader.cpp) | loader: meshes, UVs, materials, textures, lights, camera |
| [../texture.h](../texture.h) | `TextureSet` / `TextureImage` / `TextureSampler` |
| [../object.h](../object.h) | `MeshData`, shared by `GLScene::TriMesh` and `SceneObjects::Mesh` |
| [../gl_scene/tri_mesh.cpp](../gl_scene/tri_mesh.cpp) | render-only GL mesh |
| [../../pathtracer/gpu_scene.h](../../pathtracer/gpu_scene.h) / [.cu](../../pathtracer/gpu_scene.cu) | indexed GPU layout, texture tables, upload, BVH builder |
| [../../pathtracer/gpu_intersect.cuh](../../pathtracer/gpu_intersect.cuh) | indexed triangle test, `finish_hit` (normals, UVs, double-sided) |
| [../../pathtracer/gpu_render.cu](../../pathtracer/gpu_render.cu) | texture sampling, `shade_hit`, spot / falloff lights, NaN + adaptive fixes |
| [../light.cpp](../light.cpp) | point falloff / range, spot light |
| [../../pathtracer/raytraced_renderer.cpp](../../pathtracer/raytraced_renderer.cpp) | lazy CPU BVH, exposure / filmic, debug albedo switch |
| [../../util/image.h](../../util/image.h) | exposure scale and ACES curve in `toColor` |
| [../../application/](../../application/) | `-x`, `-T`, loader choice, file-FOV resize, textures to the scene |
| [../../../tools/make_test_gltf.py](../../../tools/make_test_gltf.py) | synthetic test scenes |
| [../../../tools/blender_glb_albedo.py](../../../tools/blender_glb_albedo.py) | Blender base-color render for comparison |
| [../../../tools/blender_glb_direct.py](../../../tools/blender_glb_direct.py) | Blender light-unit check |
| [../../../tools/blender_bake_for_gltf.py](../../../tools/blender_bake_for_gltf.py) | bakes a `.blend`'s materials to textures and exports a `.glb` |

`Primitive::createGPU` is gone: meshes upload from their arrays, and spheres
use `Sphere::createGPU(GPUSphere_t*)`. Sphere *intersection* on the GPU is
still the existing TODO (`intersect_sphere`, which now gets `scene.spheres`).

## Testing

Synthetic scenes (`python tools/make_test_gltf.py dae/sky/CBbunny.dae out/gltf_test`):

| Scene | Checks | Result |
|---|---|---|
| `texture_test.glb` | UV orientation, `KHR_texture_transform`, clamp, repeat | all four quads as documented in the script; matches Blender's importer |
| `cornell.glb` / `.gltf` | hierarchy, instancing, missing normals, uint16/32 and non-indexed, emission, point light | correct; matches Cycles direct lighting to within 1% |
| `sun_test.glb` | sun direction, `matrix` transforms | shadow along the sun direction |
| `nocam.glb` | bbox-orbit fallback, spot light | correct; an isolated spot test gives a lit radius of 0.91 m (expected 0.93 m) |
| `big.glb` | 3.86M triangles | 3.05 s end to end, 556 MB host peak |
| `corrupt.glb`, missing file | errors | clear message, no crash |
| COLLADA `CBbunny.dae`, `CBspheres_lambertian.dae` | unchanged path | `CBbunny` identical to before; `CBspheres` still has no spheres (GPU TODO) |

The hideout was checked against Blender importing the same `.glb`, as
described above.

### Checking against Blender

```
blender -b --factory-startup --python tools/blender_glb_albedo.py -- scene.glb blender.png 1128 600
PT_DEBUG_ALBEDO=1 build/pathtracer.exe -f ours.png -r 1128 600 -s 16 -m 1 -x -0.5 scene.glb
```

Blender is at `C:\Program Files (x86)\Steam\steamapps\common\Blender\blender.exe`
on this machine.

## Not supported (yet)

- metallic/roughness, normal, occlusion, clearcoat, specular textures and
  glossy materials (the GPU is diffuse-only; mirror and glass render black)
- partial transparency as a blend: `BLEND` is a stochastic cut-out (like
  Cycles' dithered mode), so it's right on average but there's no refraction
  or tinting. Vertex colors (`COLOR_0`: the loader warns only if
  they aren't all white), `TEXCOORD_1+`
- WebP / KTX2 images, Draco / meshopt compression (error), skinning, morph
  targets, animation, `EXT_mesh_gpu_instancing`, orthographic cameras
- emissive meshes are hit-only light sources (no light sampling on them), so
  small emitters are noisy

## Next steps

1. **Glossy BSDF on the GPU** (microfacet with the glTF roughness / metallic,
   ideally textured). This is the biggest visible gap on the hideout: the
   floor reflections.
2. **Light sampling for emissive triangles**, so small emitters converge.
3. **Instancing with a two-level BVH**, so instances stop being flattened.
4. **SAH BVH build** (currently a midpoint split).
5. **Compressed textures** (BC7 or half-res mips) if the 2.8 GB of baked
   hideout textures becomes a problem.
