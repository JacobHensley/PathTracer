#include "AppLayer.h"
#include "Core/Application.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_vulkan.h"
#include <glm/gtc/type_ptr.hpp>

AppLayer::AppLayer(const std::string& name)
	: Layer("AppLayer")
{
	Ref<VulkanDevice> device = Application::GetApp().GetVulkanDevice();

//	m_Mesh = CreateRef<Mesh>("assets/models/Suzanne/glTF/Suzanne.gltf");
	m_Mesh = CreateRef<Mesh>("assets/models/Sponza/glTF/Sponza.gltf");

	m_RenderCommandBuffer = CreateRef<RenderCommandBuffer>(2);

	FramebufferSpecification fbSpec;
	fbSpec.AttachmentFormats = { ImageFormat::RGBA32F, ImageFormat::DEPTH24_STENCIL8 };
	fbSpec.Width = 1280;
	fbSpec.Height = 720;
	fbSpec.ClearOnLoad = true;
	m_Framebuffer = CreateRef<Framebuffer>(fbSpec);

	ImageSpecification skyboxSpec;
	skyboxSpec.Width = 2048;
	skyboxSpec.Height = 2048;
	skyboxSpec.Format = ImageFormat::RGBA32F;
	skyboxSpec.Usage = ImageUsage::STORAGE_IMAGE_CUBE;
	m_Skybox = CreateRef<Image>(skyboxSpec);

	CameraSpecification cameraSpec;
	m_Camera = CreateRef<Camera>(cameraSpec);

	m_CameraBuffer.ViewProjection = m_Camera->GetViewProjection();
	m_CameraBuffer.InverseViewProjection = m_Camera->GetInverseViewProjection();
	m_CameraBuffer.CameraPosition = m_Camera->GetPosition();
	m_CameraUniformBuffer = CreateRef<UniformBuffer>(&m_CameraBuffer, sizeof(CameraBuffer));

	Texture2DSpecification BRDFTextureSpec;
	BRDFTextureSpec.path = "assets/textures/Brdf_Lut.png";
	m_BRDFLut = CreateRef<Texture2D>(BRDFTextureSpec);

	m_PBRShader = CreateRef<Shader>("assets/shaders/PBR.glsl");
	m_PreethamSkyShader = CreateRef<Shader>("assets/shaders/PreethamSky.glsl");
	m_SkyboxShader = CreateRef<Shader>("assets/shaders/Skybox.glsl");

	m_DescriptorPool = CreateDescriptorPool();
	m_ViewportDescriptorPool = CreateDescriptorPool();

	{
		VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{};
		descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorSetAllocateInfo.pSetLayouts = &ImGui_ImplVulkan_GetDescriptorSetLayout();
		descriptorSetAllocateInfo.descriptorSetCount = 1;
		descriptorSetAllocateInfo.descriptorPool = m_ViewportDescriptorPool;

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device->GetLogicalDevice(), &descriptorSetAllocateInfo, &m_ViewportImageDescriptorSet));
	}

	// Setup system descriptor sets
	{
		m_RendererDescriptorSets[0] = m_PBRShader->AllocateDescriptorSet(m_DescriptorPool, 0);
		m_RendererDescriptorSets[1] = m_PBRShader->AllocateDescriptorSet(m_DescriptorPool, 1);
		m_RendererDescriptorSets[2] = m_PBRShader->AllocateDescriptorSet(m_DescriptorPool, 2);
		std::vector<VkWriteDescriptorSet> writeDescriptors;
		
		writeDescriptors.push_back(GenerateImageWriteDescriptor("u_BRDFLutTexture", m_PBRShader, m_RendererDescriptorSets[0], m_BRDFLut->GetDescriptorImageInfo()));
		writeDescriptors.push_back(GenerateBufferWriteDescriptor("CameraBuffer", m_PBRShader, m_RendererDescriptorSets[1], m_CameraUniformBuffer->GetDescriptorBufferInfo()));
		writeDescriptors.push_back(GenerateImageWriteDescriptor("u_SkyboxTexture", m_PBRShader, m_RendererDescriptorSets[2], m_Skybox->GetDescriptorImageInfo()));
		
		vkUpdateDescriptorSets(device->GetLogicalDevice(), writeDescriptors.size(), writeDescriptors.data(), 0, nullptr);
	}

	// Geometry pass
	{
		GraphicsPipelineSpecification spec;
		spec.Shader = m_PBRShader;
		spec.TargetRenderPass = m_Framebuffer->GetRenderPass();
		m_GeometryPipeline = CreateRef<GraphicsPipeline>(spec);
	}

	// Skybox pass
	{
		GraphicsPipelineSpecification spec;
		spec.Shader = m_SkyboxShader;
		spec.TargetRenderPass = m_Framebuffer->GetRenderPass();
		m_SkyboxPipeline = CreateRef<GraphicsPipeline>(spec);
	}

	// PreethamSky pass
	{
		ComputePipelineSpecification spec;
		spec.Shader = m_PreethamSkyShader;
		m_PreethamSkyPipeline = CreateRef<ComputePipeline>(spec);

		m_PreethamSkyDescriptorSet = m_PreethamSkyShader->AllocateDescriptorSet(m_DescriptorPool, 0);
		std::vector<VkWriteDescriptorSet> writeDescriptors;

		writeDescriptors.push_back(GenerateImageWriteDescriptor("u_CubeMap", m_PreethamSkyShader, m_PreethamSkyDescriptorSet, m_Skybox->GetDescriptorImageInfo()));

		vkUpdateDescriptorSets(device->GetLogicalDevice(), writeDescriptors.size(), writeDescriptors.data(), 0, NULL);
	}

	AccelerationStructureSpecification spec{};
	spec.Mesh = m_Mesh;
	spec.Transform = glm::mat4(1.0f);

	m_AccelerationStructure = CreateRef<AccelerationStructure>(spec);
}

