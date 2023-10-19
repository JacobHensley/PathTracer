#include "RayTracingLayer.h"
#include "Core/Application.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_vulkan.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

RayTracingLayer::RayTracingLayer(const std::string& name)
	: Layer("RayTracingLayer")
{
	Ref<VulkanDevice> device = Application::GetApp().GetVulkanDevice();

	//m_Mesh = CreateRef<Mesh>("assets/models/CornellBoxReturns.gltf");
	//m_Mesh = CreateRef<Mesh>("assets/models/Suzanne/glTF/Suzanne.gltf");
	m_Mesh = CreateRef<Mesh>("assets/models/Sponza/glTF/Sponza.gltf");

	m_RenderCommandBuffer = CreateRef<RenderCommandBuffer>(1);

	CameraSpecification cameraSpec;
	m_Camera = CreateRef<Camera>(cameraSpec);

	m_CameraUniformBuffer = CreateRef<UniformBuffer>(&m_CameraBuffer, sizeof(CameraBuffer));

	m_DescriptorPool = CreateDescriptorPool();
	m_ViewportImageDescriptorSet = VkTools::AllocateDescriptorSet(m_DescriptorPool, &ImGui_ImplVulkan_GetDescriptorSetLayout());

	// Preetham Sky
	{
		ImageSpecification skyboxSpec;
		skyboxSpec.Width = 2048;
		skyboxSpec.Height = 2048;
		skyboxSpec.Format = ImageFormat::RGBA32F;
		skyboxSpec.Usage = ImageUsage::STORAGE_IMAGE_CUBE;
		m_Skybox = CreateRef<Image>(skyboxSpec);

		m_PreethamSkyComputeShader = CreateRef<Shader>("assets/shaders/PreethamSky.glsl");

		ComputePipelineSpecification spec;
		spec.Shader = m_PreethamSkyComputeShader;
		m_PreethamSkyComputePipeline = CreateRef<ComputePipeline>(spec);

		m_PreethamSkyComputeDescriptorSet = m_PreethamSkyComputeShader->AllocateDescriptorSet(m_DescriptorPool, 0);

		VkWriteDescriptorSet writeDescriptor = m_PreethamSkyComputeShader->FindWriteDescriptorSet("u_CubeMap");
		writeDescriptor.dstSet = m_PreethamSkyComputeDescriptorSet;
		writeDescriptor.pImageInfo = &m_Skybox->GetDescriptorImageInfo();

		vkUpdateDescriptorSets(device->GetLogicalDevice(), 1, &writeDescriptor, 0, NULL);
	}

	{
		AccelerationStructureSpecification spec;
		spec.Mesh = m_Mesh;
		spec.Transform = glm::mat4(1.0f) * glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
		m_AccelerationStructure = CreateRef<AccelerationStructure>(spec);
	}

	{
		ImageSpecification spec;
		spec.DebugName = "RT-FinalImage";
		spec.Format = ImageFormat::RGBA8;
		spec.Usage = ImageUsage::STORAGE_IMAGE_2D;
		spec.Width = 1;
		spec.Height = 1;
		m_Image = CreateRef<Image>(spec);
	}

	{
		ImageSpecification spec;
		spec.DebugName = "RT-AccumulationImage";
		spec.Format = ImageFormat::RGBA32F;
		spec.Usage = ImageUsage::STORAGE_IMAGE_2D;
		spec.Width = 1;
		spec.Height = 1;
		m_AccumulationImage = CreateRef<Image>(spec);
	}

	CreateRayTracingPipeline();

	m_SceneBuffer.FrameIndex = 1;
	m_SceneUniformBuffer = CreateRef<UniformBuffer>(&m_SceneBuffer, sizeof(SceneBuffer));
}

RayTracingLayer::~RayTracingLayer()
{
}

void RayTracingLayer::OnAttach()
{
	
}

void RayTracingLayer::OnDetach()
{
}

