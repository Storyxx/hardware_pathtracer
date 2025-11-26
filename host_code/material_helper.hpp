#pragma once

#include "compressed_image_data.hpp"

#include <auto_vk_toolkit.hpp>
#include <material_image_helpers.hpp>
#include <material_config.hpp>


/// <summary>
/// Effectively identical to `convert_for_gpu_usage_cached` from `material_image_helpers.hpp` just wrapped in a class.
/// The notable change is the use of the `compressed_image_data` to use the images already loaded from assimp.
/// Every other change is just cutting down complexity of the function, e.g. removing the serializer.
/// </summary>
class material_helper {

  public:

	static std::tuple<std::vector<avk::material_gpu_data>, std::vector<avk::image_sampler>, avk::command::action_type_command> convert_for_gpu_usage(
		  const aiScene *scene,
		  std::vector<avk::material_config> &allMatConfigs,
		  size_t materialIndexOffset
	) {

		avk::image_usage imageUsage = avk::image_usage::general_texture;
		avk::filter_mode textureFilterMode = avk::filter_mode::trilinear;

		avk::command::action_type_command commandsToReturn{};

		// These are the texture names loaded from file -> mapped to vector of usage-pointers
		std::unordered_map<std::string, std::vector<std::tuple<std::array<avk::border_handling_mode, 2>, std::vector<int *>>>> texNamesToBorderHandlingToUsages;

		auto addTexUsage = [&texNamesToBorderHandlingToUsages](const std::string &bPath, const std::array<avk::border_handling_mode, 2> &bBhMode, int *bUsage) {
			auto &vct = texNamesToBorderHandlingToUsages[bPath];
			for (auto &[existingBhModes, usages] : vct) {
				if (existingBhModes[0] == bBhMode[0] && existingBhModes[1] == bBhMode[1]) {
					usages.push_back(bUsage);
					return; // Found => done
				}
			}
			// Not found => add new
			vct.emplace_back(bBhMode, std::vector<int *>{ bUsage });
			};

		// However, if some textures are missing, provide 1x1 px textures in those spots
		std::vector<int *> whiteTexUsages;				// Provide a 1x1 px almost everywhere in those cases,
		std::vector<int *> straightUpNormalTexUsages;	// except for normal maps, provide a normal pointing straight up there.

		std::vector<avk::material_gpu_data> result;
		
		size_t materialConfigSize = allMatConfigs.size();
		result.reserve(materialConfigSize); // important because of the pointers

		for (auto &mc : allMatConfigs) {
			auto &newEntry = result.emplace_back();
			avk::material_gpu_data &mgd = newEntry;
			mgd.mDiffuseReflectivity = mc.mDiffuseReflectivity;
			mgd.mAmbientReflectivity = mc.mAmbientReflectivity;
			mgd.mSpecularReflectivity = mc.mSpecularReflectivity;
			mgd.mEmissiveColor = mc.mEmissiveColor;
			mgd.mTransparentColor = mc.mTransparentColor;
			mgd.mReflectiveColor = mc.mReflectiveColor;
			mgd.mAlbedo = mc.mAlbedo;

			mgd.mOpacity = mc.mOpacity;
			mgd.mBumpScaling = mc.mBumpScaling;
			mgd.mShininess = mc.mShininess;
			mgd.mShininessStrength = mc.mShininessStrength;

			mgd.mRefractionIndex = mc.mRefractionIndex;
			mgd.mReflectivity = mc.mReflectivity;
			mgd.mMetallic = mc.mMetallic;
			mgd.mSmoothness = mc.mSmoothness;

			mgd.mTransmission = mc.mTransmission;

			mgd.mSheen = mc.mSheen;
			mgd.mThickness = mc.mThickness;
			mgd.mRoughness = mc.mRoughness;
			mgd.mAnisotropy = mc.mAnisotropy;

			mgd.mAnisotropyRotation = mc.mAnisotropyRotation;
			mgd.mCustomData = mc.mCustomData;

			mgd.mDiffuseTexIndex = -1;
			if (mc.mDiffuseTex.empty()) {
				whiteTexUsages.push_back(&mgd.mDiffuseTexIndex);
			}
			else {
				auto path = avk::clean_up_path(mc.mDiffuseTex);
				addTexUsage(path, mc.mDiffuseTexBorderHandlingMode, &mgd.mDiffuseTexIndex);
			}

			mgd.mSpecularTexIndex = -1;
			if (mc.mSpecularTex.empty()) {
				whiteTexUsages.push_back(&mgd.mSpecularTexIndex);
			}
			else {
				addTexUsage(avk::clean_up_path(mc.mSpecularTex), mc.mSpecularTexBorderHandlingMode, &mgd.mSpecularTexIndex);
			}

			mgd.mAmbientTexIndex = -1;
			if (mc.mAmbientTex.empty()) {
				whiteTexUsages.push_back(&mgd.mAmbientTexIndex);
			}
			else {
				auto path = avk::clean_up_path(mc.mAmbientTex);
				addTexUsage(path, mc.mAmbientTexBorderHandlingMode, &mgd.mAmbientTexIndex);
			}

			mgd.mEmissiveTexIndex = -1;
			if (mc.mEmissiveTex.empty()) {
				whiteTexUsages.push_back(&mgd.mEmissiveTexIndex);
			}
			else {
				addTexUsage(avk::clean_up_path(mc.mEmissiveTex), mc.mEmissiveTexBorderHandlingMode, &mgd.mEmissiveTexIndex);
			}

			mgd.mHeightTexIndex = -1;
			if (mc.mHeightTex.empty()) {
				whiteTexUsages.push_back(&mgd.mHeightTexIndex);
			}
			else {
				addTexUsage(avk::clean_up_path(mc.mHeightTex), mc.mHeightTexBorderHandlingMode, &mgd.mHeightTexIndex);
			}

			mgd.mNormalsTexIndex = -1;
			if (mc.mNormalsTex.empty()) {
				straightUpNormalTexUsages.push_back(&mgd.mNormalsTexIndex);
			}
			else {
				addTexUsage(avk::clean_up_path(mc.mNormalsTex), mc.mNormalsTexBorderHandlingMode, &mgd.mNormalsTexIndex);
			}

			mgd.mShininessTexIndex = -1;
			if (mc.mShininessTex.empty()) {
				whiteTexUsages.push_back(&mgd.mShininessTexIndex);
			}
			else {
				addTexUsage(avk::clean_up_path(mc.mShininessTex), mc.mShininessTexBorderHandlingMode, &mgd.mShininessTexIndex);
			}

			mgd.mOpacityTexIndex = -1;
			if (mc.mOpacityTex.empty()) {
				whiteTexUsages.push_back(&mgd.mOpacityTexIndex);
			}
			else {
				addTexUsage(avk::clean_up_path(mc.mOpacityTex), mc.mOpacityTexBorderHandlingMode, &mgd.mOpacityTexIndex);
			}

			mgd.mDisplacementTexIndex = -1;
			if (mc.mDisplacementTex.empty()) {
				whiteTexUsages.push_back(&mgd.mDisplacementTexIndex);
			}
			else {
				addTexUsage(avk::clean_up_path(mc.mDisplacementTex), mc.mDisplacementTexBorderHandlingMode, &mgd.mDisplacementTexIndex);
			}

			mgd.mReflectionTexIndex = -1;
			if (mc.mReflectionTex.empty()) {
				whiteTexUsages.push_back(&mgd.mReflectionTexIndex);
			}
			else {
				addTexUsage(avk::clean_up_path(mc.mReflectionTex), mc.mReflectionTexBorderHandlingMode, &mgd.mReflectionTexIndex);
			}

			mgd.mLightmapTexIndex = -1;
			if (mc.mLightmapTex.empty()) {
				whiteTexUsages.push_back(&mgd.mLightmapTexIndex);
			}
			else {
				addTexUsage(avk::clean_up_path(mc.mLightmapTex), mc.mLightmapTexBorderHandlingMode, &mgd.mLightmapTexIndex);
			}

			mgd.mExtraTexIndex = -1;
			if (mc.mExtraTex.empty()) {
				whiteTexUsages.push_back(&mgd.mExtraTexIndex);
			}
			else {
				auto path = avk::clean_up_path(mc.mExtraTex);
				addTexUsage(path, mc.mExtraTexBorderHandlingMode, &mgd.mExtraTexIndex);
			}

			mgd.mDiffuseTexOffsetTiling = mc.mDiffuseTexOffsetTiling;
			mgd.mSpecularTexOffsetTiling = mc.mSpecularTexOffsetTiling;
			mgd.mAmbientTexOffsetTiling = mc.mAmbientTexOffsetTiling;
			mgd.mEmissiveTexOffsetTiling = mc.mEmissiveTexOffsetTiling;
			mgd.mHeightTexOffsetTiling = mc.mHeightTexOffsetTiling;
			mgd.mNormalsTexOffsetTiling = mc.mNormalsTexOffsetTiling;
			mgd.mShininessTexOffsetTiling = mc.mShininessTexOffsetTiling;
			mgd.mOpacityTexOffsetTiling = mc.mOpacityTexOffsetTiling;
			mgd.mDisplacementTexOffsetTiling = mc.mDisplacementTexOffsetTiling;
			mgd.mReflectionTexOffsetTiling = mc.mReflectionTexOffsetTiling;
			mgd.mLightmapTexOffsetTiling = mc.mLightmapTexOffsetTiling;
			mgd.mExtraTexOffsetTiling = mc.mExtraTexOffsetTiling;
		}

		size_t numTexUsages = 0;
		for (const auto &entry : texNamesToBorderHandlingToUsages) {
			numTexUsages += entry.second.size();
		}

		size_t numWhiteTexUsages = whiteTexUsages.empty() ? 0 : 1;
		size_t numStraightUpNormalTexUsages = (straightUpNormalTexUsages.empty() ? 0 : 1);
		size_t numTexNamesToBorderHandlingToUsages = texNamesToBorderHandlingToUsages.size();
		auto numImageViews = numTexNamesToBorderHandlingToUsages + numWhiteTexUsages + numStraightUpNormalTexUsages;

		const auto numSamplers = numTexUsages + numWhiteTexUsages + numStraightUpNormalTexUsages;
		std::vector<avk::image_sampler> imageSamplers;
		imageSamplers.reserve(numSamplers);

		// Create the white texture and assign its index to all usages
		if (numWhiteTexUsages > 0) {
			auto [tex, cmds] = avk::create_1px_texture({255, 255, 255, 255}, avk::layout::shader_read_only_optimal, vk::Format::eR8G8B8A8Unorm, avk::memory_usage::device, imageUsage);
			commandsToReturn.mNestedCommandsAndSyncInstructions.push_back(std::move(cmds));
			auto imgView = avk::context().create_image_view(std::move(tex));
			avk::sampler smplr;

			smplr = avk::context().create_sampler(avk::filter_mode::nearest_neighbor, avk::border_handling_mode::repeat);
			
			imageSamplers.push_back(avk::context().create_image_sampler(std::move(imgView), std::move(smplr)));

			int index = static_cast<int>(imageSamplers.size() - 1);
			for (auto *img : whiteTexUsages) {
				*img = materialIndexOffset + index;
			}
		}

		// Create the normal texture, containing a normal pointing straight up, and assign to all usages
		if (numStraightUpNormalTexUsages > 0) {
			auto [tex, cmds] = avk::create_1px_texture({127, 127, 255, 0}, avk::layout::shader_read_only_optimal, vk::Format::eR8G8B8A8Unorm, avk::memory_usage::device, imageUsage);
			commandsToReturn.mNestedCommandsAndSyncInstructions.push_back(std::move(cmds));
			auto imgView = avk::context().create_image_view(std::move(tex));
			avk::sampler smplr;

			smplr = avk::context().create_sampler(avk::filter_mode::nearest_neighbor, avk::border_handling_mode::repeat);

			imageSamplers.push_back(avk::context().create_image_sampler(std::move(imgView), std::move(smplr)));

			// Assign this image_sampler's index wherever it is referenced:

			int index = static_cast<int>(imageSamplers.size() - 1);
			for (auto *img : straightUpNormalTexUsages) {
				*img = materialIndexOffset + index;
			}
		}

		// Load all the images from file, and assign them to all usages
		for (auto &pair : texNamesToBorderHandlingToUsages) {
			assert(!pair.first.empty());
			
			std::string path = pair.first;
			std::string delimiter = "/*";
			std::string pathNumber = path.substr(path.find(delimiter) + delimiter.length());
			unsigned int textureIndex = std::stoi(pathNumber);

			assert(textureIndex < scene->mNumTextures);

			aiTexture* compressedData = scene->mTextures[textureIndex];
			conpressed_image_data imageData(compressedData, false, false, true, 4);
			auto [tex, cmds] = avk::create_image_from_image_data_cached(imageData, avk::layout::shader_read_only_optimal, avk::memory_usage::device, imageUsage);

			commandsToReturn.mNestedCommandsAndSyncInstructions.push_back(std::move(cmds));
			auto imgView = avk::context().create_image_view(std::move(tex));
			assert(!pair.second.empty());

			// It is now possible that an image can be referenced from different samplers, which adds support for different
			// usages of an image, e.g. once it is used as a tiled texture, at a different place it is clamped to edge, etc.
			// If we are serializing, we need to store how many different samplers are referencing the image:
			auto numDifferentSamplers = static_cast<int>(pair.second.size());

			// There can be different border handling types specified for the textures
			for (auto &[bhModes, usages] : pair.second) {
				assert(!usages.empty());

				avk::sampler smplr;
				smplr = avk::context().create_sampler(textureFilterMode, bhModes);

				if (numDifferentSamplers > 1) {
					// If we indeed have different border handling modes, create multiple samplers and share the image view resource among them:
					imageSamplers.push_back(avk::context().create_image_sampler(imgView, std::move(smplr)));
				}
				else {
					// There is only one border handling mode:
					imageSamplers.push_back(avk::context().create_image_sampler(std::move(imgView), std::move(smplr)));
				}

				// Assign the texture usages:
				auto index = static_cast<int>(imageSamplers.size() - 1);
				for (auto *img : usages) {
					*img = materialIndexOffset + index;
				}
			}
		}

		// Hand over ownership to the caller
		return std::make_tuple(std::move(result), std::move(imageSamplers), std::move(commandsToReturn));
	}
};
