#Shader ClosestHit
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#include "assets/shaders/RayTracing/Globals.h"

layout(location = 0) rayPayloadInEXT Payload g_RayPayload;

hitAttributeEXT vec2 g_HitAttributes;

layout(std430, binding = 4) buffer Vertices		{ float Data[];	} m_VertexBuffers[];
layout(std430, binding = 5) buffer Indices		{ uint Data[];	} m_IndexBuffers[];
layout(std430, binding = 6) buffer SubmeshData	{ uint Data[];	} m_SubmeshData;
layout(std430, binding = 8) buffer Materials	{ float Data[];	} m_Materials;
layout(binding = 9) uniform sampler2D u_Textures[];

struct Vertex
{
	vec3 Position;      // 12
	vec2 TextureCoords; // 20
	vec3 Normal;        // 32
	vec4 Tangent;       // 48
	vec3 Binormal;      // 60
};

struct Material
{
	vec3 AlbedoValue;				// 12
	float MetallicValue;			// 16
	float RoughnessValue;			// 20
	vec3 EmissiveValue;				// 32
	float EmissiveStrength;			// 36
	uint UseNormalMap;				// 40

	int AlbedoMapIndex;				// 44
	int MetallicRoughnessMapIndex;	// 48
	int NormalMapIndex;				// 52
};

Vertex UnpackVertex(uint vertexBufferIndex, uint index, uint vertexOffset)
{
	index += vertexOffset;

	Vertex vertex;

	const int stride = 48;
	const int offset = stride / 4;

	vertex.Position = vec3(
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 0],
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 1],
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 2]
	);

	vertex.TextureCoords = vec2(
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 3],
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 4]
	);

	vertex.Normal = vec3(
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 5],
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 6],
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 7]
	);

	vertex.Tangent = vec4(
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 8],
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 9],
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 10],
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 11]
	);

	vertex.Binormal = cross(normalize(vertex.Tangent.xyz), normalize(vertex.Normal)) * vertex.Tangent.w;

	return vertex;
}

Vertex InterpolateVertex(Vertex vertices[3], vec3 barycentrics)
{
	Vertex vertex;
	vertex.Position = vec3(0.0);
	vertex.TextureCoords = vec2(0.0);
	vertex.Normal = vec3(0.0);
	vertex.Tangent = vec4(0.0);
	vertex.Binormal = vec3(0.0);
	
	for (uint i = 0; i < 3; i++)
	{
		vertex.Position += vertices[i].Position * barycentrics[i];
		vertex.TextureCoords += vertices[i].TextureCoords * barycentrics[i];
		vertex.Normal += vertices[i].Normal * barycentrics[i];
		vertex.Tangent += vertices[i].Tangent * barycentrics[i];
		vertex.Binormal += vertices[i].Binormal * barycentrics[i];
	}

	vertex.Normal = normalize(vertex.Normal);
	vertex.Tangent = normalize(vertex.Tangent);
	vertex.Binormal = normalize(vertex.Binormal);

	return vertex;	
}

Material UnpackMaterial(uint materialIndex)
{
	const uint stride = 52;
	const uint offset = materialIndex * (stride / 4);

	Material material;

	material.AlbedoValue = vec3(m_Materials.Data[offset + 0], m_Materials.Data[offset + 1], m_Materials.Data[offset + 2]);
	material.MetallicValue = m_Materials.Data[offset + 3];
	material.RoughnessValue = m_Materials.Data[offset + 4];
	material.EmissiveValue = vec3(m_Materials.Data[offset + 5], m_Materials.Data[offset + 6], m_Materials.Data[offset + 7]);
	material.EmissiveStrength = m_Materials.Data[offset + 8];
	material.UseNormalMap = floatBitsToUint(m_Materials.Data[offset + 9]);

	material.AlbedoMapIndex = floatBitsToInt(m_Materials.Data[offset + 10]);
	material.MetallicRoughnessMapIndex = floatBitsToInt(m_Materials.Data[offset + 11]);
	material.NormalMapIndex = floatBitsToInt(m_Materials.Data[offset + 12]);

	return material;
}

