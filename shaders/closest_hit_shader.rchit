#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_ray_query : require
#extension GL_GOOGLE_include_directive : require

layout(set = 0, binding = 0) uniform sampler2D textures[];

struct MaterialGpuData
{
	vec4 mDiffuseReflectivity;
	vec4 mAmbientReflectivity;
	vec4 mSpecularReflectivity;
	vec4 mEmissiveColor;
	vec4 mTransparentColor;
	vec4 mReflectiveColor;
	vec4 mAlbedo;

	float mOpacity;
	float mBumpScaling;
	float mShininess;
	float mShininessStrength;
	
	float mRefractionIndex;
	float mReflectivity;
	float mMetallic;
	float mSmoothness;

	float mTransmission;
	
	float mSheen;
	float mThickness;
	float mRoughness;
	float mAnisotropy;
	
	vec4 mAnisotropyRotation;
	vec4 mCustomData;
	
	int mDiffuseTexIndex;
	int mSpecularTexIndex;
	int mAmbientTexIndex;
	int mEmissiveTexIndex;
	int mHeightTexIndex;
	int mNormalsTexIndex;
	int mShininessTexIndex;
	int mOpacityTexIndex;
	int mDisplacementTexIndex;
	int mReflectionTexIndex;
	int mLightmapTexIndex;
	int mExtraTexIndex;
	
	vec4 mDiffuseTexOffsetTiling;
	vec4 mSpecularTexOffsetTiling;
	vec4 mAmbientTexOffsetTiling;
	vec4 mEmissiveTexOffsetTiling;
	vec4 mHeightTexOffsetTiling;
	vec4 mNormalsTexOffsetTiling;
	vec4 mShininessTexOffsetTiling;
	vec4 mOpacityTexOffsetTiling;
	vec4 mDisplacementTexOffsetTiling;
	vec4 mReflectionTexOffsetTiling;
	vec4 mLightmapTexOffsetTiling;
	vec4 mExtraTexOffsetTiling;
};

layout(set = 0, binding = 1) buffer Material 
{
	MaterialGpuData materials[];
} materialsBuffer;

layout(set = 0, binding = 2) uniform usamplerBuffer indexBuffers[];
layout(set = 0, binding = 3) uniform samplerBuffer texCoordsBuffers[];
layout(set = 0, binding = 4) uniform samplerBuffer normalsBuffers[];
layout(set = 0, binding = 5) uniform samplerBuffer tangentsBuffers[];
layout(set = 0, binding = 6) uniform samplerBuffer bitangentsBuffers[];

layout(set = 2, binding = 0) uniform accelerationStructureEXT topLevelAS;


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

// Receive barycentric coordinates from the geometry hit:
hitAttributeEXT vec3 hitAttribs;

layout(push_constant) uniform PushConstants {
    mat4 mCameraTransform;
	mat4 mInvCameraTransform;
	float mCameraHalfFovAngle;
} pushConstants;

vec4 sample_from_diffuse_texture(int matIndex, vec2 uv)
{
	int texIndex = materialsBuffer.materials[matIndex].mDiffuseTexIndex;
	vec4 offsetTiling = materialsBuffer.materials[matIndex].mDiffuseTexOffsetTiling;
	vec2 texCoords = uv * offsetTiling.zw + offsetTiling.xy;
	vec4 result = textureLod(textures[texIndex], texCoords, 0.0);

	if (result.rgb == vec3(1.0)) {
		result = materialsBuffer.materials[matIndex].mDiffuseReflectivity;
	}
	return result;
}

vec3 sample_from_pbr_texture(int matIndex, vec2 uv)
{
	int texIndex = materialsBuffer.materials[matIndex].mLightmapTexIndex;
	vec4 offsetTiling = materialsBuffer.materials[matIndex].mLightmapTexOffsetTiling;
	vec2 texCoords = uv * offsetTiling.zw + offsetTiling.xy;
    vec3 pbrFromTexture = texture(textures[texIndex], texCoords).rgb;

	float ambientOcclusion = pbrFromTexture.r;
	float roughness = pbrFromTexture.g;
	float metalness = pbrFromTexture.b;

	if (pbrFromTexture == vec3(1,1,1)) {
		ambientOcclusion = 0;
		roughness = materialsBuffer.materials[matIndex].mRoughness;
		metalness = materialsBuffer.materials[matIndex].mMetallic;
	}
	return vec3(ambientOcclusion, roughness, metalness);
}

vec4 sample_from_emission_texture(int matIndex, vec2 uv)
{
	int texIndex = materialsBuffer.materials[matIndex].mEmissiveTexIndex;
	vec4 offsetTiling = materialsBuffer.materials[matIndex].mEmissiveTexOffsetTiling;
	vec2 texCoords = uv * offsetTiling.zw + offsetTiling.xy;
	vec4 result = textureLod(textures[texIndex], texCoords, 0.0);

	if (result.rgb == vec3(1.0)) {
		result = materialsBuffer.materials[matIndex].mEmissiveColor;
	}
	return result;
}

