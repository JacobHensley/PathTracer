
#define PI         3.14159265358979323
#define INV_PI     0.31830988618379067
#define TWO_PI     6.28318530717958648
#define INV_TWO_PI 0.15915494309189533
#define INV_4_PI   0.07957747154594766

struct Payload
{
	float Distance;
	vec3 Albedo;
	float Metallic;
	float Roughness;
	vec3 Emission;
	vec3 WorldPosition;
	vec3 WorldNormal;
	mat3 WorldNormalMatrix;
	vec3 Binormal;
	vec3 Tangent;
	vec3 View;
	vec3 WorldRayDirection;

	// NEW STUFF TO IMPLEMENT
	float Anisotropic;
	float Subsurface;
	float SpecularTint;
	float Sheen;
	float SheenTint;
	float Clearcoat;
	float ClearcoatRoughness;
	float SpecTrans;
	float ior;
	float ax;
	float ay;

	// From State
	float eta;
};

struct ScatterSampleRec
{
	vec3 L;
	vec3 f;
	float pdf;
};

struct LightSampleRec
{
	vec3 normal;
	vec3 emission;
	vec3 direction;
	float dist;
	float pdf;
};

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