AppLayer::~AppLayer()
{
}

void AppLayer::OnAttach()
{
	
}

void AppLayer::OnDetach()
{
}

void AppLayer::OnUpdate()
{
	Ref<VulkanDevice> device = Application::GetApp().GetVulkanDevice();

	m_Camera->Update();

	// Dispatch PreethamSky compute shader 
	{
		VkCommandBuffer commandBuffer = device->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_PreethamSkyPipeline->GetPipeline());
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_PreethamSkyPipeline->GetPipelineLayout(), 0, 1, &m_PreethamSkyDescriptorSet, 0, nullptr);
		vkCmdPushConstants(commandBuffer, m_PreethamSkyPipeline->GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::vec3), &m_SkyboxSettings);

		vkCmdDispatch(commandBuffer, 64, 64, 6);

		device->FlushCommandBuffer(commandBuffer, true);
	}
}

void AppLayer::OnRender()
{
	Ref<VulkanDevice> device = Application::GetApp().GetVulkanDevice();
	Ref<Swapchain> swapchain = Application::GetApp().GetSwapchain();
	VkCommandBuffer commandBuffer = m_RenderCommandBuffer->GetCommandBuffer();

	m_RenderCommandBuffer->Begin();

	BeginRenderPass(m_Framebuffer, commandBuffer, false);

	// Draw fullscreen triangle with skybox texture
	{
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyboxPipeline->GetPipeline());
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyboxPipeline->GetPipelineLayout(), 0, (uint32_t)m_RendererDescriptorSets.size(), m_RendererDescriptorSets.data(), 0, nullptr);

		vkCmdDraw(commandBuffer, 3, 1, 0, 0);
	}

	// Draw geometry
	{
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GeometryPipeline->GetPipeline());
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GeometryPipeline->GetPipelineLayout(), 0, (uint32_t)m_RendererDescriptorSets.size(), m_RendererDescriptorSets.data(), 0, nullptr);

		VkDeviceSize offset = 0;
		VkBuffer vertexBuffer = m_Mesh->GetVertexBuffer()->GetBuffer();
		glm::mat4 transform = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));

		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
		vkCmdBindIndexBuffer(commandBuffer, m_Mesh->GetIndexBuffer()->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
		vkCmdPushConstants(commandBuffer, m_GeometryPipeline->GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &transform);
		
		const uint32_t materialDescriptorSetIndex = 3;
		for (int i = 0; i < m_Mesh->GetSubMeshes().size(); i++)
		{
			const SubMesh& subMesh = m_Mesh->GetSubMeshes()[i];
			const Material& material = m_Mesh->GetMaterials()[subMesh.MaterialIndex];
			VkDescriptorSet set = material.GetDescriptorSet();
			const auto& desc = material.GetPushConstantRangeDescription();

			vkCmdPushConstants(commandBuffer, m_GeometryPipeline->GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, desc.Offset, desc.Size, material.GetBuffer());
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GeometryPipeline->GetPipelineLayout(), materialDescriptorSetIndex, 1, &set, 0, nullptr);
			vkCmdDrawIndexed(commandBuffer, subMesh.IndexCount, 1, subMesh.IndexOffset, subMesh.VertexOffset, 0);
		}
	}

	EndRenderPass(commandBuffer);

	m_RenderCommandBuffer->End();
	m_RenderCommandBuffer->Submit();
}

