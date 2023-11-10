#include "RayTracingLayer.h"
#include "Core/Application.h"
#include "Input/Input.h"
#include "Input/KeyCodes.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_vulkan.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

RayTracingLayer::RayTracingLayer(const std::string& name)
	: Layer("RayTracingLayer")
{
	Ref<VulkanDevice> device = Application::GetApp().GetVulkanDevice();

	//m_Mesh = CreateRef<Mesh>("assets/models/Suzanne/glTF/Suzanne.gltf");
	m_Mesh = CreateRef<Mesh>("assets/models/Sponza/glTF/Sponza.gltf");
	//m_Mesh = CreateRef<Mesh>("assets/models/CornellBox.gltf");

	m_Transform = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
	//m_Transform = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));

	m_RenderCommandBuffer = CreateRef<RenderCommandBuffer>(1);

	CameraSpecification cameraSpec;
	cameraSpec.pitch = 0.208f;
	cameraSpec.yaw = 1.731f;
	m_Camera = CreateRef<Camera>(cameraSpec);
	m_Camera->SetPosition({ -12.5f, 6.7f, -1.85f });

	m_CameraUniformBuffer = CreateRef<UniformBuffer>(&m_CameraBuffer, sizeof(CameraBuffer));

	m_DescriptorPool = VkTools::CreateDescriptorPool();

	m_ViewportPanel = CreateRef<ViewportPanel>();

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
		ImageSpecification imageSpec;
		imageSpec.DebugName = "PostProcessing";
		imageSpec.Format = ImageFormat::RGBA8;
		imageSpec.Usage = ImageUsage::STORAGE_IMAGE_2D;
		imageSpec.Width = 1;
		imageSpec.Height = 1;
		m_PostProcessingImage = CreateRef<Image>(imageSpec);

		ComputePipelineSpecification pipelineSpec;
		pipelineSpec.Shader = CreateRef<Shader>("assets/shaders/PostProcessing.glsl");;
		m_PostProcessingComputePipeline = CreateRef<ComputePipeline>(pipelineSpec);

		m_PostProcessingComputeDescriptorSet = pipelineSpec.Shader->AllocateDescriptorSet(m_DescriptorPool, 0);
	}

	{
		AccelerationStructureSpecification spec;
		spec.Mesh = m_Mesh;
		spec.Transform = m_Transform;
		m_AccelerationStructure = CreateRef<AccelerationStructure>(spec);
	}

	{
		ImageSpecification spec;
		spec.DebugName = "RT-FinalImage";
		spec.Format = ImageFormat::RGBA32F;
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
		m_ViewportPanel->GetSize().x,
		m_ViewportPanel->GetSize().y,
		1);

	m_SceneBuffer.FrameIndex++;
}

