#pragma once
#include "Core/Layer.h"
#include "Graphics/Mesh.h"
#include "Graphics/Camera.h"
#include "Graphics/Image.h"
#include "Graphics/VulkanBuffers.h"
#include "Graphics/RenderCommandBuffer.h"
#include "Graphics/AccelerationStructure.h"
#include "Graphics/RayTracingPipeline.h"
#include "Graphics/ComputePipeline.h"
#include <vulkan/vulkan.h>

using namespace VkLibrary;

struct PointLight
{
	glm::vec3 Position;
	float padding0;
	glm::vec3 Color;
	float Intensity;
	uint32_t Active;
};

struct CameraBuffer
{
	glm::mat4 ViewProjection;
	glm::mat4 InverseViewProjection;
	glm::mat4 View;
	glm::mat4 InverseView;
	glm::mat4 InverseProjection;
};

struct SceneBuffer
{
	PointLight PointLight;
	uint32_t FrameIndex;
};

class RayTracingLayer : public Layer
{
	public:
		RayTracingLayer(const std::string& name);
		~RayTracingLayer();

	public:
		void OnAttach();
		void OnDetach();

		void OnUpdate();
		void OnRender();

		void OnImGUIRender();

	private:
		void RayTracingPass();
		bool CreateRayTracingPipeline();

		VkDescriptorPool CreateDescriptorPool();

	private:
		Ref<Mesh> m_Mesh;

		bool m_NeedsResize = false;

		Ref<Camera> m_Camera;
		CameraBuffer m_CameraBuffer;
		Ref<UniformBuffer> m_CameraUniformBuffer;
		
		Ref<RenderCommandBuffer> m_RenderCommandBuffer;
		std::vector<VkWriteDescriptorSet> m_WriteDescriptors;
		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

		Ref<RayTracingPipeline> m_RayTracingPipeline;
		Ref<AccelerationStructure> m_AccelerationStructure;
		VkDescriptorSet m_RayTracingDescriptorSet = VK_NULL_HANDLE;
		Ref<Image> m_Image;
		Ref<Image> m_AccumulationImage;
		bool m_Accumulate = true;

		SceneBuffer m_SceneBuffer;
		Ref<UniformBuffer> m_SceneUniformBuffer;

		Ref<Image> m_Skybox;
		Ref<Shader> m_PreethamSkyComputeShader;
		Ref<ComputePipeline> m_PreethamSkyComputePipeline;
		VkDescriptorSet m_PreethamSkyComputeDescriptorSet = VK_NULL_HANDLE;
		glm::vec3 m_SkyboxSettings = { 3.14f, 0.0f, 0.0f };
		bool m_UpdateSkyBox = true;

		VkDescriptorSet m_ViewportImageDescriptorSet = VK_NULL_HANDLE;
		VkImageView m_ViewportImageView = VK_NULL_HANDLE;
		uint32_t m_ViewportWidth = 1;
		uint32_t m_ViewportHeight = 1;
};