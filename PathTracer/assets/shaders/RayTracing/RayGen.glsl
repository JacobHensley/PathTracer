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

struct PointLight
{
	vec3 Position;
	vec3 Color;
	float Intensity;
	uint Active;
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
	PointLight PointLight;
	uint FrameIndex;
} u_SceneData;

const float PI = 3.1415;

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

// ----------------------------------------------------------------------------

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ----------------------------------------------------------------------------

vec3 TracePath(Ray ray, uint seed)
{
	uint flags = gl_RayFlagsOpaqueEXT;
	uint mask = 0xff;

	vec3 throughput = vec3(1.0);
	vec3 color = vec3(0.0);

	const int MAX_BOUNCES = 2;
	float bounceCount = 0.0;

	for (int bounceIndex = 0; bounceIndex < MAX_BOUNCES; bounceIndex++)
	{
		bounceCount++;

		traceRayEXT(u_TopLevelAS, flags, mask, 0, 0, 0, ray.Origin, ray.TMin, ray.Direction, ray.TMax, 0);
		Payload payload = g_RayPayload;

		vec3 Lo = vec3(0.0);
		vec3 ambient = vec3(0.0);

		vec3 lightPosition = u_SceneData.PointLight.Position;
		vec3 lightColor = u_SceneData.PointLight.Color;

		vec3 N = payload.WorldNormal;
		vec3 V = normalize(u_CameraBuffer.InverseView[3].xyz - payload.WorldPosition);
		vec3 R = reflect(-V, N);

		vec3 F0 = vec3(0.04); 
		F0 = mix(F0, payload.Albedo, payload.Metallic);

		if (payload.Distance != -1)
		{
			vec3 L = normalize(lightPosition - payload.WorldPosition);
			vec3 H = normalize(V + L);
			float distance = length(lightPosition - payload.WorldPosition);
			float attenuation = 1.0 / (distance * distance);
			vec3 radiance = lightColor * attenuation;

			float NDF = DistributionGGX(N, H, payload.Roughness);   
			float G   = GeometrySmith(N, V, L, payload.Roughness);    
			vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);        
        
			vec3 numerator    = NDF * G * F;
			float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
			vec3 specular = numerator / denominator;
    
			vec3 kS = F;
			vec3 kD = vec3(1.0) - kS;
			kD *= 1.0 - payload.Metallic;	                
            
			float NdotL = max(dot(N, L), 0.0);

			Lo += (kD * payload.Albedo / PI + specular) * radiance * NdotL;
		}
		else
		{
			vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, payload.Roughness);

			vec3 kS = F;
			vec3 kD = 1.0 - kS;
			kD *= 1.0 - payload.Metallic;	  
    
			vec3 irradiance = texture(u_Skybox, N).rgb;
			vec3 diffuse = irradiance * payload.Albedo;

			ambient = (kD * diffuse);

			break;
		}

		color += ambient + Lo;

		ray.Direction = normalize(payload.WorldNormal + RandomDirection(seed));
		ray.Origin = payload.WorldPosition;
		ray.Origin += payload.WorldNormal * 0.0001 - ray.Direction * 0.0001;
	}

	color = color / bounceCount;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2)); 

	return color;
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