void AppLayer::OnImGUIRender()
{
	ImGuiIO io = ImGui::GetIO();
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
	bool open = true;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("Editor", &open, flags);

	// Handle resize
	ImVec2 size = ImGui::GetContentRegionAvail();
	bool resized = m_Framebuffer->Resize(size.x, size.y);
	if (resized)
	{
		m_Camera->Resize(size.x, size.y);
	}
	
	// Update camera uniform buffer
	{
		m_CameraBuffer.ViewProjection = m_Camera->GetViewProjection();
		m_CameraBuffer.InverseViewProjection = m_Camera->GetInverseViewProjection();
		m_CameraBuffer.CameraPosition = m_Camera->GetPosition();

		CameraBuffer* mem = m_CameraUniformBuffer->Map<CameraBuffer>();
		memcpy(mem, &m_CameraBuffer, sizeof(CameraBuffer));
		m_CameraUniformBuffer->Unmap();
	}

	// Update viewport descriptor on reisze
	{
		Ref<VulkanDevice> device = Application::GetApp().GetVulkanDevice();

		const VkDescriptorImageInfo& descriptorInfo = m_Framebuffer->GetImage(0)->GetDescriptorImageInfo();
		if (m_ViewportImageView != descriptorInfo.imageView)
		{
			VkWriteDescriptorSet write_desc{};
			write_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write_desc.dstSet = m_ViewportImageDescriptorSet;
			write_desc.descriptorCount = 1;
			write_desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write_desc.pImageInfo = &descriptorInfo;

			vkUpdateDescriptorSets(device->GetLogicalDevice(), 1, &write_desc, 0, nullptr);
			m_ViewportImageView = descriptorInfo.imageView;
		}
	}

	ImGui::Image(m_ViewportImageDescriptorSet, ImVec2(size.x, size.y), ImVec2::ImVec2(0, 1), ImVec2::ImVec2(1, 0));

	ImGui::End();
	ImGui::PopStyleVar();

	ImGui::Begin("Preetham Sky Settings");
	ImGui::DragFloat("Turbidity",   &m_SkyboxSettings.x, 0.01f, 1.3,  10.0f);
	ImGui::DragFloat("Azimuth",     &m_SkyboxSettings.y, 0.01f, 0, 2 * 3.14);
	ImGui::DragFloat("Inclination", &m_SkyboxSettings.z, 0.01f, 0, 2 * 3.14);
	ImGui::End();
}

