#pragma once
#include "Core/Core.h"
#include "Core/Layer.h"
#include "Graphics/Mesh.h"
#include "Graphics/Shader.h"
#include "Graphics/Camera.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/GraphicsPipeline.h"
#include "Graphics/ComputePipeline.h"
#include "Graphics/RenderCommandBuffer.h"
#include "Graphics/VulkanBuffers.h"
#include "Graphics/VulkanTools.h"
#include "Graphics/AccelerationStructure.h"
#include <vulkan/vulkan.h>

using namespace VkLibrary;

struct CameraBuffer
{
	glm::mat4 ViewProjection;
	glm::mat4 InverseViewProjection;
	glm::vec3 CameraPosition;
};

class AppLayer : public Layer
{
	public:
		AppLayer(const std::string& name);
		~AppLayer();

	public:
		void OnAttach();
		void OnDetach();

		void OnUpdate();
		void OnRender();

		void OnImGUIRender();

	private:
		void BeginRenderPass(Ref<Framebuffer> framebuffer, VkCommandBuffer commandBuffer, bool explicitClear);
		void EndRenderPass(VkCommandBuffer commandBuffer);

		VkWriteDescriptorSet GenerateBufferWriteDescriptor(const std::string& name, Ref<Shader> shader, const VkDescriptorSet descriptorSet, const VkDescriptorBufferInfo bufferInfo);
		VkWriteDescriptorSet GenerateImageWriteDescriptor(const std::string& name, Ref<Shader> shader, const VkDescriptorSet descriptorSet, const VkDescriptorImageInfo imageInfo);

		VkDescriptorPool CreateDescriptorPool();

	private:
		Ref<Mesh> m_Mesh;
		Ref<Framebuffer> m_Framebuffer;
		Ref<Image> m_Skybox;
		Ref<Texture2D> m_BRDFLut;

		Ref<Camera> m_Camera;
		CameraBuffer m_CameraBuffer;
		Ref<UniformBuffer> m_CameraUniformBuffer;
		
		Ref<RenderCommandBuffer> m_RenderCommandBuffer;

		glm::vec3 m_SkyboxSettings = { 3.14f, 0.0f, 0.0f };;

		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

		Ref<Shader> m_PBRShader;
		Ref<Shader> m_PreethamSkyShader;
		Ref<Shader> m_SkyboxShader;

		Ref<GraphicsPipeline> m_GeometryPipeline;
		Ref<GraphicsPipeline> m_SkyboxPipeline;
		Ref<ComputePipeline> m_PreethamSkyPipeline;

		std::array<VkDescriptorSet, 3> m_RendererDescriptorSets;

		VkDescriptorSet m_PreethamSkyDescriptorSet = VK_NULL_HANDLE;

		VkDescriptorPool m_ViewportDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet m_ViewportImageDescriptorSet = VK_NULL_HANDLE;
		VkImageView m_ViewportImageView = VK_NULL_HANDLE;

		Ref<AccelerationStructure> m_AccelerationStructure;
};