#pragma once

#include "camera_controller.h"
#include "model_loader.h"

#include <auto_vk_toolkit.hpp>
#include <invokee.hpp>
#include <material_image_helpers.hpp>

class renderer : public avk::invokee
{
public:
	struct transformation_matrices {
		glm::mat4 mModelMatrix;
		int mMaterialIndex;
	};

	struct ray_tracing_push_constant_data {
		glm::mat4 mCameraTransform;
		glm::mat4 mInvCameraTransform;
		float mCameraHalfFovAngle;
	};

	renderer(avk::queue &aQueue, glm::uvec2 resolution);

	// utils
	avk::image_sampler create_sampler(avk::image_view &imageView);

	void create_ray_tracing_prerequisites();
	void create_ray_tracing_pipeline();
	void prepare_screenshots();

	void initialize() override;

	void render() override;

	void update() override;

	void take_screenshot();

private:
	std::chrono::high_resolution_clock::time_point mInitTime;

	avk::queue *mQueue;
	avk::descriptor_cache mDescriptorCache;

	avk::image_view mRayTracingCameraImageView;
	avk::image_view mRayTracingLightImageView;
	avk::image_view mRayTracingResultImageView;
	avk::top_level_acceleration_structure mTlas;

	std::array<avk::buffer, 3> mViewProjBuffers;

	avk::ray_tracing_pipeline mRayTracingPipeline;
	
	camera_controller *mCameraController = nullptr;

	model_loader mModelLoader;

	bool mIsFullscreen = false;
	
	std::chrono::steady_clock::time_point mStartTime;
	size_t mStartTimestamp;

	glm::uvec2 mResolution;

	avk::image mScreenshotImage;
	avk::buffer mScreenshotBuffer;
};
