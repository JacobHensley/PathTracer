#Shader Vertex
#version 450 core

layout(location = 0) out vec3 v_Position;

layout(set = 1, binding = 0) uniform CameraBuffer
{
    mat4 ViewProjection;
	mat4 InverseViewProjection;
	vec3 CameraPosition;
} u_CameraBuffer;

vec2 c_Vertices[4] = vec2[4] (
    vec2(-1.0f, -1.0f),
    vec2( 1.0f, -1.0f),
    vec2( 1.0f,  1.0f),
    vec2(-1.0f,  1.0f)
);

void main()
{
	vec2 UV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	vec4 position  = vec4(UV * 2.0f + -1.0f, 1.0f, 1.0f);

	v_Position = (u_CameraBuffer.InverseViewProjection * position).xyz;
	gl_Position = position;
}

#Shader Fragment
#version 450 core

layout(location = 0) in vec3 v_Position;

layout(location = 0) out vec4 color;

layout(set = 2, binding = 0) uniform samplerCube u_SkyboxTexture;

void main()
{
	color = texture(u_SkyboxTexture, v_Position);
}