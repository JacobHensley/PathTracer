#Shader RayGen
#version 460
#extension GL_EXT_ray_tracing : require

#include "assets/shaders/RayTracing/Disney.glsl"

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

layout(location = 0) rayPayloadEXT Payload g_RayPayload;

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

vec3 DisneySample2(Payload payload, vec3 V, vec3 N, out vec3 L, out float pdf, inout uint seed)
{
	return vec3(0.0);
}

vec3 TracePath(Ray ray, inout uint seed)
{
	uint flags = gl_RayFlagsOpaqueEXT;
	uint mask = 0xff;

	const int MAX_BOUNCES = 5;

	vec3 radiance = vec3(0.0);
	vec3 throughput = vec3(1.0);
	
	vec3 specularComponent = vec3(0.0);

	bool surfaceScatter = false;

	ScatterSampleRec scatterSample;

	for (int bounceIndex = 0; bounceIndex < MAX_BOUNCES; bounceIndex++)
	{
		traceRayEXT(u_TopLevelAS, flags, mask, 0, 0, 0, ray.Origin, ray.TMin, ray.Direction, ray.TMax, 0);
		Payload payload = g_RayPayload;

		// MISS
		if (payload.Distance < 0.0)
		{
			// Miss, hit sky light
			const vec3 skyColor = vec3(0.7, 0.75, 0.95) * 1.0;
			radiance += skyColor * throughput;

// TODO: ENV MAP
#ifdef OPT_ENVMAP
                vec4 envMapColPdf = EvalEnvMap(r);

                float misWeight = 1.0;

                // Gather radiance from envmap and use scatterSample.pdf from previous bounce for MIS
                if (state.depth > 0)
                    misWeight = PowerHeuristic(scatterSample.pdf, envMapColPdf.w);

			#if defined(OPT_MEDIUM) && !defined(OPT_VOL_MIS)
                if(!surfaceScatter)
                    misWeight = 1.0f;
			#endif

                if(misWeight > 0)
                    radiance += misWeight * envMapColPdf.rgb * throughput * envMapIntensity;
#endif
             break;
        }

		radiance += payload.Emission * throughput;

		{
			surfaceScatter = true;

			// Next event estimation
			// radiance += DirectLight(r, payload, true) * throughput;

			// Sample BSDF for color and outgoing direction
			scatterSample.f = DisneySample(payload, -ray.Direction, payload.WorldNormal, scatterSample.L, scatterSample.pdf, seed);
			if (scatterSample.pdf > 0.0)
				throughput *= scatterSample.f / scatterSample.pdf;
			else
				break;
         }

        // Move ray origin to hit point and set direction for next bounce
		vec3 fhp = ray.Origin + ray.Direction * payload.Distance;

        ray.Direction = scatterSample.L;
		const float EPS = 0.0003;
        ray.Origin = fhp + ray.Direction * EPS;


// TODO: RR
#ifdef OPT_RR
        // Russian roulette
        if (state.depth >= OPT_RR_DEPTH)
        {
            float q = min(max(throughput.x, max(throughput.y, throughput.z)) + 0.001, 0.95);
            if (rand() > q)
                break;
            throughput /= q;
        }
#endif
	}
	return radiance;
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