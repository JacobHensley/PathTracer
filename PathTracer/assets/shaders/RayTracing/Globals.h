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
};