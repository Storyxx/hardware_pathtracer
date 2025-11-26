#pragma once

#include <auto_vk_toolkit.hpp>
#include <image_data.hpp>
#include <vk_convenience_functions.hpp>

/// <summary>
/// Kombined magic of `avk::image_data_interface` and `avk::image_data_stb`, 
/// the code is mostly copied from there.
/// </summary>
class conpressed_image_data : public avk::image_data
{
public:

	explicit conpressed_image_data(aiTexture *aTexture, const bool aLoadHdrIfPossible = false, const bool aLoadSrgbIfApplicable = false, const bool aFlip = false, const int aPreferredNumberOfTextureComponents = 4)
		: image_data("", aLoadHdrIfPossible, aLoadSrgbIfApplicable, aFlip, aPreferredNumberOfTextureComponents)
		, mTexture(aTexture)
		, mData(nullptr, &deleter)
		, mExtent(0, 0, 0)
		, mChannelsInFile(0)
		, mSizeofPixelPerChannel(0)
		, mFormat(vk::Format::eUndefined)
	{
	}


	void load()
	{
		if (!empty())
		{
			return;
		}

		stbi_set_flip_vertically_on_load(mFlip);

		int width, height, channels;
		stbi_uc *data = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(mTexture->pcData), mTexture->mWidth, &width, &height, &channels, mPreferredNumberOfTextureComponents);
		if (data == nullptr) {
			throw std::runtime_error("Could not load compressed texture\n");
		}
		
		mData = std::unique_ptr<void, decltype(&deleter)>(data, &deleter);
		mSizeofPixelPerChannel = sizeof(stbi_uc);
		mExtent = vk::Extent3D(width, height, 1);
		mFormat = select_format(mPreferredNumberOfTextureComponents, mLoadHdrIfPossible, mLoadSrgbIfApplicable);
	}

	vk::Format get_format() const
	{
		return mFormat;
	};

	vk::ImageType target() const
	{
		return vk::ImageType::e2D;
	}

	extent_type extent(const uint32_t level = 0) const
	{
		assert(level == 0);

		return mExtent;
	};

	void *get_data(const uint32_t layer, const uint32_t face, const uint32_t level)
	{
		assert(layer == 0 && face == 0 && level == 0);

		return mData.get();
	};

	size_t size() const
	{
		return mExtent.width * mExtent.height * mPreferredNumberOfTextureComponents * mSizeofPixelPerChannel;
	}

	size_t size(const uint32_t level) const
	{
		assert(level == 0);

		return size();
	}

	bool empty() const
	{
		return mData == nullptr;
	};

	uint32_t levels() const
	{
		return 1;
	}

	uint32_t layers() const
	{
		return 1;
	};

	uint32_t faces() const
	{
		return 1;
	}

	bool is_hdr() const
	{
		return false;
	}

	bool can_flip() const
	{
		return true;
	}

private:

	/** Select Vulkan image format based on preferred number of texture components, HDR, and sRGB requirements
	* @param aPreferredNumberOfTextureComponents the preferred number of texture components, must be 1, 2, 3, or 4.
	* @param aLoadHdrIfPossible		load the texture as HDR (high dynamic range) data, if supported by the image loading library. If set to true, the image data may be returned in a HDR format even if the texture file does not contain HDR data. If set to false, the image data may be returned in an LDR format even if the texture contains HDR data. It is therefore advised to set this parameter according to the data format of the texture file.
	* @param aLoadSrgbIfApplicable	load the texture as sRGB color-corrected data, if supported by the image loading library. If set to true, the image data may be returned in an sRGB format even if the texture file does not contain sRGB data. If set to false, the image data may be returned in a plain RGB format even if the texture contains sRGB data. It is therefore advised to set this parameter according to the color space of the texture file.
	* @return the stbi internal constant that represents the given number of texture components
	*/
	static vk::Format select_format(const int aPreferredNumberOfTextureComponents, const bool aLoadHdrIfPossible, const bool aLoadSrgbIfApplicable)
	{
		vk::Format imFmt = vk::Format::eUndefined;

		if (aLoadHdrIfPossible) {
			switch (aPreferredNumberOfTextureComponents) {
			case 4:
			default:
				imFmt = avk::default_rgb16f_4comp_format();
				break;
				// Attention: There's a high likelihood that your GPU does not support formats with less than four color components!
			case 3:
				imFmt = avk::default_rgb16f_3comp_format();
				break;
			case 2:
				imFmt = avk::default_rgb16f_2comp_format();
				break;
			case 1:
				imFmt = avk::default_rgb16f_1comp_format();
				break;
			}
		}
		else if (aLoadSrgbIfApplicable) {
			switch (aPreferredNumberOfTextureComponents) {
			case 4:
			default:
				imFmt = avk::default_srgb_4comp_format();
				break;
				// Attention: There's a high likelihood that your GPU does not support formats with less than four color components!
			case 3:
				imFmt = avk::default_srgb_3comp_format();
				break;
			case 2:
				imFmt = avk::default_srgb_2comp_format();
				break;
			case 1:
				imFmt = avk::default_srgb_1comp_format();
				break;
			}
		}
		else {
			switch (aPreferredNumberOfTextureComponents) {
			case 4:
			default:
				imFmt = avk::default_rgb8_4comp_format();
				break;
				// Attention: There's a high likelihood that your GPU does not support formats with less than four color components!
			case 3:
				imFmt = avk::default_rgb8_3comp_format();
				break;
			case 2:
				imFmt = avk::default_rgb8_2comp_format();
				break;
			case 1:
				imFmt = avk::default_rgb8_1comp_format();
				break;
			}
		}

		return imFmt;
	}

	// stb_image uses malloc() for memory allocation, therefore it should be deallocated with free()
	static void deleter(void *data) {
		stbi_image_free(data);
	};

	aiTexture* mTexture;

	std::unique_ptr<void, decltype(&deleter)> mData;

	vk::Extent3D mExtent;
	int mChannelsInFile;
	size_t mSizeofPixelPerChannel;
	vk::Format mFormat;

};
