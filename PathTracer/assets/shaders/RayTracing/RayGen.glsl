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

float RandomValueNormalDistribution(inout uint seed)
{
	float theta = 2 * PI * RandomValue(seed);
	float rho = sqrt(-2 * log(RandomValue(seed)));
	return rho * cos(theta);
}

vec3 RandomDirection(inout uint seed)
{
	float x = RandomValueNormalDistribution(seed);
	float y = RandomValueNormalDistribution(seed);
	float z = RandomValueNormalDistribution(seed);
	return normalize(vec3(x, y, z));
}

vec2 RandomPointInCircle(inout uint seed)
{
	float angle = RandomValue(seed) * 2 * PI;
	vec2 pointOnCircle = vec2(cos(angle), sin(angle));
	return pointOnCircle * sqrt(RandomValue(seed));
}

vec3 SampleCosineWeightedHemisphere(vec3 normal, inout uint seed)
{
    vec2 u = vec2(RandomValue(seed), RandomValue(seed));

    float r = sqrt(u.x);
    float theta = 2.0 * PI * u.y;
 
    vec3 B = normalize( cross( normal, vec3(0.0,1.0,1.0) ) );
	vec3 T = cross( B, normal );
    
    return normalize(r * sin(theta) * B + sqrt(1.0 - u.x) * normal + r * cos(theta) * T);
}

// ----------------------------------------------------------------------------

vec3 FresnelSchlickRoughness(vec3 F0, float cosTheta, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float NdfGGX(float cosLh, float roughness)
{
	float alpha = roughness * roughness;
	float alphaSq = alpha * alpha;

	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
	return alphaSq / (PI * denom * denom);
}

float GaSchlickG1(float cosTheta, float k)
{
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

float GaSchlickGGX(float cosLi, float NdotV, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	return GaSchlickG1(cosLi, k) * GaSchlickG1(NdotV, k);
}

// Returns next ray direction
vec3 SampleMicrofacetBRDF(Ray ray, Payload payload, inout uint seed, out vec3 throughput)
{
	vec3 F0 = mix(vec3(0.04), payload.Albedo, payload.Metallic);

	payload.Roughness = max(0.05, payload.Roughness);

	bool specular = RandomValue(seed) > 0.5;

	if (specular) // Specular pass
	{
		float r2 = payload.Roughness * payload.Roughness;
		float theta = acos(sqrt((1.0 - RandomValue(seed)) / (1.0 + (r2 * r2 - 1.0) * RandomValue(seed))));
		float phi = 2.0 * PI * RandomValue(seed);

		vec3 dir = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));

		vec3 H = normalize(payload.WorldNormal + dir * r2);
		vec3 L = reflect(-payload.View, H);

		float cosLh = clamp(dot(payload.WorldNormal, H), 0.0, 1.0);
		float cosLi = clamp(dot(payload.WorldNormal, L), 0.0, 1.0);
		float NdotV = clamp(dot(payload.WorldNormal, payload.View), 0.0, 1.0);
		float VdotH = clamp(dot(payload.View, H), 0.0, 1.0);

		vec3 F = FresnelSchlickRoughness(F0, max(0.0, clamp(dot(H, payload.View), 0.0, 1.0)), payload.Roughness);
		float D = NdfGGX(cosLh, payload.Roughness);
		float G = GaSchlickGGX(cosLi, NdotV, payload.Roughness);

		throughput = F * G * VdotH / max(0.0001, cosLi * NdotV);
		throughput *= 2.0;

		return L;
	}
	else // Diffuse pass
	{
		float theta = asin(sqrt(RandomValue(seed)));
		float phi = 2.0 * PI * RandomValue(seed);

		vec3 dir = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));

		vec3 L = normalize(payload.WorldNormal + dir);
		vec3 H = normalize(payload.View + L);
	
		vec3 F = FresnelSchlickRoughness(F0, max(0.0, clamp(dot(H, payload.View), 0.0, 1.0)), payload.Roughness);
		vec3 kd = (1.0 - F) * (1.0 - payload.Metallic);

		throughput = kd * payload.Albedo;
		throughput *= 2.0;

		return L;
	}
}

// ----------------------------------------------------------------------------

// NOTE: Image gets far too bright with too many bounces
// NOTE: Image is far more grainy with small high intensity emissive light vs skybox

vec3 TracePath(Ray ray, inout uint seed)
{
	uint flags = gl_RayFlagsOpaqueEXT;
	uint mask = 0xff;

	vec3 totalThroughput = vec3(1.0); // the amount of energy reflected vs. absorbed
	vec3 directThroughput = vec3(0.0);
	vec3 light = vec3(0.0);

	const int MAX_BOUNCES = 50;
	
	for (int bounceIndex = 0; bounceIndex < MAX_BOUNCES; bounceIndex++)
	{
		traceRayEXT(u_TopLevelAS, flags, mask, 0, 0, 0, ray.Origin, ray.TMin, ray.Direction, ray.TMax, 0);
		Payload payload = g_RayPayload;

		if (payload.Distance == -1)
		{
			vec3 skyColor = vec3(0.53, 0.80, 0.92);
			light += skyColor * totalThroughput;

			break;
		}
	
		vec3 BRDFThroughput;
		ray.Direction = SampleMicrofacetBRDF(ray, payload, seed, BRDFThroughput);
		totalThroughput *= BRDFThroughput;

		directThroughput += payload.Emission;
		directThroughput *= 0.7;

		ray.Origin = payload.WorldPosition + (payload.WorldNormal * 0.00001 - ray.Direction * 0.00001);
	}

	return light + directThroughput;
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