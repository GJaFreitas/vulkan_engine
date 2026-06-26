#include "tiny_gltf.h"
#include <cglm/cglm.h>
#include <iostream>
#include <vector>
#include <cstddef>
#include <ktx.h>

enum Extension
{
	GLTF,
	GLB,
	NOT_SUPPORTED
};

static inline enum Extension	get_extension(char *viewer)
{
	const char	*gltf_str = "gltf";
	const char	*glb_str = "glb";
	int		i = 0;

	while (i < 3 && viewer[i] == glb_str[i]) {
		i++;
	}
	if (i == 3)
		return GLB;
	i = 0;
	while (i < 4 && viewer[i] == gltf_str[i]) {
		i++;
	}
	if (i == 4)
		return GLTF;
	return NOT_SUPPORTED;
}


void	loadModel(const char *model_path)
{
	tinygltf::Model		gltf_model;
	tinygltf::TinyGLTF	loader;
	std::string		err, warn, path;

	path = std::string(model_path);

	bool	ret = false;

	char	*path_checker = (char *)model_path;
	// path_checker is the dot
	while (*(path_checker++) != '.');
	// path_checker is after the dot
	path_checker++;
	enum Extension	ext = get_extension(path_checker);
	if (ext == GLTF) {
		ret = loader.LoadASCIIFromFile(&gltf_model, &err, &warn, path);
	} else if (ext == GLB) {
		ret = loader.LoadBinaryFromFile(&gltf_model, &err, &warn, path);
	} else {
		err = "Unsuported file extension";
	}

	if (!warn.empty()) {
		std::cout << "glTF warning: " + warn << std::endl;
	}
	if (!err.empty()) {
		std::cout << "glTF error: " + err << std::endl;
	}
	if (!warn.empty()) {
		std::cout << "glTF warning: " + warn << std::endl;
	}

	gltf_model = tinygltf::Model();

	std::vector<tinygltf::Texture> textures;
	for (size_t i = 0; i < gltf_model.textures.size(); i++) {
		const auto&	texture = gltf_model.textures[i];
		const auto&	image = gltf_model.images[texture.source];

		tinygltf::Texture tex;
		tex.name = image.name.empty() ? "texture_" + std::to_string(i) : image.name;

		if (image.mimeType == "image/ktx2" && image.bufferView >= 0) {
			auto&	buffer_view = gltf_model.bufferViews[image.bufferView];
			auto&	buffer = gltf_model.buffers[buffer_view.buffer];

			const uint8_t*	ktx2_data = buffer.data.data() + buffer_view.byteOffset;
			size_t	ktx2_size = buffer_view.byteLength;

			ktxTexture2	*ktx_texture = nullptr;
			KTX_error_code	result = ktxTexture2_CreateFromMemory(
				ktx2_data, ktx2_size,
				KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
				&ktx_texture
			);

			if (result != KTX_SUCCESS) {
				std::cerr << "Failed to load KTX2 texture: " << ktxErrorString(result) << std::endl;
				continue;
			}

			if (ktx_texture->isCompressed && ktxTexture2_NeedsTranscoding(ktx_texture)) {
				// TODO: Check device support for different formats! 26/06/26
				ktx_transcode_fmt_e	transcode_fmt = KTX_TTF_ASTC_4x4_RGBA;

				result = ktxTexture2_TranscodeBasis(ktx_texture, transcode_fmt, 0);
				if (result != KTX_SUCCESS) {
					std::cerr << "Failed to transcode KTX2 texture: " << ktxErrorString(result) << std::endl;
					ktxTexture2_Destroy(ktx_texture);
					continue;
				}
			}
		}
	}
}
