#pragma once

#include "camera_controller.h"

#include <auto_vk_toolkit.hpp>
#include <invokee.hpp>
#include <material_image_helpers.hpp>


class model_loader
{

public:
	struct data_for_draw_call
	{
		avk::buffer mPositionsBuffer;
		avk::buffer mTexCoordsBuffer;
		avk::buffer mNormalsBuffer;
		avk::buffer mTangentsBuffer;
		avk::buffer mBitangentsBuffer;
		avk::buffer mIndexBuffer;

		int mMaterialIndex;
	};


	model_loader(avk::queue* aQueue);

	void load_models_from_ini(std::string iniPath);

	void update_transform_for_model(size_t modelIndex, glm::mat4 newTransform);

	void update();

	// getters
	uint32_t max_number_of_geometry_instances() const { return static_cast<uint32_t>(mAllGeometryInstances.size()); }
	inline const std::vector<data_for_draw_call> &draw_calls() const { return mDrawCalls; }
	inline const avk::buffer &material_buffer() const { return mMaterialBuffer; }
	inline const avk::buffer &transforms_buffer() const { return mTransformsBuffer; };
	inline const std::vector<avk::image_sampler> &image_samplers() const { return mImageSamplers; }
	inline const std::vector<avk::combined_image_sampler_descriptor_info> &combined_image_sampler_descriptor_infos() const { return mCombinedImageSamplerDescriptorInfos; }
	inline const std::vector<avk::buffer_view> &position_buffer_views() const { return mPositionsBufferViews; }
	inline const std::vector<avk::buffer_view> &tex_coords_buffer_views() const { return mTexCoordsBufferViews; }
	inline const std::vector<avk::buffer_view> &normals_buffer_views() const { return mNormalsBufferViews; }
	inline const std::vector<avk::buffer_view> &tangents_buffer_views() const { return mTangentsBufferViews; }
	inline const std::vector<avk::buffer_view> &bitangents_buffer_views() const { return mBitangentsBufferViews; }
	inline const std::vector<avk::buffer_view> &index_buffer_views() const { return mIndexBufferViews; }
	inline const bool has_updated_geometry_for_tlas() const { return mTlasUpdateRequired; }
	
	const std::vector<avk::geometry_instance> get_active_geometry_instances_for_tlas_build();



private:
	std::tuple<std::vector<avk::material_gpu_data>, std::vector<avk::image_sampler>, avk::command::action_type_command> load_single_model(
		std::string filePath,
		size_t samplerIndexOffset,
		size_t materialIndexOffset,
		size_t modelIndex
	);

	avk::queue *mQueue;
	std::vector<data_for_draw_call> mDrawCalls;
	avk::buffer mMaterialBuffer;
	avk::buffer mTransformsBuffer;
	std::vector<avk::image_sampler> mImageSamplers;

	std::vector<avk::bottom_level_acceleration_structure> mBlas;

	std::vector<avk::combined_image_sampler_descriptor_info> mCombinedImageSamplerDescriptorInfos;

	std::vector<avk::buffer_view> mPositionsBufferViews;
	std::vector<avk::buffer_view> mTexCoordsBufferViews;
	std::vector<avk::buffer_view> mNormalsBufferViews;
	std::vector<avk::buffer_view> mTangentsBufferViews;
	std::vector<avk::buffer_view> mBitangentsBufferViews;
	std::vector<avk::buffer_view> mIndexBufferViews;

	std::vector<size_t> mMaterialModelMapping;
	std::vector<glm::mat4> mTransforms;

	std::vector<avk::geometry_instance> mAllGeometryInstances;
	std::vector<bool> mGeometryInstanceActive;
	bool mTlasUpdateRequired = true;
	std::vector<avk::geometry_instance> mActiveGeometryInstances;

};
