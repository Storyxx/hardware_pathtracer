#include "model_loader.h"

#include "material_helper.hpp"
#include "INIReader.h"

#include <model.hpp>
#include <sequential_invoker.hpp>
#include <vk_convenience_functions.hpp>
#include <conversion_utils.hpp>


model_loader::model_loader(avk::queue *aQueue)
	: mQueue{aQueue}
{}


void model_loader::load_models_from_ini(
	std::string iniPath
) {

	std::vector<avk::material_gpu_data> gpuMaterials;
	std::vector<avk::image_sampler> imageSamplers;
	avk::command::action_type_command materialCommands;
	size_t modelIndex = 0;

	INIReader reader(iniPath);
	std::set<std::string> sections = reader.Sections();
	for (std::set<std::string>::iterator it = sections.begin(); it != sections.end(); ++it)
	{
		std::string modelGLBPath = reader.Get(*it, "path", "");
		// TODO add model position and scale

		auto [gpuMaterialsData, imageSamplersData, materialCommandsData] = load_single_model(modelGLBPath, imageSamplers.size(), gpuMaterials.size(), modelIndex);

		gpuMaterials.insert(gpuMaterials.end(), gpuMaterialsData.begin(), gpuMaterialsData.end());
		imageSamplers.insert(imageSamplers.end(), imageSamplersData.begin(), imageSamplersData.end());
		materialCommands.mNestedCommandsAndSyncInstructions.push_back(std::move(materialCommandsData));

		modelIndex++;
	}

	mActiveGeometryInstances.insert(std::begin(mActiveGeometryInstances), std::begin(mAllGeometryInstances), std::end(mAllGeometryInstances));
	mTlasUpdateRequired = true;

	mImageSamplers = std::move(imageSamplers);
	mCombinedImageSamplerDescriptorInfos = avk::as_combined_image_samplers(mImageSamplers, avk::layout::shader_read_only_optimal);

	// A buffer to hold all the material data:
	mMaterialBuffer = avk::context().create_buffer(
		avk::memory_usage::host_visible, {},
		avk::storage_buffer_meta::create_from_data(gpuMaterials)
	);

	// Submit the commands material commands and the materials buffer fill to the device:
	avk::context().record_and_submit_with_fence({
		std::move(materialCommands),
		mMaterialBuffer->fill(gpuMaterials.data(), 0)
	}, *mQueue)->wait_until_signalled();

	// create a host coherent buffer for the transforms to use in the rasterization pass
	mTransformsBuffer = avk::context().create_buffer(
		avk::memory_usage::host_coherent, {},
		avk::storage_buffer_meta::create_from_data(mTransforms)
	);
	avk::context().record_and_submit_with_fence({
		mTransformsBuffer->fill(mTransforms.data(), 0),
	}, *mQueue)->wait_until_signalled();

}

void model_loader::update_transform_for_model(size_t modelIndex, glm::mat4 newTransform)
{
	for (size_t i = 0; i < mMaterialModelMapping.size(); i++) {
		if (mMaterialModelMapping[i] == modelIndex) {
			mTransforms[i] = newTransform;
			mAllGeometryInstances[i].set_transform_column_major(avk::to_array(newTransform));
		}
	}
	mTlasUpdateRequired = true;
	avk::context().record_and_submit_with_fence({
		mTransformsBuffer->fill(mTransforms.data(), 0),
	}, *mQueue)->wait_until_signalled();
	update();
}


const std::vector<avk::geometry_instance> model_loader::get_active_geometry_instances_for_tlas_build()
{
	std::vector<avk::geometry_instance> toBeReturned = std::move(mActiveGeometryInstances);
	mActiveGeometryInstances.clear();
	mTlasUpdateRequired = false;
	return toBeReturned;
}


void model_loader::update()
{
	if (mTlasUpdateRequired)
	{
		assert(mAllGeometryInstances.size() == mGeometryInstanceActive.size());

		mActiveGeometryInstances.clear();
		for (size_t i = 0; i < mAllGeometryInstances.size(); ++i) {
			if (mGeometryInstanceActive[i]) {
				mActiveGeometryInstances.push_back(mAllGeometryInstances[i]);
			}
		}
	}
}


