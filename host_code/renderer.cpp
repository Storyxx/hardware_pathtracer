#include "renderer.h"

#include "material_helper.hpp"

#include <model.hpp>
#include <sequential_invoker.hpp>
#include <vk_convenience_functions.hpp>

#include <glm/gtx/io.hpp>
#include <stb_image_write.h>


renderer::renderer(avk::queue &aQueue, glm::uvec2 resolution)
	: mQueue{&aQueue}
	, mResolution(resolution)
	, mModelLoader{mQueue}
{
	mStartTime = std::chrono::high_resolution_clock::now();

	const auto p1 = std::chrono::system_clock::now();
	mStartTimestamp = std::chrono::duration_cast<std::chrono::seconds>(p1.time_since_epoch()).count();
}


avk::image_sampler renderer::create_sampler(avk::image_view &imageView)
{
	avk::sampler smplr;
	smplr = avk::context().create_sampler(avk::filter_mode::nearest_neighbor, avk::border_handling_mode::clamp_to_edge);
	return avk::context().create_image_sampler(imageView, smplr);
}

void renderer::create_ray_tracing_prerequisites()
{
	// Create an offscreen image to ray-trace into. It is accessed via an image view:

	avk::image cameraImage = avk::context().create_image(mResolution.x, mResolution.y, vk::Format::eR32G32B32A32Sfloat, 1, avk::memory_usage::device, avk::image_usage::general_storage_image);
	avk::image lightImage = avk::context().create_image(mResolution.x, mResolution.y, vk::Format::eR32G32B32A32Sfloat, 1, avk::memory_usage::device, avk::image_usage::general_storage_image);
	avk::image resultImage = avk::context().create_image(mResolution.x, mResolution.y, vk::Format::eB8G8R8A8Unorm, 1, avk::memory_usage::device, avk::image_usage::general_storage_image);


	avk::context().record_and_submit_with_fence({
		avk::sync::image_memory_barrier(cameraImage.as_reference(),
										avk::stage::none >> avk::stage::none,
										avk::access::none >> avk::access::none).with_layout_transition(avk::layout::undefined >> avk::layout::general),
		avk::sync::image_memory_barrier(lightImage.as_reference(),
										avk::stage::none >> avk::stage::none,
										avk::access::none >> avk::access::none).with_layout_transition(avk::layout::undefined >> avk::layout::general),
		avk::sync::image_memory_barrier(resultImage.as_reference(),
										avk::stage::none >> avk::stage::none,
										avk::access::none >> avk::access::none).with_layout_transition(avk::layout::undefined >> avk::layout::general),
	}, *mQueue)->wait_until_signalled();

	mRayTracingCameraImageView = avk::context().create_image_view(cameraImage);
	mRayTracingLightImageView = avk::context().create_image_view(lightImage);
	mRayTracingResultImageView = avk::context().create_image_view(resultImage);

	// Initialize the TLAS (but don't build it yet)
	mTlas = avk::context().create_top_level_acceleration_structure(
		mModelLoader.max_number_of_geometry_instances(),
		true
	);
}


void renderer::create_ray_tracing_pipeline()
{
	mRayTracingPipeline = avk::context().create_ray_tracing_pipeline_for(
		// Specify all the shaders which participate in rendering in a shader binding table (the order matters):
		avk::define_shader_table(
			avk::ray_generation_shader("shaders/ray_gen_shader.rgen"),
			avk::triangles_hit_group::create_with_rchit_only("shaders/closest_hit_shader.rchit"),
			avk::miss_shader("shaders/miss_shader.rmiss")
		),
		// We won't need the maximum recursion depth, but why not:
		avk::context().get_max_ray_tracing_recursion_depth(),
		// Define push constants and descriptor bindings:
		avk::push_constant_binding_data{avk::shader_type::ray_generation | avk::shader_type::closest_hit, 0, sizeof(ray_tracing_push_constant_data)},
		avk::descriptor_binding(0, 0, mModelLoader.combined_image_sampler_descriptor_infos()),
		avk::descriptor_binding(0, 1, mModelLoader.material_buffer()),
		avk::descriptor_binding(0, 2, avk::as_uniform_texel_buffer_views(mModelLoader.index_buffer_views())),
		avk::descriptor_binding(0, 3, avk::as_uniform_texel_buffer_views(mModelLoader.tex_coords_buffer_views())),
		avk::descriptor_binding(0, 4, avk::as_uniform_texel_buffer_views(mModelLoader.normals_buffer_views())),
		avk::descriptor_binding(0, 5, avk::as_uniform_texel_buffer_views(mModelLoader.tangents_buffer_views())),
		avk::descriptor_binding(0, 6, avk::as_uniform_texel_buffer_views(mModelLoader.bitangents_buffer_views())),
		avk::descriptor_binding(1, 0, mRayTracingCameraImageView->as_storage_image(avk::layout::general)),
		avk::descriptor_binding(1, 1, mRayTracingLightImageView->as_storage_image(avk::layout::general)),
		avk::descriptor_binding(1, 2, mRayTracingResultImageView->as_storage_image(avk::layout::general)),
		avk::descriptor_binding(2, 0, mTlas) // Bind the TLAS, s.t. we can trace rays against it
	);

	std::cout << "Maximum Recursion Depth: " << avk::context().get_max_ray_tracing_recursion_depth().mMaxRecursionDepth << std::endl;

	mRayTracingPipeline->print_shader_binding_table_groups();
}

