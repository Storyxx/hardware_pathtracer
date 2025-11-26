#version 460
#extension GL_EXT_ray_tracing : require

struct BSDF {
	vec3 albedo;
    vec3 emission;
	float roughness;
	float metalness;
	float transmission;
};

struct RayPayloadType {
    BSDF bsdf;
	vec3 normal;
    vec3 position;
	bool hit;
};

layout(location = 0) rayPayloadInEXT RayPayloadType payload;

void main()
{

    BSDF bsdf = BSDF(
		vec3(0),
        vec3(0),
        0,
        0,
        0
	);

    payload.bsdf = bsdf;
    payload.normal = vec3(0.0, 0.0, 0.0);
    payload.position = vec3(0.0);
    payload.hit = false;
}