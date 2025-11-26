#include "camera_controller.h"

camera_controller::camera_data camera_controller::m_camera_data = m_camera_data = { 0.02f,
		1.0f,
		0.001f,
		0.02f,
		1000.0f };

camera_controller::camera_controller(float aspect_ratio, avk::composition_interface *current_composition)
{
	mOrbitCam.set_translation({0.0f, 1.0f, 0.0f});
	mQuakeCam.set_translation({0.0f, 1.0f, 0.0f});
	mOrbitCam.set_perspective_projection(glm::radians(60.0f), aspect_ratio, 0.3f, 1000.0f);
	mQuakeCam.set_perspective_projection(glm::radians(60.0f), aspect_ratio, 0.3f, 1000.0f);
	current_composition->add_element(mOrbitCam);
	current_composition->add_element(mQuakeCam);
	switch_to_quake_cam();
}

void camera_controller::switch_to_orbit_cam()
{
	mOrbitCam.set_matrix(mQuakeCam.matrix());
	mOrbitCam.enable();
	mQuakeCam.disable();
}

void camera_controller::switch_to_quake_cam()
{
	mQuakeCam.set_matrix(mOrbitCam.matrix());
	mQuakeCam.enable();
	mOrbitCam.disable();
}

void camera_controller::disable_cams() {
	mQuakeCam.disable();
	mOrbitCam.disable();
}

void camera_controller::handle_mouse_occupation(bool begin, bool end)
{
	if (begin && mOrbitCam.is_enabled()) {
		mOrbitCam.disable();
	} 
	else if (end && !mQuakeCam.is_enabled()) {
		mOrbitCam.enable();
	}
}

void camera_controller::update(avk::input_buffer &input, avk::composition_interface *current_composition)
{
	if (input.key_pressed(avk::key_code::left)) {
		mQuakeCam.look_along(avk::left());
	}
	if (input.key_pressed(avk::key_code::right)) {
		mQuakeCam.look_along(avk::right());
	}
	if (input.key_pressed(avk::key_code::up)) {
		mQuakeCam.look_along(avk::front());
	}
	if (input.key_pressed(avk::key_code::down)) {
		mQuakeCam.look_along(avk::back());
	}
	if (input.key_pressed(avk::key_code::page_up)) {
		mQuakeCam.look_along(avk::up());
	}
	if (input.key_pressed(avk::key_code::page_down)) {
		mQuakeCam.look_along(avk::down());
	}
	if (input.key_pressed(avk::key_code::home)) {
		mQuakeCam.look_at(glm::vec3{0.0f, 0.0f, 0.0f});
	}

	glm::mat4 currentTransform = global_transformation_matrix();
	mMoved = (mPreviousTransform != currentTransform);
	mPreviousTransform = currentTransform;
}

glm::mat4 camera_controller::projection_and_view_matrix()
{
	return mQuakeCam.is_enabled() ? mQuakeCam.projection_and_view_matrix() : mOrbitCam.projection_and_view_matrix();
}

glm::mat4 camera_controller::global_transformation_matrix() {
	return mQuakeCam.is_enabled() ? mQuakeCam.global_transformation_matrix() : mOrbitCam.global_transformation_matrix();
}

glm::mat4 camera_controller::inverse_global_transformation_matrix() {
	return mQuakeCam.is_enabled() ? mQuakeCam.inverse_global_transformation_matrix() : mOrbitCam.inverse_global_transformation_matrix();
}

void camera_controller::set_aspect_ratio(float aspect_ratio)
{
	mQuakeCam.set_aspect_ratio(aspect_ratio);
	mOrbitCam.set_aspect_ratio(aspect_ratio);
}

void camera_controller::set_global_transformation_matrix(glm::mat4 matrix)
{
	mQuakeCam.set_matrix(matrix);
	mOrbitCam.set_matrix(matrix);
}

float camera_controller::get_current_far_plane()
{
		return mQuakeCam.is_enabled() ? mQuakeCam.far_plane_distance() : mOrbitCam.far_plane_distance();
}

void camera_controller::setFocalLength(float a)
{
	m_camera_data.mFocalLength = a;
}

void camera_controller::setFocalDistance(float a)
{
	m_camera_data.mFocalDistance = a;
}

void camera_controller::setApertureSize(float a)
{
	m_camera_data.mApertureSize = a;
}

void camera_controller::setMaxCoC(float a)
{
	m_camera_data.mMaximumCoCDiameter = a;
}
