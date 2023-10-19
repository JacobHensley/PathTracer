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
const float TWO_PI = PI * 2.0;

layout(location = 0) rayPayloadEXT Payload g_RayPayload;

// ----------------------------------------------------------------------------

uint PCG_Hash(inout uint seed)
{
	seed = seed * 747796405 + 2891336453;
	uint result = ((seed >> ((seed >> 28) + 4)) ^ seed) * 277803737;
	return (result >> 22) ^ result;
}

float RandomValue(inout uint seed)
{
	return PCG_Hash(seed) / 4294967295.0;
}

vec2 RandomPointInCircle(inout uint seed)
{
	float angle = RandomValue(seed) * 2 * PI;
	vec2 pointOnCircle = vec2(cos(angle), sin(angle));
	return pointOnCircle * sqrt(RandomValue(seed));
}

// Compute a cosine distributed random direction on the hemisphere about the given (normal) direction.
vec3 GetRandomCosineDirectionOnHemisphere(vec3 direction, inout uint seed)
{
	// Choose random points on the unit sphere offset along the surface normal
	// to produce a cosine distribution of random directions.
	float a = RandomValue(seed) * TWO_PI;
	float z = RandomValue(seed) * 2.0 - 1.0;
	float r = sqrt(1.0 - z * z);

	vec3 p = vec3(r * cos(a), r * sin(a), z) + direction;
	return normalize(p);
}

// ----------------------------------------------------------------------------
// From DirectX Path Tracing thing
// ----------------------------------------------------------------------------

vec3 SampleGGXVisibleNormal(vec3 wo, float ax, float ay, float u1, float u2)
{
    // Stretch the view vector so we are sampling as though
    // roughness==1
    vec3 v = normalize(vec3(wo.x * ax, wo.y * ay, wo.z));

    // Build an orthonormal basis with v, t1, and t2
    vec3 t1 = (v.z < 0.999) ? normalize(cross(v, vec3(0, 0, 1))) : vec3(1, 0, 0);
    vec3 t2 = cross(t1, v);

    // Choose a point on a disk with each half of the disk weighted
    // proportionally to its projection onto direction v
    float a = 1.0 / (1.0 + v.z);
    float r = sqrt(u1);
    float phi = (u2 < a) ? (u2 / a) * PI : PI + (u2 - a) / (1.0 - a) * PI;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi) * ((u2 < a) ? 1.0 : v.z);

    // Calculate the normal in this stretched tangent space
    vec3 n = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * v;

    // Unstretch and normalize the normal
    return normalize(vec3(ax * n.x, ay * n.y, max(0.0, n.z)));
}

vec3 Fresnel(in vec3 specAlbedo, in vec3 h, in vec3 l)
{
    vec3 fresnel = specAlbedo + (1.0f - specAlbedo) * pow((1.0f - clamp(dot(l, h), 0.0, 1.0)), 5.0f);

    // Fade out spec entirely when lower than 0.1% albedo
    fresnel *= clamp(dot(specAlbedo, vec3(333.0)), 0.0, 1.0);

    return fresnel;
}

float SmithGGXMasking(vec3 n, vec3 l, vec3 v, float a2)
{
    float dotNL = clamp(dot(n, l), 0.0, 1.0);
    float dotNV = clamp(dot(n, v), 0.0, 1.0);
    float denomC = sqrt(a2 + (1.0 - a2) * dotNV * dotNV) + dotNV;

    return 2.0 * dotNV / denomC;
}

float SmithGGXMaskingShadowing(vec3 n, vec3 l, vec3 v, float a2)
{
    float dotNL = clamp(dot(n, l), 0.0, 1.0);
    float dotNV = clamp(dot(n, v), 0.0, 1.0);

    float denomA = dotNV * sqrt(a2 + (1.0f - a2) * dotNL * dotNL);
    float denomB = dotNL * sqrt(a2 + (1.0f - a2) * dotNV * dotNV);

    return 2.0 * dotNL * dotNV / (denomA + denomB);
}


// ----------------------------------------------------------------------------