float sample_transmission(int matIndex)
{
	return materialsBuffer.materials[matIndex].mTransmission;
}

vec4 sample_from_normal_texture(int matIndex, vec2 uv)
{
	int texIndex = materialsBuffer.materials[matIndex].mNormalsTexIndex;
	vec4 offsetTiling = materialsBuffer.materials[matIndex].mNormalsTexOffsetTiling;
	vec2 texCoords = uv * offsetTiling.zw + offsetTiling.xy;
	vec4 result = textureLod(textures[texIndex], texCoords, 0.0);
	return result;
}


struct HitInfo
{
	vec3 color;
	vec3 pbrData;
	vec3 worldPosition;
	vec3 worldNormal;
	vec3 emission;
	float transmission;
};

HitInfo getObjectHitInfo(const int primitiveID, const int customIndex, const vec3 bary) {
	HitInfo result;

	// Read the triangle indices from the index buffer:
	const ivec3 indices = ivec3(texelFetch(indexBuffers[customIndex], primitiveID).rgb);

	// Use barycentric coordinates to compute the interpolated uv coordinates:
	const vec2 uv0 = texelFetch(texCoordsBuffers[customIndex], indices.x).st;
	const vec2 uv1 = texelFetch(texCoordsBuffers[customIndex], indices.y).st;
	const vec2 uv2 = texelFetch(texCoordsBuffers[customIndex], indices.z).st;
	const vec2 uv = (bary.x * uv0 + bary.y * uv1 + bary.z * uv2);

	// Use barycentric coordinates to compute the interpolated normals
	const vec3 nrm0 = texelFetch(normalsBuffers[customIndex], indices.x).rgb;
	const vec3 nrm1 = texelFetch(normalsBuffers[customIndex], indices.y).rgb; 
	const vec3 nrm2 = texelFetch(normalsBuffers[customIndex], indices.z).rgb;
	vec3 normalWS = (bary.x * nrm0 + bary.y * nrm1 + bary.z * nrm2);

	const vec3 tng0 = texelFetch(tangentsBuffers[customIndex], indices.x).rgb;
	const vec3 tng1 = texelFetch(tangentsBuffers[customIndex], indices.y).rgb; 
	const vec3 tng2 = texelFetch(tangentsBuffers[customIndex], indices.z).rgb;
	vec3 tangentWS = (bary.x * tng0 + bary.y * tng1 + bary.z * tng2);

	const vec3 bitng0 = texelFetch(bitangentsBuffers[customIndex], indices.x).rgb;
	const vec3 bitng1 = texelFetch(bitangentsBuffers[customIndex], indices.y).rgb; 
	const vec3 bitng2 = texelFetch(bitangentsBuffers[customIndex], indices.z).rgb;
	vec3 bitangentWS = (bary.x * bitng0 + bary.y * bitng1 + bary.z * bitng2);

	vec3 normal = sample_from_normal_texture(customIndex, uv).rgb;

	vec3 T = normalize(tangentWS);
	vec3 B = normalize(bitangentWS);
	vec3 N = normalize(normalWS);
	mat3 TBN = mat3(T,B,N);

	normal = normal * 2.0 - 1.0;
	normal = normalize(TBN * normal);

	result.color = sample_from_diffuse_texture(customIndex, uv).rgb;
	result.pbrData = sample_from_pbr_texture(customIndex, uv).rgb;
	result.worldPosition = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
	result.worldNormal = mix(normalWS, normal, 1.0);
	result.emission = sample_from_emission_texture(customIndex, uv).rgb;
	result.transmission = sample_transmission(customIndex);

	return result;
}



void main() {
	const vec3 bary = vec3(1.0 - hitAttribs.x - hitAttribs.y, hitAttribs.x, hitAttribs.y);
	HitInfo primaryHitInfo = getObjectHitInfo(gl_PrimitiveID, nonuniformEXT(gl_InstanceCustomIndexEXT), bary);

	vec3 cameraPosition = vec3(pushConstants.mCameraTransform[3]);

	float ao = primaryHitInfo.pbrData.r;
	float roughness = primaryHitInfo.pbrData.g;
	float metalness = primaryHitInfo.pbrData.b;
	float transmission = primaryHitInfo.transmission;

	BSDF bsdf = BSDF(
		primaryHitInfo.color,
		primaryHitInfo.emission,
		roughness,
		metalness,
		transmission
	);

	payload.bsdf = bsdf;
	payload.normal = primaryHitInfo.worldNormal;
	payload.position = primaryHitInfo.worldPosition;
	payload.hit = true;
}
