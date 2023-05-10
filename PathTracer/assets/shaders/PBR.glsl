#Shader Vertex
#version 450

// Attributes
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec4 a_Tangent;

// Vertex Out
layout(location = 0) out vec3 v_Normal;
layout(location = 1) out vec3 v_WorldPosition;
layout(location = 2) out vec2 v_TexCoord;
layout(location = 3) out vec3 v_WorldNormals;

// Push Constants
layout(push_constant) uniform PushConstantTransform
{
    mat4 Transform;
} TransformData;

// Uniforms
layout(set = 1, binding = 0) uniform CameraBuffer
{
    mat4 ViewProjection;
	mat4 InverseViewProjection;
	vec3 CameraPosition;
} u_CameraBuffer;

void main() 
{
    gl_Position = u_CameraBuffer.ViewProjection * TransformData.Transform * vec4(a_Position, 1.0);

	v_Normal = mat3(TransformData.Transform) * a_Normal;
	v_WorldPosition = vec3(TransformData.Transform * vec4(a_Position, 1.0f));
	v_TexCoord = a_TexCoord;
	v_WorldNormals = inverse(transpose(mat3(TransformData.Transform))) * a_Normal;
}

#Shader Fragment
#version 450

// Fragment In
layout(location = 0) in vec3 v_Normal;
layout(location = 1) in vec3 v_WorldPosition;
layout(location = 2) in vec2 v_TexCoord;
layout(location = 3) in vec3 v_WorldNormals;

// Fragment Out
layout(location = 0) out vec4 outColor;

// DescriptorSets
// Set 0: Constant Uniforms
// Set 1: Renderer Uniforms
// Set 2: Environment Uniforms
// Set 3: Material Uniforms

// Uniforms
layout(set = 1, binding = 0) uniform CameraBuffer
{
	mat4 ViewProjection;
	mat4 InverseViewProjection;
	vec3 CameraPosition;
} u_CameraBuffer;

layout(push_constant) uniform PushConstantMaterial
{
	layout(offset = 64) vec3 AlbedoValue;
	layout(offset = 76) float MetallicValue;
	layout(offset = 80) float RoughnessValue;
	layout(offset = 84) bool UseNormalMap;
} u_MaterialData;

layout(set = 0, binding = 0) uniform sampler2D u_BRDFLutTexture; 
layout(set = 2, binding = 0) uniform samplerCube u_SkyboxTexture;
layout(set = 3, binding = 0) uniform sampler2D u_AlbedoTexture;
layout(set = 3, binding = 1) uniform sampler2D u_MetallicRoughnessTexture;
layout(set = 3, binding = 2) uniform sampler2D u_NormalTexture;

// Constants
const float Epsilon = 0.00001;
const float PI = 3.141592;

// Shlick's approximation of the Fresnel factor.
vec3 fresnelSchlick(vec3 F0, float cosTheta)
{
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnelSchlickRoughness(vec3 F0, float cosTheta, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// GGX/Towbridge-Reitz normal distribution function.
// Uses Disney's reparametrization of alpha = roughness^2
float ndfGGX(float cosLh, float roughness)
{
	float alpha = roughness * roughness;
	float alphaSq = alpha * alpha;

	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
	return alphaSq / (PI * denom * denom);
}

// Single term for separable Schlick-GGX below.
float gaSchlickG1(float cosTheta, float k)
{
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

// Schlick-GGX approximation of geometric attenuation function using Smith's method.
float gaSchlickGGX(float cosLi, float NdotV, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0; // Epic suggests using this roughness remapping for analytic lights.
	return gaSchlickG1(cosLi, k) * gaSchlickG1(NdotV, k);
}

vec3 DirectionalLight_Contribution(vec3 F0, vec3 V, vec3 N, vec3 albedo, float R, float M, float NdotV)
{
	vec3 Li = vec3(1.0f, 1.0f, 1.0f);
	vec3 Lradiance = vec3(1.0f, 1.0f, 1.0f);
	vec3 Lh = normalize(Li + V);

	float cosLi = max(0.0, dot(N, Li));
	float cosLh = max(0.0, dot(N, Lh));

	vec3 F = fresnelSchlick(F0, max(0.0, dot(Lh, V)));
	float D = ndfGGX(cosLh, R);
	float G = gaSchlickGGX(cosLi, NdotV, R);

	vec3 kd = (1.0 - F) * (1.0 - M);
	vec3 diffuseBRDF = kd * albedo;

	vec3 specularBRDF = (F * D * G) / max(Epsilon, 4.0 * cosLi * NdotV);

	return (diffuseBRDF + specularBRDF) * Lradiance * cosLi;
}

vec3 IBL_Contribution(vec3 Lr, vec3 albedo, float R, float M, vec3 N, vec3 V, float NdotV, vec3 F0)
{
	vec3 irradiance = texture(u_SkyboxTexture, N).rgb;
	vec3 F = fresnelSchlickRoughness(F0, NdotV, R);
	vec3 kd = (1.0 - F) * (1.0 - M);
	vec3 diffuseIBL = albedo * irradiance;

	int mipLevels = textureQueryLevels(u_SkyboxTexture);
	float roughness = sqrt(R) * mipLevels;
	vec3 specularIrradiance = textureLod(u_SkyboxTexture, Lr, roughness).rgb * albedo;

	vec2 specularBRDF = texture(u_BRDFLutTexture, vec2(NdotV, 1.0 - R)).rg;
	vec3 specularIBL = specularIrradiance * (F * specularBRDF.x + specularBRDF.y);
	return kd * diffuseIBL + specularIBL;
}

void main() 
{
	vec3 albedo = texture(u_AlbedoTexture, v_TexCoord).rgb * u_MaterialData.AlbedoValue;
	vec2 metallicRoughness = texture(u_MetallicRoughnessTexture, v_TexCoord).bg;
	float metallic = metallicRoughness.x * u_MaterialData.MetallicValue;
	float roughness = metallicRoughness.y * u_MaterialData.RoughnessValue;
	vec3 normal = normalize(v_WorldNormals); // TODO: Fix normal map

	vec3 view = normalize(u_CameraBuffer.CameraPosition - v_WorldPosition);
	float NdotV = max(dot(normal, view), 0.0);
	vec3 Lr = 2.0 * NdotV * normal - view;

	const vec3 Fdielectric = vec3(0.04);
	vec3 F0 = mix(Fdielectric, albedo, metallic);

	vec3 directionalLight_Contribution = DirectionalLight_Contribution(F0, view, normal, albedo, roughness, metallic, NdotV);
	vec3 IBL_Contribution = IBL_Contribution(Lr, albedo, roughness, metallic, normal, view, NdotV, F0);

	outColor.rgb = directionalLight_Contribution + IBL_Contribution;
    outColor.a = 1.0;
}