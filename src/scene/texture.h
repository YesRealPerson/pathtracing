#ifndef CGL_SCENE_TEXTURE_H
#define CGL_SCENE_TEXTURE_H

#include <cstdint>
#include <string>
#include <vector>

namespace CGL {

// Wrap modes, matching glTF / OpenGL sampler semantics.
enum TextureWrap : int {
  TEX_WRAP_REPEAT = 0,
  TEX_WRAP_CLAMP  = 1,
  TEX_WRAP_MIRROR = 2,
};

/**
 * A decoded 8-bit RGBA image. Texel (x, y) is rgba[(y * w + x)], packed as
 * 0xAABBGGRR (little-endian bytes R, G, B, A). Row 0 is the top of the image,
 * which is where glTF puts uv = (0, 0).
 */
struct TextureImage {
  std::string name;
  int w = 0, h = 0;
  std::vector<uint32_t> rgba;
};

/**
 * A texture as materials reference it: an image plus how to wrap it. Several
 * textures can share one image (glTF does this a lot).
 */
struct TextureSampler {
  int image = -1;
  int wrap_s = TEX_WRAP_REPEAT;
  int wrap_t = TEX_WRAP_REPEAT;
};

/**
 * All textures of a scene. Produced by the glTF loader, carried by the scene,
 * uploaded to the GPU by GPUScene. After the GPU upload the texel data is
 * released (see GPUScene::upload_textures), so `images[i].rgba` may be empty
 * while `w` / `h` stay valid.
 */
struct TextureSet {
  std::vector<TextureImage> images;
  std::vector<TextureSampler> textures;
};

} // namespace CGL

#endif // CGL_SCENE_TEXTURE_H
