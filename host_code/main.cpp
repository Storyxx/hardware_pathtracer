

#include "renderer.h"


int main() {

	int result = EXIT_FAILURE;

	try {
		avk::window* mainWnd = avk::context().create_window("Renderer");
		mainWnd->set_resolution({ 960, 540 }); //1920, 1080
		mainWnd->enable_resizing(false);
		mainWnd->set_presentaton_mode(avk::presentation_mode::mailbox);
		mainWnd->set_number_of_concurrent_frames(1u);
		mainWnd->open();

		avk::queue& singleQueue = avk::context().create_queue({}, avk::queue_selection_preference::versatile_queue, mainWnd);
		mainWnd->set_queue_family_ownership(singleQueue.family_index());
		mainWnd->set_present_queue(singleQueue);

		renderer app = renderer(singleQueue, { 3840, 2160 });

		auto composition = configure_and_compose(
			avk::application_name("Renderer"),
			avk::required_device_extensions()
				.add_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
				.add_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
				.add_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME)
				.add_extension(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME)
				.add_extension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME)
				.add_extension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)
				.add_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
				.add_extension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME),
			[](vk::PhysicalDeviceVulkan12Features& aVulkan12Featues) {
				// Also this Vulkan 1.2 feature is required for ray tracing:
				aVulkan12Featues.setBufferDeviceAddress(VK_TRUE);
			},
			[](vk::PhysicalDeviceRayTracingPipelineFeaturesKHR& aRayTracingFeatures) {
				// Enabling the extensions is not enough, we need to activate ray tracing features explicitly here:
				aRayTracingFeatures.setRayTracingPipeline(VK_TRUE);
			},
			[](vk::PhysicalDeviceAccelerationStructureFeaturesKHR& aAccelerationStructureFeatures) {
				// ...and here:
				aAccelerationStructureFeatures.setAccelerationStructure(VK_TRUE);
			},
			[](vk::PhysicalDeviceRayQueryFeaturesKHR& aRayQueryFeatures) {
				aRayQueryFeatures.setRayQuery(VK_TRUE);
			},
			[](avk::validation_layers& config) {
				config.enable_feature(vk::ValidationFeatureEnableEXT::eSynchronizationValidation);
			},
			// Pass windows:
			//mainWnd,
			// Pass invokees:
			app
		);

		avk::sequential_invoker invoker;

		composition.start_render_loop(
			[&invoker](const std::vector<avk::invokee*>& aToBeInvoked) {
				invoker.invoke_updates(aToBeInvoked);
			},
			[&invoker](const std::vector<avk::invokee*>& aToBeInvoked) {
				avk::context().execute_for_each_window([](avk::window* wnd) {
					wnd->sync_before_render();
				});

				invoker.invoke_renders(aToBeInvoked);

				avk::context().execute_for_each_window([](avk::window* wnd) {
					wnd->render_frame();
				});
			}
		);
		result = EXIT_SUCCESS;
	}
	catch (avk::logic_error&) {}
	catch (avk::runtime_error&) {}

	return result;
}