#Shader RayGen
#version 460
#extension GL_EXT_ray_tracing : require
#include "assets/shaders/RayTracing/Globals.h"

layout(binding = 0) uniform accelerationStructureEXT u_TopLevelAS;

layout (binding = 1, rgba8) uniform image2D o_Image;
layout (binding = 2, rgba32f) uniform image2D o_AccumulationImage;
layout (binding = 10) uniform samplerCube u_Skybox;

struct Ray
{
	vec3 Origin;
	vec3 Direction;
	float TMin;
	float TMax;
};

layout(binding = 3) uniform CameraBuffer
{
	mat4 ViewProjection;
	mat4 InverseViewProjection;
	mat4 View;
	mat4 InverseView;
	mat4 InverseProjection;
} u_CameraBuffer;

layout(binding = 7) uniform SceneBuffer
{
	uint FrameIndex;
} u_SceneData;

const float PI = 3.14159265359;

layout(location = 0) rayPayloadEXT Payload g_RayPayload;

// ----------------------------------------------------------------------------

uint NextRandom(inout uint seed)
{
	seed = seed * 747796405 + 2891336453;
	uint result = ((seed >> ((seed >> 28) + 4)) ^ seed) * 277803737;
	result = (result >> 22) ^ result;
	return result;
}

float RandomValue(inout uint seed)
{
	return NextRandom(seed) / 4294967295.0;
}

vec2 RandomPointInCircle(inout uint seed)
{
	float angle = RandomValue(seed) * 2 * PI;
	vec2 pointOnCircle = vec2(cos(angle), sin(angle));
	return pointOnCircle * sqrt(RandomValue(seed));
}

// ----------------------------------------------------------------------------

vec3 TracePath(Ray ray, inout uint seed)
{
	uint flags = gl_RayFlagsOpaqueEXT;
	uint mask = 0xff;

	const int MAX_BOUNCES = 10;
	
	for (int bounceIndex = 0; bounceIndex < MAX_BOUNCES; bounceIndex++)
	{
		traceRayEXT(u_TopLevelAS, flags, mask, 0, 0, 0, ray.Origin, ray.TMin, ray.Direction, ray.TMax, 0);
		Payload payload = g_RayPayload;

		mat3 tangentToWorld = payload.WorldNormalMatrix;
		const vec3 positionWS = payload.WorldPosition;
		const vec3 incomingRayOriginWS = gl_WorldRayOriginEXT;
		const vec3 incomingRayDirWS = gl_WorldRayDirectionEXT;
		vec3 normalWS = payload.WorldNormal;

		vec3 baseColor = payload.Albedo;
		const float metallic = payload.Metallic;
		const float roughness = payload.Roughness;
		vec3 radiance = payload.Emission;

		const vec3 diffuseAlbedo = mix(baseColor, 0.0, metallic);
		const vec3 specularAlbedo = mix(0.04, baseColor, metallic);

		float selector = RandomValue(seed);
		
		vec3 throughput = 0.0;
		vec3 rayDirTS = 0.0;

		if (selector < 0.5)
		{
			
		}
		else
		{
		}

	}

	return vec3(0.0);
}

void main()
{
	uint seed = gl_LaunchIDEXT.x + gl_LaunchIDEXT.y * gl_LaunchSizeEXT.x;
	seed *= u_SceneData.FrameIndex;

	vec3 color = vec3(0.0);

	const uint SAMPLE_COUNT = 5;
	for (uint i = 0; i < SAMPLE_COUNT; i++)
	{
		vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);

		if (i > 0)
        {
			pixelCenter += RandomPointInCircle(seed);
        }

		vec2 inUV = pixelCenter / vec2(gl_LaunchSizeEXT.xy);
 		vec2 d = inUV * 2.0 - 1.0;

		vec4 target = u_CameraBuffer.InverseProjection * vec4(d.x, d.y, 1, 1);
		vec4 direction = u_CameraBuffer.InverseView * vec4(normalize(target.xyz / target.w), 0);

		Ray ray;
		ray.Origin = u_CameraBuffer.InverseView[3].xyz;
		ray.Direction = normalize(direction.xyz);
		ray.TMin = 0.0;
		ray.TMax = 1e27f;

		color += TracePath(ray, seed);
	}

	float numPaths = SAMPLE_COUNT;
	if (u_SceneData.FrameIndex > 1)
	{	
		// Load the accumulation image, W component is the numPaths.
		vec4 data = imageLoad(o_AccumulationImage, ivec2(gl_LaunchIDEXT.xy));
		vec3 previousColor = data.xyz;
		float numPreviousPaths = data.w;

		// Add previous color to current color.
		color += previousColor;
		numPaths = numPreviousPaths + SAMPLE_COUNT;

		imageStore(o_AccumulationImage, ivec2(gl_LaunchIDEXT.xy), vec4(color, numPaths));
	}
	else
	{
		// On the first frame, fill the accumulation image with black.
		imageStore(o_AccumulationImage, ivec2(gl_LaunchIDEXT.xy), vec4(0.0));
	}

	color /= numPaths;
	imageStore(o_Image, ivec2(gl_LaunchIDEXT.xy), vec4(color, 1));
}