std::tuple<std::vector<avk::material_gpu_data>, std::vector<avk::image_sampler>, avk::command::action_type_command> model_loader::load_single_model(
	std::string filePath,
	size_t samplerIndexOffset,
	size_t materialIndexOffset,
	size_t modelIndex
) {
	auto model = avk::model_t::load_from_file(filePath, aiProcess_Triangulate | aiProcess_PreTransformVertices);

	// Get all the different materials of the model:
	auto distinctMaterials = model->distinct_material_configs();

	// The following might be a bit tedious still, but maybe it's not. For what it's worth, it is expressive.
	// The following loop gathers all the vertex and index data PER MATERIAL and constructs the buffers and materials.
	// Later, we'll use ONE draw call PER MATERIAL to draw the whole scene.
	std::vector<avk::material_config> allMatConfigs;

	for (const auto &[materialConfig, indices] : distinctMaterials) {
		auto &newElement = mDrawCalls.emplace_back();
		allMatConfigs.push_back(materialConfig);
		newElement.mMaterialIndex = static_cast<int>(allMatConfigs.size() - 1 + materialIndexOffset);

		auto selection = avk::make_model_references_and_mesh_indices_selection(model, indices);

		auto [posBfr, idxBfr, posIdxCmds] = avk::create_vertex_and_index_buffers<
			avk::uniform_texel_buffer_meta, 
			avk::read_only_input_to_acceleration_structure_builds_buffer_meta>(selection);
		auto [nrmBfr, nrmCmds] = avk::create_normals_buffer<
			avk::uniform_texel_buffer_meta,
			avk::vertex_buffer_meta>(selection);
		auto [tngBfr, tngCmds] = avk::create_tangents_buffer<
			avk::uniform_texel_buffer_meta,
			avk::vertex_buffer_meta>(selection);
		auto [bitngBfr, bitngCmds] = avk::create_bitangents_buffer<
			avk::uniform_texel_buffer_meta,
			avk::vertex_buffer_meta>(selection);
		auto [texBfr, texCmds] = avk::create_2d_texture_coordinates_buffer<
			avk::uniform_texel_buffer_meta,
			avk::vertex_buffer_meta>(selection);

		newElement.mPositionsBuffer = posBfr;
		newElement.mIndexBuffer = idxBfr;
		newElement.mNormalsBuffer = nrmBfr;
		newElement.mTangentsBuffer = tngBfr;
		newElement.mBitangentsBuffer = bitngBfr;
		newElement.mTexCoordsBuffer = texBfr;


		// Create a bottom level acceleration structure instance with this geometry.
		auto blas = avk::context().create_bottom_level_acceleration_structure(
			{avk::acceleration_structure_size_requirements::from_buffers(avk::vertex_index_buffer_pair{ posBfr, idxBfr })},
			false // no need to allow updates for static geometry
		);

		// Upload the data, represented by commands:
		avk::context().record_and_submit_with_fence({
			std::move(posIdxCmds),
			std::move(nrmCmds),
			std::move(tngCmds),
			std::move(bitngCmds),
			std::move(texCmds),
			// Gotta wait until all buffers have been transfered before we can start the BLAS build:
			avk::sync::global_memory_barrier(avk::stage::transfer >> avk::stage::acceleration_structure_build, avk::access::transfer_write >> avk::access::acceleration_structure_write),
			blas->build({ avk::vertex_index_buffer_pair{ posBfr, idxBfr } })
		}, *mQueue)->wait_until_signalled();


		auto bufferViewIndex = static_cast<uint32_t>(mTexCoordsBufferViews.size());

		// Maybe TODO: for all instances:
		mAllGeometryInstances.push_back(
			avk::context().create_geometry_instance(blas.as_reference())
			// Set this instance's custom index, which is especially important since we'll use it in shaders
			// to refer to the right material and also vertex data (these two are aligned index-wise):
			.set_custom_index(bufferViewIndex)
			// Set this instance's transformation matrix:
			//.set_transform_column_major(avk::to_array(avk::matrix_from_transforms(inst.mTranslation, glm::quat(inst.mRotation), inst.mScaling)))
		);

		mMaterialModelMapping.push_back(modelIndex);
		mTransforms.push_back(glm::mat4(1.0));

		// State that this geometry instance shall be included in TLAS generation by default:
		mGeometryInstanceActive.push_back(true);

		mBlas.push_back(std::move(blas)); // Move this BLAS s.t. we don't have to enable_shared_ownership. We're done with it here.

		// After we have used positions and indices for building the BLAS, still need to create buffer views which allow us to access
		// the per vertex data in ray tracing shaders, where they will be accessible via samplerBuffer- or usamplerBuffer-type uniforms.
		mPositionsBufferViews.push_back(avk::context().create_buffer_view(posBfr));
		mIndexBufferViews.push_back(avk::context().create_buffer_view(idxBfr));
		mNormalsBufferViews.push_back(avk::context().create_buffer_view(nrmBfr));
		mTangentsBufferViews.push_back(avk::context().create_buffer_view(tngBfr));
		mBitangentsBufferViews.push_back(avk::context().create_buffer_view(bitngBfr));
		mTexCoordsBufferViews.push_back(avk::context().create_buffer_view(texBfr));
	}

	// For all the different materials, transfer them in structs which are well
	// suited for GPU-usage (proper alignment, and containing only the relevant data),
	// also load all the referenced images from file and provide access to them
	// via samplers;
	return material_helper::convert_for_gpu_usage(model->handle(), allMatConfigs, samplerIndexOffset);
}