void renderer::prepare_screenshots() {
	mScreenshotImage = avk::context().create_image(mResolution.x, mResolution.y, vk::Format::eR8G8B8A8Unorm);

	mScreenshotBuffer = avk::context().create_buffer(
		avk::memory_usage::host_visible,
		vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
		avk::generic_buffer_meta::create_from_size(mResolution.x * mResolution.y * 4)
	);
}


void renderer::initialize()
{
	mInitTime = std::chrono::high_resolution_clock::now();

	// Create a descriptor cache that helps us to conveniently create descriptor sets:
	mDescriptorCache = avk::context().create_descriptor_cache();

	mModelLoader.load_models_from_ini("assets/models.ini");

	// Create a buffer for the transformation matrices in a host coherent memory region (one for each frame in flight):
	for (int i = 0; i < 3; ++i) {
		mViewProjBuffers[i] = avk::context().create_buffer(
			avk::memory_usage::host_coherent, {},
			avk::uniform_buffer_meta::create_from_data(glm::mat4())
		);
	}


	create_ray_tracing_prerequisites();
	create_ray_tracing_pipeline();

	mRayTracingPipeline.enable_shared_ownership();
	mRayTracingCameraImageView.enable_shared_ownership();
	mRayTracingLightImageView.enable_shared_ownership();
	mRayTracingResultImageView.enable_shared_ownership();

	prepare_screenshots();

	mUpdater.emplace();

	// enable shader hot reloading for all pipelines
	mUpdater->on(avk::shader_files_changed_event(mRayTracingPipeline.as_reference())).update(mRayTracingPipeline);

	// handle a window resize update
	avk::updater_config_proxy updaterProxy = mUpdater->on(avk::swapchain_resized_event(avk::context().main_window()));
	updaterProxy
		.invoke([this]() {
			this->mCameraController->set_aspect_ratio(avk::context().main_window()->aspect_ratio());
		})
		.update(
			mRayTracingCameraImageView,
			mRayTracingLightImageView,
			mRayTracingResultImageView,
			mRayTracingPipeline
		)
		.then_on(avk::destroying_image_view_event()) // Make sure that our descriptor cache stays cleaned up:
		.invoke([this](const avk::image_view &aImageViewToBeDestroyed) {
			mDescriptorCache->remove_sets_with_handle(aImageViewToBeDestroyed->handle());
		});


	// Add the cameras to the composition (and let them handle updates)
	mCameraController = new camera_controller(avk::context().main_window()->aspect_ratio(), avk::current_composition());

	// setup for automated quake camera
	auto resolution = avk::context().main_window()->resolution();
	avk::context().main_window()->set_cursor_pos({resolution[0] / 2.0, resolution[1] / 2.0});

	mCameraController->set_global_transformation_matrix({
		0.719,		-0.000,	0.695,	0.000,
		 0.063,		0.996,	-0.065, 0.000,
		-0.692,		0.091,	0.716,	0.000,
		-13.311,	2.343,  1.132,  1.000
	});
	mCameraController->disable_cams();

	//avk::context().main_window()->switch_to_fullscreen_mode();
	//mIsFullscreen = true;
}