void RayTracingLayer::PostProcessingPass()
{
	Ref<VulkanDevice> device = Application::GetApp().GetVulkanDevice();

	{
		std::array<VkWriteDescriptorSet, 2> writeDescriptors;
		writeDescriptors[0] = m_PostProcessingComputePipeline->GetShader()->FindWriteDescriptorSet("u_OutputImage");
		writeDescriptors[0].dstSet = m_PostProcessingComputeDescriptorSet;
		writeDescriptors[0].pImageInfo = &m_PostProcessingImage->GetDescriptorImageInfo();

		writeDescriptors[1] = m_PostProcessingComputePipeline->GetShader()->FindWriteDescriptorSet("u_InputImage");
		writeDescriptors[1].dstSet = m_PostProcessingComputeDescriptorSet;
		writeDescriptors[1].pImageInfo = &m_Image->GetDescriptorImageInfo();

		vkUpdateDescriptorSets(device->GetLogicalDevice(), writeDescriptors.size(), writeDescriptors.data(), 0, NULL);
	}

	VkCommandBuffer commandBuffer = device->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_PostProcessingComputePipeline->GetPipeline());
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_PostProcessingComputePipeline->GetPipelineLayout(), 0, 1, &m_PostProcessingComputeDescriptorSet, 0, nullptr);
	vkCmdPushConstants(commandBuffer, m_PostProcessingComputePipeline->GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &m_Exposure);

	glm::ivec3 workGroups = {
		(int)glm::ceil((float)m_PostProcessingImage->GetWidth() / 32.0f),
		(int)glm::ceil((float)m_PostProcessingImage->GetHeight() / 32.0f),
		1
	};

	vkCmdDispatch(commandBuffer, workGroups.x, workGroups.y, workGroups.z);

	device->FlushCommandBuffer(commandBuffer, true);
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

	if (Input::IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && m_ViewportPanel->IsHovered())
	{
		m_SelectedSubMeshIndex = m_Mesh->RayIntersection(m_ViewportPanel->CastMouseRay(m_Camera), m_Transform);
	}

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
	if (m_ViewportPanel->HasResized())
	{
		m_Image->Resize(m_ViewportPanel->GetSize().x, m_ViewportPanel->GetSize().y);
		m_AccumulationImage->Resize(m_ViewportPanel->GetSize().x, m_ViewportPanel->GetSize().y);
		m_PostProcessingImage->Resize(m_ViewportPanel->GetSize().x, m_ViewportPanel->GetSize().y);

		m_SceneBuffer.FrameIndex = 1;
	}

	m_SceneUniformBuffer->SetData(&m_SceneBuffer);

	// Update camera uniform buffer
	{
		m_Camera->Resize(m_ViewportPanel->GetSize().x, m_ViewportPanel->GetSize().y);

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
	PostProcessingPass();

	m_RenderCommandBuffer->End();
	m_RenderCommandBuffer->Submit();
}

void RayTracingLayer::OnImGUIRender()
{
	if (m_DoPostProcessing)
		m_ViewportPanel->Render(m_PostProcessingImage);
	else
		m_ViewportPanel->Render(m_Image);

	ImGui::Begin("Settings");

	if (ImGui::Button("Reload Pipeline"))
	{
		if (!CreateRayTracingPipeline())
			LOG_CRITICAL("Failed to create Ray Tracing pipeline!");
	}

	ImGui::SliderFloat("Exposure", &m_Exposure, 0.0f, 10.0f);

	ImGui::Checkbox("Post-Processing", &m_DoPostProcessing);
	ImGui::Checkbox("Accumulate", &m_Accumulate);

	if (m_SelectedSubMeshIndex > -1)
	{
		ImGui::Separator();

		uint32_t materialIndex = m_Mesh->GetSubMeshes()[m_SelectedSubMeshIndex].MaterialIndex;
		MaterialBuffer& materialBuffer = m_Mesh->GetMaterialBuffers()[materialIndex];
		bool updated = false;

		if (ImGui::ColorEdit3("Albdeo", glm::value_ptr(materialBuffer.AlbedoValue)))
			updated = true;
		if (ImGui::DragFloat("Metallic", &materialBuffer.MetallicValue, 0.01f, 0.0f, 1.0f))
			updated = true;
		if (ImGui::DragFloat("Roughness", &materialBuffer.RoughnessValue, 0.01f, 0.0f, 1.0f))
			updated = true;
		if (ImGui::ColorEdit3("Emissive Color", glm::value_ptr(materialBuffer.EmissiveValue)))
			updated = true;
		if (ImGui::DragFloat("Emissive Strength", &materialBuffer.EmissiveStrength, 0.1f, 0.0f, 10.0f))
			updated = true;

		if (updated)
		{
			m_SceneBuffer.FrameIndex = 1;
			m_AccelerationStructure->UpdateMaterialData();
		}
	}

	ImGui::Separator();
	ImGui::Text("Camera");
	ImGui::Text("Position: %.3f, %.3f, %.3f", m_Camera->GetPosition().x, m_Camera->GetPosition().y, m_Camera->GetPosition().z);
	ImGui::Text("Pitch/Yaw: %.3f, %.3f", m_Camera->GetPitchYaw().x, m_Camera->GetPitchYaw().y);
	ImGui::Text("Focal Point: %.3f, %.3f, %.3f", m_Camera->GetFocalPoint().x, m_Camera->GetFocalPoint().y, m_Camera->GetFocalPoint().z);
	ImGui::Text("Distance: %.3f", m_Camera->GetDistance());


	ImGui::End();
}