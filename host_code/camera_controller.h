#pragma once

#include <auto_vk_toolkit.hpp>
#include <orbit_camera.hpp>
#include <quake_camera.hpp>

class camera_controller {


public:

	struct camera_data {
		float mFocalLength;
		float mFocalDistance;
		float mApertureSize;
		float mMaximumCoCDiameter;
		float mFarPlaneDistance;
	};

	camera_controller(float aspect_ratio, avk::composition_interface *current_composition);

	void switch_to_orbit_cam();
	void switch_to_quake_cam();
	void disable_cams();
	void handle_mouse_occupation(bool begin, bool end);
	void update(avk::input_buffer &input, avk::composition_interface *current_composition);

	// getter
	inline bool is_quake_cam_enabled() { return mQuakeCam.is_enabled(); };
	glm::mat4 projection_and_view_matrix();
	glm::mat4 global_transformation_matrix();
	glm::mat4 inverse_global_transformation_matrix();

	inline avk::quake_camera *quakeCamera() { return &mQuakeCam; };
	inline bool hasMoved() { return mMoved; }

	// setter
	void set_aspect_ratio(float aspect_ratio);
	void set_global_transformation_matrix(glm::mat4 matrix);

	float get_current_far_plane();

	static void setFocalLength(float a);
	static void setFocalDistance(float a);
	static void setApertureSize(float a);
	static void setMaxCoC(float a);
	static camera_data m_camera_data;

private:
	avk::orbit_camera mOrbitCam;
	avk::quake_camera mQuakeCam;
	bool mMoved;
	glm::mat4 mPreviousTransform;
};