void AppLayer::BeginRenderPass(Ref<Framebuffer> framebuffer, VkCommandBuffer commandBuffer, bool explicitClear)
{
	VkClearValue clearColor[2];
	clearColor[0].color = { 0.1f, 0.1f, 0.1f, 1.0f };
	clearColor[1].depthStencil = { 1.0f, 0 };

	VkExtent2D extent = { framebuffer->GetWidth(), framebuffer->GetHeight() };

	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = framebuffer->GetRenderPass();
	renderPassInfo.framebuffer = framebuffer->GetFramebuffer();
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = extent;
	renderPassInfo.clearValueCount = 2;
	renderPassInfo.pClearValues = clearColor;

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)extent.width;
	viewport.height = (float)extent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor;
	scissor.offset = renderPassInfo.renderArea.offset;
	scissor.extent = renderPassInfo.renderArea.extent;

	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	if (explicitClear)
	{
		const uint32_t colorAttachmentCount = (uint32_t)framebuffer->GetColorAttachmentCount();
		const uint32_t totalAttachmentCount = colorAttachmentCount + (framebuffer->HasDepthAttachment() ? 1 : 0);

		VkClearValue clearValues;
		glm::vec4 clearColor = framebuffer->GetSpecification().ClearColor;
		clearValues.color = { clearColor.r, clearColor.g, clearColor.b, clearColor.a };

		std::vector<VkClearAttachment> attachments(totalAttachmentCount);
		std::vector<VkClearRect> clearRects(totalAttachmentCount);

		for (uint32_t i = 0; i < colorAttachmentCount; i++)
		{
			attachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			attachments[i].colorAttachment = i;
			attachments[i].clearValue = clearValues;

			clearRects[i].rect.offset = { (int32_t)0, (int32_t)0 };
			clearRects[i].rect.extent = { extent.width, extent.height };
			clearRects[i].baseArrayLayer = 0;
			clearRects[i].layerCount = 1;
		}

		if (framebuffer->HasDepthAttachment())
		{
			clearValues.depthStencil = { 1.0f, 0 };

			attachments[colorAttachmentCount].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			attachments[colorAttachmentCount].clearValue = clearValues;
			clearRects[colorAttachmentCount].rect.offset = { (int32_t)0, (int32_t)0 };
			clearRects[colorAttachmentCount].rect.extent = { extent.width,  extent.height };
			clearRects[colorAttachmentCount].baseArrayLayer = 0;
			clearRects[colorAttachmentCount].layerCount = 1;
		}

		vkCmdClearAttachments(commandBuffer, totalAttachmentCount, attachments.data(), totalAttachmentCount, clearRects.data());
	}
}

void AppLayer::EndRenderPass(VkCommandBuffer commandBuffer)
{
	vkCmdEndRenderPass(commandBuffer);
}

VkWriteDescriptorSet AppLayer::GenerateBufferWriteDescriptor(const std::string& name, Ref<Shader> shader, const VkDescriptorSet descriptorSet, const VkDescriptorBufferInfo bufferInfo)
{
	VkWriteDescriptorSet writeDescriptor = shader->FindWriteDescriptorSet(name);
	writeDescriptor.dstSet = descriptorSet;
	writeDescriptor.pBufferInfo = &bufferInfo;

	return writeDescriptor;
}

VkWriteDescriptorSet AppLayer::GenerateImageWriteDescriptor(const std::string& name, Ref<Shader> shader, const VkDescriptorSet descriptorSet, const VkDescriptorImageInfo imageInfo)
{
	VkWriteDescriptorSet writeDescriptor = shader->FindWriteDescriptorSet(name);
	writeDescriptor.dstSet = descriptorSet;
	writeDescriptor.pImageInfo = &imageInfo;

	return writeDescriptor;
}

VkDescriptorPool AppLayer::CreateDescriptorPool()
{
	Ref<VulkanDevice> device = Application::GetApp().GetVulkanDevice();

	VkDescriptorPoolSize poolSizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 }
	};

	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
	descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolCreateInfo.flags = 0;
	descriptorPoolCreateInfo.maxSets = 1000;
	descriptorPoolCreateInfo.poolSizeCount = 1;
	descriptorPoolCreateInfo.pPoolSizes = poolSizes;

	VkDescriptorPool pool;
	VK_CHECK_RESULT(vkCreateDescriptorPool(device->GetLogicalDevice(), &descriptorPoolCreateInfo, nullptr, &pool));

	return pool;
}