void main()
{
	// Collect the vertex data for the triangle that was hit
	uint bufferIndex = m_SubmeshData.Data[gl_InstanceCustomIndexEXT * 4 + 0];
	uint vertexOffset = m_SubmeshData.Data[gl_InstanceCustomIndexEXT * 4 + 1];
	uint indexOffset = m_SubmeshData.Data[gl_InstanceCustomIndexEXT * 4 + 2];
	uint materialIndex = m_SubmeshData.Data[gl_InstanceCustomIndexEXT * 4 + 3];

	Material material = UnpackMaterial(materialIndex);

	uint index0 = m_IndexBuffers[bufferIndex].Data[gl_PrimitiveID * 3 + 0 + indexOffset];
	uint index1 = m_IndexBuffers[bufferIndex].Data[gl_PrimitiveID * 3 + 1 + indexOffset];
	uint index2 = m_IndexBuffers[bufferIndex].Data[gl_PrimitiveID * 3 + 2 + indexOffset];
	
	Vertex vertices[3] = Vertex[](
		UnpackVertex(bufferIndex, index0, vertexOffset),
		UnpackVertex(bufferIndex, index1, vertexOffset),
		UnpackVertex(bufferIndex, index2, vertexOffset)
	);

	// Interpolate the vertex using barycentrics 
	vec3 barycentrics = vec3(1.0 - g_HitAttributes.x - g_HitAttributes.y, g_HitAttributes.x, g_HitAttributes.y);
	Vertex vertex = InterpolateVertex(vertices, barycentrics);

	// Organize the data
	vec3 worldPosition = gl_ObjectToWorldEXT * vec4(vertex.Position, 1.0);
	vec3 worldNormal = normalize(mat3(gl_ObjectToWorldEXT) * vertex.Normal);
	mat3 worldNormalMatrix = mat3(gl_ObjectToWorldEXT) * mat3(vertex.Tangent.xyz, vertex.Binormal, vertex.Normal);
	worldNormalMatrix =  mat3(normalize(worldNormalMatrix[0]), normalize(worldNormalMatrix[1]), normalize(worldNormalMatrix[2]));
	vec3 view = normalize(-gl_WorldRayDirectionEXT);

	// Load the textures if they exist
	vec3 AlbedoTextureValue = vec3(1.0);
	if (material.AlbedoMapIndex != -1)
		AlbedoTextureValue = texture(u_Textures[material.AlbedoMapIndex], vertex.TextureCoords).rgb;

	vec2 MetallicRoughnessMapTextureValue = vec2(1.0);
	if (material.MetallicRoughnessMapIndex != -1)
		MetallicRoughnessMapTextureValue = texture(u_Textures[material.MetallicRoughnessMapIndex], vertex.TextureCoords).bg;

	vec3 NormalMapTextureValue = vec3(1.0);
	if (material.NormalMapIndex != -1)
		NormalMapTextureValue = texture(u_Textures[material.NormalMapIndex], vertex.TextureCoords).rgb;

	// If using a normal map apply it 
	if (false && material.UseNormalMap == 1.0 && material.NormalMapIndex != -1)
	{
		vec3 normal = normalize(NormalMapTextureValue * 2.0 - 1.0);
		normal = normalize(worldNormalMatrix * normal);

		worldNormal = normal;
	}

	// Fill the payload
	g_RayPayload.Distance			= gl_RayTmaxEXT;
	g_RayPayload.Albedo				= material.AlbedoValue * AlbedoTextureValue;
	g_RayPayload.Roughness			= material.RoughnessValue * MetallicRoughnessMapTextureValue.y;
	g_RayPayload.Metallic			= material.MetallicValue * MetallicRoughnessMapTextureValue.x;
	g_RayPayload.Emission			= material.EmissiveValue * material.EmissiveStrength;
	g_RayPayload.WorldPosition		= worldPosition;
	g_RayPayload.WorldNormal		= worldNormal;
	g_RayPayload.WorldNormalMatrix	= worldNormalMatrix;
	g_RayPayload.Binormal			= vertex.Binormal;
	g_RayPayload.Binormal			= normalize(mat3(gl_ObjectToWorldEXT) * vertex.Binormal);
	g_RayPayload.Tangent			= vec3(vertex.Tangent.xyz);
	g_RayPayload.Tangent			= normalize(mat3(gl_ObjectToWorldEXT) * vec3(vertex.Tangent.xyz));
	g_RayPayload.View				= view;
	g_RayPayload.WorldRayDirection	= gl_WorldRayDirectionEXT;

	g_RayPayload.Anisotropic = 0.0;

	g_RayPayload.Roughness = 0.04;
	g_RayPayload.Metallic = 0.0;

	if (gl_InstanceCustomIndexEXT == 5)
		g_RayPayload.Emission = vec3(2.0);

	// NEW
	float aspect = sqrt(1.0 - g_RayPayload.Anisotropic * 0.9);
    g_RayPayload.ax = max(0.001, g_RayPayload.Roughness / aspect);
    g_RayPayload.ay = max(0.001, g_RayPayload.Roughness * aspect);

	g_RayPayload.ior = 1.0;

	g_RayPayload.eta = dot(view, worldNormal) < 0.0 ? (1.0 / g_RayPayload.ior ) : g_RayPayload.ior;

	// gl_InstanceCustomIndexEXT: Cornell Box
	// 0:  Back wall
	// 1:  Ceiling
	// 2:  Floor
	// 3:  Right wall
	// 4:  Left wall
	// 5:  Small box
	// 6:  Large box
	// 7:  Sphere
	// 8:  Suzanne body
	// 9:  Light
	// 10: Suzanne eyes

}