void renderer::render()
{
	auto mainWnd = avk::context().main_window();
	auto inFlightIndex = mainWnd->current_in_flight_index();

	auto currentTime = std::chrono::high_resolution_clock::now();
	float time = std::chrono::duration<float, std::milli>(currentTime - mStartTime).count();

	auto viewProjMat = mCameraController->projection_and_view_matrix();
	auto emptyCmd = mViewProjBuffers[inFlightIndex]->fill(glm::value_ptr(viewProjMat), 0);

	// Get a command pool to allocate command buffers from:
	auto &commandPool = avk::context().get_command_pool_for_single_use_command_buffers(*mQueue);

	// The swap chain provides us with an "image available semaphore" for the current frame.
	// Only after the swapchain image has become available, we may start rendering into it.
	auto imageAvailableSemaphore = mainWnd->consume_current_image_available_semaphore();

	// Create a command buffer and render into the *current* swap chain image:
	auto cmdBfr = commandPool->alloc_command_buffer(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

	avk::context().record({

		// clear camera image on move
		avk::sync::image_memory_barrier(mRayTracingCameraImageView->get_image(),
			avk::stage::ray_tracing_shader >> avk::stage::all_transfer,
			avk::access::shader_write >> avk::access::transfer_write
		).with_layout_transition(avk::layout::general >> avk::layout::transfer_dst),


		avk::command::conditional([this] { return mCameraController->hasMoved(); },
			[this] {
				return avk::command::custom_commands([=](avk::command_buffer_t& cb) {
					auto const clearValue = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f};
					auto const subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u);
					cb.handle().clearColorImage(
						mRayTracingCameraImageView->get_image().handle(),
						vk::ImageLayout::eTransferDstOptimal,
						&clearValue,
						1,
						&subresourceRange,
						cb.root_ptr()->dispatch_loader_core()
					);
				});
			}
		),

		avk::sync::image_memory_barrier(mRayTracingCameraImageView->get_image(),
			avk::stage::all_transfer >> avk::stage::ray_tracing_shader,
			avk::access::transfer_write >> avk::access::shader_write
		).with_layout_transition(avk::layout::transfer_dst >> avk::layout::general),


		// clear light image on move
		avk::sync::image_memory_barrier(mRayTracingLightImageView->get_image(),
			avk::stage::ray_tracing_shader >> avk::stage::all_transfer,
			avk::access::shader_write >> avk::access::transfer_write
		).with_layout_transition(avk::layout::general >> avk::layout::transfer_dst),


		avk::command::conditional([this] { return mCameraController->hasMoved(); },
			[this] {
				return avk::command::custom_commands([=](avk::command_buffer_t& cb) {
					auto const clearValue = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f};
					auto const subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u);
					cb.handle().clearColorImage(
						mRayTracingLightImageView->get_image().handle(),
						vk::ImageLayout::eTransferDstOptimal,
						&clearValue,
						1,
						&subresourceRange,
						cb.root_ptr()->dispatch_loader_core()
					);
				});
			}
		),

		avk::sync::image_memory_barrier(mRayTracingLightImageView->get_image(),
			avk::stage::all_transfer >> avk::stage::ray_tracing_shader,
			avk::access::transfer_write >> avk::access::shader_write
		).with_layout_transition(avk::layout::transfer_dst >> avk::layout::general),



		// do ray tracing
		avk::command::bind_pipeline(mRayTracingPipeline.as_reference()),
		avk::command::bind_descriptors(mRayTracingPipeline->layout(), mDescriptorCache->get_or_create_descriptor_sets({
			avk::descriptor_binding(0, 0, mModelLoader.combined_image_sampler_descriptor_infos()),
			avk::descriptor_binding(0, 1, mModelLoader.material_buffer()),
			avk::descriptor_binding(0, 2, avk::as_uniform_texel_buffer_views(mModelLoader.index_buffer_views())),
			avk::descriptor_binding(0, 3, avk::as_uniform_texel_buffer_views(mModelLoader.tex_coords_buffer_views())),
			avk::descriptor_binding(0, 4, avk::as_uniform_texel_buffer_views(mModelLoader.normals_buffer_views())),
			avk::descriptor_binding(0, 5, avk::as_uniform_texel_buffer_views(mModelLoader.tangents_buffer_views())),
			avk::descriptor_binding(0, 6, avk::as_uniform_texel_buffer_views(mModelLoader.bitangents_buffer_views())),
			avk::descriptor_binding(1, 0, mRayTracingCameraImageView->as_storage_image(avk::layout::general)),
			avk::descriptor_binding(1, 1, mRayTracingLightImageView->as_storage_image(avk::layout::general)),
			avk::descriptor_binding(1, 2, mRayTracingResultImageView->as_storage_image(avk::layout::general)),
			avk::descriptor_binding(2, 0, mTlas)
		})),
		avk::command::push_constants(
			mRayTracingPipeline->layout(),
			ray_tracing_push_constant_data {
				mCameraController->global_transformation_matrix(),
				mCameraController->inverse_global_transformation_matrix(),
				((90 / 2.0) / 180.0) * glm::pi<float>()
			},
			avk::shader_type::ray_generation | avk::shader_type::closest_hit
		),

		// Do it:
		avk::command::trace_rays(
			{mResolution.x, mResolution.y, 1},
			mRayTracingPipeline->shader_binding_table(),
			avk::using_raygen_group_at_index(0),
			avk::using_miss_group_at_index(0),
			avk::using_hit_group_at_index(0)
		),


		avk::sync::image_memory_barrier(mRayTracingResultImageView->get_image(),
			avk::stage::ray_tracing_shader >> avk::stage::blit,
			avk::access::shader_write >> avk::access::transfer_read
		).with_layout_transition(avk::layout::general >> avk::layout::transfer_src),
		avk::sync::image_memory_barrier(mainWnd->current_backbuffer_reference().image_at(0),
			avk::stage::none >> avk::stage::blit,
			avk::access::none >> avk::access::transfer_write
			).with_layout_transition(avk::layout::undefined >> avk::layout::transfer_dst),

		avk::blit_image(
			mRayTracingResultImageView->get_image(), avk::layout::transfer_src,
			mainWnd->current_backbuffer_reference().image_at(0), avk::layout::transfer_dst
		), 

		avk::sync::image_memory_barrier(mRayTracingResultImageView->get_image(),
			avk::stage::blit >> avk::stage::ray_tracing_shader,
			avk::access::transfer_read >> avk::access::shader_write
			).with_layout_transition(avk::layout::transfer_src >> avk::layout::general),
		avk::sync::image_memory_barrier(mainWnd->current_backbuffer_reference().image_at(0),
			avk::stage::blit >> avk::stage::color_attachment_output,
			avk::access::transfer_write >> avk::access::color_attachment_write
			).with_layout_transition(avk::layout::transfer_dst >> avk::layout::present_src),
		
		
	})
	.into_command_buffer(cmdBfr)
	.then_submit_to(*mQueue)
	// Do not start to render before the image has become available:
	.waiting_for(imageAvailableSemaphore >> avk::stage::ray_tracing_shader)
	.submit();

	mainWnd->handle_lifetime(std::move(cmdBfr));
}

