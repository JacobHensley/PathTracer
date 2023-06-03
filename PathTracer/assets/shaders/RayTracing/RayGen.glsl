#Shader RayGen
#version 460
#extension GL_EXT_ray_tracing : require
#include "assets/shaders/RayTracing/Globals.h"

layout(binding = 0) uniform accelerationStructureEXT u_TopLevelAS;

layout (binding = 1, rgba8) uniform image2D o_Image;
layout (binding = 2, rgba32f) uniform image2D o_AccumulationImage;

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
	vec3 DirectionalLight_Direction;
	vec3 PointLight_Position;
	uint FrameIndex;
} u_SceneData;

struct Ray
{
	vec3 Origin;
	vec3 Direction;
	float TMin;
	float TMax;
};

layout(location = 0) rayPayloadEXT Payload g_RayPayload;

vec3 TracePath(Ray ray, uint seed)
{
	vec3 color = vec3(0.0f);

	uint flags = gl_RayFlagsOpaqueEXT;
	uint mask = 0xff;

	traceRayEXT(u_TopLevelAS, flags, mask, 0, 0, 0, ray.Origin, ray.TMin, ray.Direction, ray.TMax, 0);

	Payload payload = g_RayPayload;

	return payload.Albedo;
}

void main()
{
	uint seed = gl_LaunchIDEXT.x + gl_LaunchIDEXT.y * gl_LaunchSizeEXT.x;
	seed *= u_SceneData.FrameIndex;

	vec3 color = vec3(0.0);

	const uint SAMPLE_COUNT = 2;
	for (uint i = 0; i < SAMPLE_COUNT; i++)
	{
		vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);

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

		imageStore(o_AccumulationImage, ivec2(gl_LaunchIDEXT.xy), vec4(color, numPaths ));
	}
	else
	{
		// If it is the first frame, fill the accumulation image with black.
		imageStore(o_AccumulationImage, ivec2(gl_LaunchIDEXT.xy), vec4(0.0));
	}

	color /= numPaths;
	imageStore(o_Image, ivec2(gl_LaunchIDEXT.xy), vec4(color, 1));
}