#Shader Compute

#version 450 core

layout(binding = 0, rgba8) writeonly uniform image2D u_OutputImage;
layout(binding = 1, rgba32f) readonly uniform image2D u_InputImage;

layout (push_constant) uniform Uniforms
{
	float Exposure;
} u_Uniforms;

// Based on http://www.oscars.org/science-technology/sci-tech-projects/aces
vec3 ACESTonemap(vec3 color)
{
	mat3 m1 = mat3(
		0.59719, 0.07600, 0.02840,
		0.35458, 0.90834, 0.13383,
		0.04823, 0.01566, 0.83777
	);
	mat3 m2 = mat3(
		1.60475, -0.10208, -0.00327,
		-0.53108, 1.10813, -0.07276,
		-0.07367, -0.00605, 1.07602
	);
	vec3 v = m1 * color;
	vec3 a = v * (v + 0.0245786) - 0.000090537;
	vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
	return clamp(m2 * (a / b), 0.0, 1.0);
}

vec3 GammaCorrect(vec3 color, float gamma)
{
	return pow(color, vec3(1.0 / gamma));
}

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;
void main()
{
	const float gamma = 2.2;

	// restrict range
	ivec2 inputImageSize = imageSize(u_InputImage);
	if (gl_GlobalInvocationID.x >= inputImageSize.x || gl_GlobalInvocationID.y >= inputImageSize.y )
		return;

	vec3 inputPixel = imageLoad(u_InputImage, ivec2(gl_GlobalInvocationID.xy)).rgb;
	
	inputPixel *= u_Uniforms.Exposure;

	inputPixel = ACESTonemap(inputPixel);
	inputPixel = GammaCorrect(inputPixel, gamma);

	imageStore(u_OutputImage, ivec2(gl_GlobalInvocationID.xy), vec4(inputPixel, 1.0));
}
