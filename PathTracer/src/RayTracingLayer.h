#pragma once
#include "Core/Layer.h"

#include "Graphics/Mesh.h"
#include "Graphics/Camera.h"
#include "Graphics/Image.h"
#include "Graphics/Texture.h"
#include "Graphics/VulkanBuffers.h"
#include "Graphics/RenderCommandBuffer.h"
#include "Graphics/AccelerationStructure.h"
#include "Graphics/RayTracingPipeline.h"
#include "Graphics/ComputePipeline.h"

#include "ImGui/Panels/ViewportPanel.h"
#include <vulkan/vulkan.h>

using namespace VkLibrary;

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
		void PostProcessingPass();
		bool CreateRayTracingPipeline();
	private:
		Ref<Mesh> m_Mesh;
		glm::mat4 m_Transform;

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
		Ref<Image> m_PostProcessingImage;
		bool m_Accumulate = true;

		SceneBuffer m_SceneBuffer;
		Ref<UniformBuffer> m_SceneUniformBuffer;

		Ref<TextureCube> m_RadianceMap;

		Ref<Image> m_PreethamSkybox;
		Ref<Shader> m_PreethamSkyComputeShader;
		Ref<ComputePipeline> m_PreethamSkyComputePipeline;
		VkDescriptorSet m_PreethamSkyComputeDescriptorSet = VK_NULL_HANDLE;

		Ref<ComputePipeline> m_PostProcessingComputePipeline;
		VkDescriptorSet m_PostProcessingComputeDescriptorSet = VK_NULL_HANDLE;

		glm::vec3 m_SkyboxSettings = { 3.14f, 0.0f, 0.0f };
		bool m_UpdateSkyBox = true;
		bool m_DoPostProcessing = true;

		float m_Exposure = 0.8f;

		Ref<ViewportPanel> m_ViewportPanel;

		int m_SelectedSubMeshIndex = -1;
};