vec3 TracePath(Ray ray, inout uint seed)
{
	uint flags = gl_RayFlagsOpaqueEXT;
	uint mask = 0xff;

	const int MAX_BOUNCES = 1;

	vec3 radiance = vec3(0.0);
	vec3 throughput = vec3(1.0);
	
	vec3 specularComponent = vec3(0.0);

	for (int bounceIndex = 0; bounceIndex < MAX_BOUNCES; bounceIndex++)
	{
		traceRayEXT(u_TopLevelAS, flags, mask, 0, 0, 0, ray.Origin, ray.TMin, ray.Direction, ray.TMax, 0);
		Payload payload = g_RayPayload;

		if (payload.Distance < 0.0)
		{
			// Miss, hit sky light
			const vec3 skyColor = vec3(0.7, 0.75, 0.95);
			radiance += skyColor * throughput;
			break;
		}

		mat3 tangentToWorld = mat3(payload.Tangent * vec3(1, 1, 1), payload.Binormal * vec3(1, 1, 1), payload.WorldNormal * vec3(1, 1, 1));
		const vec3 positionWS = payload.WorldPosition;
		const vec3 incomingRayOriginWS = ray.Origin; // gl_WorldRayOriginEXT;
		const vec3 incomingRayDirWS = payload.WorldRayDirection; // gl_WorldRayDirectionEXT;
		vec3 normalWS = payload.WorldNormal;

	//	return tangentToWorld[0] * 0.5 + 0.5;

		vec3 baseColor = payload.Albedo;
		float metallic = payload.Metallic;
		float roughness = payload.Roughness;
		roughness = max(0.05, roughness);
		roughness = 0.5;
		metallic=1.0;
		//vec3 radiance = payload.Emission;

		const vec3 diffuseAlbedo = mix(baseColor, vec3(0.0), vec3(metallic));
		const vec3 specularAlbedo = mix(vec3(0.04), baseColor, vec3(metallic));

		float selector = RandomValue(seed);
		vec3 rayDirTS = vec3(0.0);

		// Suspicious
		uint sampleSeed = seed + bounceIndex;
		vec2 brdfSample = vec2(RandomValue(sampleSeed), RandomValue(sampleSeed));

		ray.Origin = positionWS;
		selector = 1.0;
		if (selector < 0.5)
		{
			brdfSample.x *= 2.0f;
			// WS
			ray.Direction = GetRandomCosineDirectionOnHemisphere(normalWS, seed);
			throughput *= diffuseAlbedo;
		}
		else
		{
			//brdfSample.x = (brdfSample.x - 0.5f) * 2.0f;

			vec3 incomingRayDirTS = normalize(transpose(tangentToWorld) * incomingRayDirWS);
			vec3 microfacetNormalTS = SampleGGXVisibleNormal(-incomingRayDirTS, roughness, roughness, brdfSample.x, brdfSample.y);
			vec3 sampleDirTS = reflect(incomingRayDirTS, microfacetNormalTS);

			vec3 normalTS = vec3(0.0, 0.0, 1.0);

			vec3 F = Fresnel(specularAlbedo, microfacetNormalTS, sampleDirTS);
			float G1 = SmithGGXMasking(normalTS, sampleDirTS, -incomingRayDirTS, roughness * roughness);
			float G2 = SmithGGXMaskingShadowing(normalTS, sampleDirTS, -incomingRayDirTS, roughness * roughness);

			ray.Direction = normalize(tangentToWorld * sampleDirTS);
			throughput *= (F * (G2 / G1));

			return microfacetNormalTS;
		}

		throughput *= 2.0;

	}


	//if (all(equal(specularComponent, vec3(0))))
	//	return vec3(0, 1, 0);

	//if (all(lessThan(specularComponent, vec3(0.01))))// && all(greaterThan(specularComponent, vec3(-0.0001))))
	//	return vec3(1, 0, 1);
	
	return throughput;
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
		ray.TMin = 0.00001;
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

	if (any(isnan(color)))
		imageStore(o_Image, ivec2(gl_LaunchIDEXT.xy), vec4(1, 0, 0, 1));
	else
		imageStore(o_Image, ivec2(gl_LaunchIDEXT.xy), vec4(color, 1));
}