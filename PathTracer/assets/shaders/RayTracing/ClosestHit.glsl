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
	vec3 Position;
	vec2 TextureCoords;
	vec3 Normal;
	vec4 Tangent;
	vec3 Binormal;
};

struct Material
{
	vec3 AlbedoValue;
	float MetallicValue;
	float RoughnessValue;
	uint UseNormalMap;

	uint AlbedoMapIndex;
	uint MetallicRoughnessMapIndex;
	uint NormalMapIndex;
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
		m_VertexBuffers[vertexBufferIndex].Data[offset * index +  8],
		m_VertexBuffers[vertexBufferIndex].Data[offset * index +  9],
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 10],
		m_VertexBuffers[vertexBufferIndex].Data[offset * index + 11]
	);

	vertex.Binormal = cross(normalize(vertex.Normal), normalize(vertex.Tangent.xyz)) * vertex.Tangent.w;

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
	uint offset = materialIndex * 9;

	Material material;

	material.AlbedoValue = vec3(m_Materials.Data[offset + 0], m_Materials.Data[offset + 1], m_Materials.Data[offset + 2]);
	material.MetallicValue = m_Materials.Data[offset + 3];
	material.RoughnessValue = m_Materials.Data[offset + 4];
	material.UseNormalMap = floatBitsToUint(m_Materials.Data[offset + 5]);

	material.AlbedoMapIndex = floatBitsToUint(m_Materials.Data[offset + 6]);
	material.MetallicRoughnessMapIndex = floatBitsToUint(m_Materials.Data[offset + 7]);
	material.NormalMapIndex = floatBitsToUint(m_Materials.Data[offset + 8]);

	return material;
}

void main()
{
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

	// Weight to each vertex
	vec3 barycentrics = vec3(1.0 - g_HitAttributes.x - g_HitAttributes.y, g_HitAttributes.x, g_HitAttributes.y);
	Vertex vertex = InterpolateVertex(vertices, barycentrics);

	vec3 worldPosition = gl_ObjectToWorldEXT * vec4(vertex.Position, 1.0);
	vec3 worldNormal = normalize(mat3(gl_ObjectToWorldEXT) * vertex.Normal);
	vec3 view = normalize(-gl_WorldRayDirectionEXT);

	g_RayPayload.Distance			= gl_RayTmaxEXT;
	g_RayPayload.Albedo				= material.AlbedoValue;
	g_RayPayload.Roughness			= material.RoughnessValue;
	g_RayPayload.Metallic			= material.MetallicValue;
	g_RayPayload.WorldPosition		= worldPosition;
	g_RayPayload.WorldNormal		= worldNormal;
	g_RayPayload.WorldNormalMatrix	= mat3(vertex.Tangent.xyz, vertex.Binormal, vertex.Normal);
	g_RayPayload.Tangent			= vertex.Tangent.xyz;
	g_RayPayload.View				= view;
}