void renderer::update()
{
	static int counter = 0;
	if (++counter == 4) {
		auto current = std::chrono::high_resolution_clock::now();
		auto time_span = current - mInitTime;
		auto int_min = std::chrono::duration_cast<std::chrono::minutes>(time_span).count();
		auto int_sec = std::chrono::duration_cast<std::chrono::seconds>(time_span).count();
		auto fp_ms = std::chrono::duration<double, std::milli>(time_span).count();
		printf("Time from init to fourth frame: %d min, %lld sec %lf ms\n", int_min, int_sec - static_cast<decltype(int_sec)>(int_min) * 60, fp_ms - 1000.0 * int_sec);
	}

	const size_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	if (timestamp - mStartTimestamp > 10*60) {
		mStartTimestamp = timestamp;
		take_screenshot();
	}

	if (avk::input().key_pressed(avk::key_code::c)) {
		// Center the cursor:
		auto resolution = avk::context().main_window()->resolution();
		avk::context().main_window()->set_cursor_pos({resolution[0] / 2.0, resolution[1] / 2.0});
		mCameraController->switch_to_quake_cam();
	}
	if (avk::input().key_pressed(avk::key_code::escape) ||
		glfwWindowShouldClose(avk::context().main_window()->handle().value().mHandle)) {
		// Stop the current composition:
		avk::current_composition()->stop();
	}
	if (avk::input().key_pressed(avk::key_code::f11)) {
		if (mIsFullscreen) {
			avk::context().main_window()->switch_to_windowed_mode();
			mIsFullscreen = false;
		} else {
			avk::context().main_window()->switch_to_fullscreen_mode();
			mIsFullscreen = true;
		}
	}

	if (avk::input().key_pressed(avk::key_code::p)) {
		std::cout << glm::transpose(mCameraController->global_transformation_matrix()) << std::endl;
		take_screenshot();
	}

	mCameraController->update(avk::input(), avk::current_composition());


	if (mModelLoader.has_updated_geometry_for_tlas())
	{
		// Getometry selection has changed => rebuild the TLAS:

		std::vector<avk::geometry_instance> activeGeometryInstances = mModelLoader.get_active_geometry_instances_for_tlas_build();

		if (!activeGeometryInstances.empty()) {
			// Get a command pool to allocate command buffers from:
			auto &commandPool = avk::context().get_command_pool_for_single_use_command_buffers(*mQueue);

			// Create a command buffer and render into the *current* swap chain image:
			auto cmdBfr = commandPool->alloc_command_buffer(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

			avk::context().record({
				// We're using only one TLAS for all frames in flight. Therefore, we need to set up a barrier
				// affecting the whole queue which waits until all previous ray tracing work has completed:
				avk::sync::global_execution_barrier(avk::stage::ray_tracing_shader >> avk::stage::acceleration_structure_build),

				// ...then we can safely update the TLAS with new data:
				mTlas->build(                // We're not updating existing geometry, but we are changing the geometry => therefore, we need to perform a full rebuild.
					activeGeometryInstances, // Build only with the active geometry instances
					{}                       // Let top_level_acceleration_structure_t::build handle the staging buffer internally 
				),

				// ...and we need to ensure that the TLAS update-build has completed (also in terms of memory
				// access--not only execution) before we may continue ray tracing with that TLAS:
				avk::sync::global_memory_barrier(
					avk::stage::acceleration_structure_build >> avk::stage::ray_tracing_shader,
					avk::access::acceleration_structure_write >> avk::access::acceleration_structure_read
				)
				})
				.into_command_buffer(cmdBfr)
				.then_submit_to(*mQueue)
				.submit();

			avk::context().main_window()->handle_lifetime(std::move(cmdBfr));
		}
	}
}

void renderer::take_screenshot() {
	std::cout << "taking screenshot" << std::endl;

	avk::context().record_and_submit_with_fence({
	avk::sync::image_memory_barrier(mRayTracingResultImageView->get_image(),
		avk::stage::ray_tracing_shader >> avk::stage::blit,
		avk::access::shader_write >> avk::access::transfer_read
	).with_layout_transition(avk::layout::general >> avk::layout::transfer_src),

	avk::sync::image_memory_barrier(mScreenshotImage.get(),
		avk::stage::none >> avk::stage::blit,
		avk::access::none >> avk::access::transfer_write
	).with_layout_transition(avk::layout::undefined >> avk::layout::transfer_dst),

	avk::blit_image(mRayTracingResultImageView->get_image(), avk::layout::transfer_src, mScreenshotImage, avk::layout::transfer_dst),

	avk::sync::image_memory_barrier(mRayTracingResultImageView->get_image(),
		avk::stage::blit >> avk::stage::ray_tracing_shader,
		avk::access::transfer_read >> avk::access::shader_write
	).with_layout_transition(avk::layout::transfer_src >> avk::layout::general),

	avk::sync::image_memory_barrier(mScreenshotImage.get(),
		avk::stage::blit >> avk::stage::copy,
		avk::access::transfer_write >> avk::access::transfer_read
	).with_layout_transition(avk::layout::transfer_dst >> avk::layout::transfer_src),

	avk::copy_image_to_buffer(mScreenshotImage, avk::layout::transfer_src, vk::ImageAspectFlagBits::eColor, mScreenshotBuffer)
	}, *mQueue)->wait_until_signalled();

	void* data = mScreenshotBuffer->map_memory(avk::mapping_access::read).get();

	std::thread stbiThread([](void* data, glm::uvec2 resolution) {
		int channels = 4;

		const auto p1 = std::chrono::system_clock::now();
		const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(p1.time_since_epoch()).count();

		std::stringstream fileName;
		fileName << "./results/" << timestamp << ".png";

		int result = stbi_write_png(
			fileName.str().c_str(),
			resolution.x,
			resolution.y,
			channels,
			data,
			resolution.x * channels
		);

		std::cout << "stbi said: " << result << std::endl;
	}, data, mResolution);

	stbiThread.detach();
}