void RayTracingLayer::RayTracingPass()
{
	Ref<VulkanDevice> device = Application::GetVulkanDevice();

	VkCommandBuffer commandBuffer = m_RenderCommandBuffer->GetCommandBuffer();

	Ref<StorageBuffer> submeshDataStorageBuffer = m_AccelerationStructure->GetSubmeshDataStorageBuffer();

	if (m_RayTracingDescriptorSet == VK_NULL_HANDLE)
		m_RayTracingDescriptorSet = VkTools::AllocateDescriptorSet(m_DescriptorPool, &m_RayTracingPipeline->GetDescriptorSetLayout());

	VkWriteDescriptorSetAccelerationStructureKHR asDescriptorWrite{};
	asDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	asDescriptorWrite.accelerationStructureCount = 1;
	asDescriptorWrite.pAccelerationStructures = &m_AccelerationStructure->GetAccelerationStructure();

	VkWriteDescriptorSet accelerationStructureWrite{};
	accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	accelerationStructureWrite.pNext = &asDescriptorWrite;
	accelerationStructureWrite.dstSet = m_RayTracingDescriptorSet;
	accelerationStructureWrite.dstBinding = 0;
	accelerationStructureWrite.descriptorCount = 1;
	accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

	const auto& asSpec = m_AccelerationStructure->GetSpecification();

	std::vector<VkDescriptorBufferInfo> vertexBufferInfos;
	{
		VkBuffer vb = asSpec.Mesh->GetVertexBuffer()->GetBuffer();
		vertexBufferInfos.push_back({ vb, 0, VK_WHOLE_SIZE });
	}

	std::vector<VkDescriptorBufferInfo> indexBufferInfos;
	{
		VkBuffer ib = asSpec.Mesh->GetIndexBuffer()->GetBuffer();
		indexBufferInfos.push_back({ ib, 0, VK_WHOLE_SIZE });
	}

	std::vector<VkDescriptorImageInfo> textureImageInfos;
	{
		const auto& textures = m_AccelerationStructure->GetTextures();
		for (auto texture : textures)
		{
			textureImageInfos.push_back(texture->GetDescriptorImageInfo());
		}
	}

	std::vector<VkWriteDescriptorSet> rayTracingWriteDescriptors = {
		accelerationStructureWrite,
		VkTools::WriteDescriptorSet(m_RayTracingDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,  &m_Image->GetDescriptorImageInfo()),
		VkTools::WriteDescriptorSet(m_RayTracingDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2,  &m_AccumulationImage->GetDescriptorImageInfo()),
		VkTools::WriteDescriptorSet(m_RayTracingDescriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3, &m_CameraUniformBuffer->GetDescriptorBufferInfo()),
		VkTools::WriteDescriptorSet(m_RayTracingDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4, vertexBufferInfos.data(), (uint32_t)vertexBufferInfos.size()),
		VkTools::WriteDescriptorSet(m_RayTracingDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5, indexBufferInfos.data(), (uint32_t)indexBufferInfos.size()),
		VkTools::WriteDescriptorSet(m_RayTracingDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6, &submeshDataStorageBuffer->GetDescriptorBufferInfo()),
		VkTools::WriteDescriptorSet(m_RayTracingDescriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 7, &m_SceneUniformBuffer->GetDescriptorBufferInfo()),
		VkTools::WriteDescriptorSet(m_RayTracingDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8, &m_AccelerationStructure->GetMaterialBuffer()->GetDescriptorBufferInfo()),
		VkTools::WriteDescriptorSet(m_RayTracingDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10, &m_Skybox->GetDescriptorImageInfo()),
	};

	if (textureImageInfos.size() > 0)
		rayTracingWriteDescriptors.push_back(VkTools::WriteDescriptorSet(m_RayTracingDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 9, textureImageInfos.data(), (uint32_t)textureImageInfos.size()));

	vkUpdateDescriptorSets(device->GetLogicalDevice(), rayTracingWriteDescriptors.size(), rayTracingWriteDescriptors.data(), 0, NULL);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_RayTracingPipeline->GetPipeline());
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_RayTracingPipeline->GetPipelineLayout(), 0, 1, &m_RayTracingDescriptorSet, 0, 0);

	const auto& shaderBindingTable = m_RayTracingPipeline->GetShaderBindingTable();

	VkStridedDeviceAddressRegionKHR empty{};

	vkCmdTraceRaysKHR(commandBuffer,
		&shaderBindingTable[0].StridedDeviceAddressRegion,
		&shaderBindingTable[1].StridedDeviceAddressRegion,
		&shaderBindingTable[2].StridedDeviceAddressRegion,
		&empty,
		m_ViewportWidth,
		m_ViewportHeight,
		1);

	m_SceneBuffer.FrameIndex++;
}

bool RayTracingLayer::CreateRayTracingPipeline()
{
	RayTracingPipelineSpecification spec;

	spec.RayGenShader = CreateRef<Shader>("assets/shaders/RayTracing/RayGen.glsl");
	if (!spec.RayGenShader->CompiledSuccessfully())
		return false;

	spec.MissShader = CreateRef<Shader>("assets/shaders/RayTracing/Miss.glsl");
	if (!spec.MissShader->CompiledSuccessfully())
		return false;

	spec.ClosestHitShader = CreateRef<Shader>("assets/shaders/RayTracing/ClosestHit.glsl");
	if (!spec.ClosestHitShader->CompiledSuccessfully())
		return false;

	m_SceneBuffer.FrameIndex = 1;

	m_RayTracingPipeline = CreateRef<RayTracingPipeline>(spec);
	return true;
}

void RayTracingLayer::OnUpdate()
{
	Ref<VulkanDevice> device = Application::GetApp().GetVulkanDevice();

	bool moved = m_Camera->Update();

	if (!m_Accumulate || moved || m_UpdateSkyBox)
		m_SceneBuffer.FrameIndex = 1;

	m_SceneUniformBuffer->SetData(&m_SceneBuffer);

	// Dispatch PreethamSky compute shader 
	if (m_UpdateSkyBox) 
	{
		VkCommandBuffer commandBuffer = device->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_PreethamSkyComputePipeline->GetPipeline());
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_PreethamSkyComputePipeline->GetPipelineLayout(), 0, 1, &m_PreethamSkyComputeDescriptorSet, 0, nullptr);
		vkCmdPushConstants(commandBuffer, m_PreethamSkyComputePipeline->GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::vec3), &m_SkyboxSettings);

		vkCmdDispatch(commandBuffer, 64, 64, 6);

		device->FlushCommandBuffer(commandBuffer, true);

		m_UpdateSkyBox = false;
	}
}

void RayTracingLayer::OnRender()
{
	/////////////////////////////////////////////
	// 1. Update data
	/////////////////////////////////////////////

	// Handle resize
	if (m_NeedsResize)
	{
		m_NeedsResize = false;
		
		m_Image->Resize(m_ViewportWidth, m_ViewportHeight);
		m_AccumulationImage->Resize(m_ViewportWidth, m_ViewportHeight);

		m_SceneBuffer.FrameIndex = 1;
	}

	m_SceneUniformBuffer->SetData(&m_SceneBuffer);

	// Update camera uniform buffer
	{
		m_Camera->Resize(m_ViewportWidth, m_ViewportHeight);

		m_CameraBuffer.ViewProjection = m_Camera->GetViewProjection();
		m_CameraBuffer.InverseViewProjection = m_Camera->GetInverseViewProjection();
		m_CameraBuffer.View = m_Camera->GetView();
		m_CameraBuffer.InverseView = m_Camera->GetInverseView();
		m_CameraBuffer.InverseProjection = m_Camera->GetInverseProjection();

		m_CameraUniformBuffer->SetData(&m_CameraBuffer);
	}

	/////////////////////////////////////////////
	// 2. Record command buffers
	/////////////////////////////////////////////

	m_RenderCommandBuffer->Begin();

	RayTracingPass();

	m_RenderCommandBuffer->End();
	m_RenderCommandBuffer->Submit();
}

void RayTracingLayer::OnImGUIRender()
{
	ImGuiIO io = ImGui::GetIO();
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
	bool open = true;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("RTX On", &open, flags);

	ImVec2 size = ImGui::GetContentRegionAvail();

	if (m_ViewportWidth != size.x || m_ViewportHeight != size.y)
	{
		m_NeedsResize = true;
		m_ViewportWidth = size.x;
		m_ViewportHeight = size.y;
	}

	// Update viewport descriptor on reisze
	{
		Ref<VulkanDevice> device = Application::GetApp().GetVulkanDevice();

		const VkDescriptorImageInfo& descriptorInfo = m_Image->GetDescriptorImageInfo();
		if (m_ViewportImageView != descriptorInfo.imageView)
		{
			VkWriteDescriptorSet writeDescriptor = VkTools::WriteDescriptorSet(m_ViewportImageDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &descriptorInfo);
			vkUpdateDescriptorSets(device->GetLogicalDevice(), 1, &writeDescriptor, 0, nullptr);
			m_ViewportImageView = descriptorInfo.imageView;
		}
	}

	ImGui::Image(m_ViewportImageDescriptorSet, ImVec2(size.x, size.y), ImVec2::ImVec2(0, 1), ImVec2::ImVec2(1, 0));

	ImGui::End();
	ImGui::PopStyleVar();

	ImGui::Begin("Settings");

	if (ImGui::Button("Reload Pipeline"))
	{
		if (!CreateRayTracingPipeline())
			LOG_CRITICAL("Failed to create Ray Tracing pipeline!");
	}

	ImGui::Checkbox("Accumulate", &m_Accumulate);

	ImGui::Separator();

	if (ImGui::DragFloat("Turbidity",   &m_SkyboxSettings.x, 0.01f, 1.3, 10.0f))
		m_UpdateSkyBox = true;
	if (ImGui::DragFloat("Azimuth",     &m_SkyboxSettings.y, 0.01f, 0, 2 * 3.14))
		m_UpdateSkyBox = true;
	if (ImGui::DragFloat("Inclination", &m_SkyboxSettings.z, 0.01f, 0, 2 * 3.14))
		m_UpdateSkyBox = true;

	ImGui::End();
}

VkDescriptorPool RayTracingLayer::CreateDescriptorPool()
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