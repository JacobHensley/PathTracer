#include "RayTracingLayer.h"
#include "Core/Application.h"
#include "Input/Input.h"
#include "Input/KeyCodes.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_vulkan.h"
#include <FastNoise/FastNoise.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>

RayTracingLayer::RayTracingLayer(const std::string& name)
	: Layer("RayTracingLayer")
{
	Ref<VulkanDevice> device = Application::GetApp().GetVulkanDevice();

	//m_Mesh = CreateRef<Mesh>(MeshSource("assets/models/Suzanne/glTF/Suzanne.gltf"));
	//m_Mesh = CreateRef<Mesh>(CreateRef<MeshSource>("assets/models/Sponza/glTF/Sponza.gltf"));
	m_Mesh = CreateRef<Mesh>(CreateRef<MeshSource>("assets/models/IntelSponza/NewSponza_Main_glTF_002.gltf"));
	//m_Mesh = CreateRef<Mesh>(CreateRef<MeshSource>("assets/models/Rotation.gltf"));
	//m_Mesh = CreateRef<Mesh>(CreateRef<MeshSource>("assets/models/Cube.gltf"));
	//m_Mesh = CreateRef<Mesh>(CreateRef<MeshSource>("assets/models/CornellBox.gltf"));
	//m_Transform = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));
	m_Transform = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));

	m_RenderCommandBuffer = CreateRef<RenderCommandBuffer>(1);

	CameraSpecification cameraSpec;
//	cameraSpec.pitch = 0.208f;
//	cameraSpec.yaw = 1.731f;
	m_Camera = CreateRef<Camera>(cameraSpec);
//	m_Camera->SetPosition({ -12.5f, 6.7f, -1.85f });

	m_CameraUniformBuffer = CreateRef<UniformBuffer>(&m_CameraBuffer, sizeof(CameraBuffer));

	m_DescriptorPool = VkTools::CreateDescriptorPool();

	m_ViewportPanel = CreateRef<ViewportPanel>();

	{
		TextureCubeSpecification spec;
		spec.path = "assets/hdr/graveyard_pathways_4k.hdr";
		m_RadianceMap = CreateRef<TextureCube>(spec);
	}
	
	// Preetham Sky
	{
		ImageSpecification skyboxSpec;
		skyboxSpec.Width = 2048;
		skyboxSpec.Height = 2048;
		skyboxSpec.Format = ImageFormat::RGBA32F;
		skyboxSpec.Usage = ImageUsage::STORAGE_IMAGE_CUBE;
		m_PreethamSkybox = CreateRef<Image>(skyboxSpec);

		m_PreethamSkyComputeShader = CreateRef<Shader>("assets/shaders/PreethamSky.glsl");

		ComputePipelineSpecification spec;
		spec.Shader = m_PreethamSkyComputeShader;
		m_PreethamSkyComputePipeline = CreateRef<ComputePipeline>(spec);

		m_PreethamSkyComputeDescriptorSet = m_PreethamSkyComputeShader->AllocateDescriptorSet(m_DescriptorPool, 0);

		VkWriteDescriptorSet writeDescriptor = m_PreethamSkyComputeShader->FindWriteDescriptorSet("u_CubeMap");
		writeDescriptor.dstSet = m_PreethamSkyComputeDescriptorSet;
		writeDescriptor.pImageInfo = &m_PreethamSkybox->GetDescriptorImageInfo();

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

	CreateAccelerationStructure();

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
	m_SceneBuffer.AbsorptionFactor = glm::vec3(1.0);
	m_SceneUniformBuffer = CreateRef<UniformBuffer>(&m_SceneBuffer, sizeof(SceneBuffer));

	{
		uint32_t width = 512;
		uint32_t height = 512;
		uint32_t depth = 512;
		uint64_t noiseSize = width * height * depth * 4;
		uint8_t* data = new uint8_t[noiseSize];

		const char* filepath = "Cloud.noise";
		if (std::filesystem::exists(filepath))
		{
			std::ifstream stream(filepath, std::ios::binary);
			stream.read((char*)data, noiseSize);
			stream.close();
		}
		else
		{
			//FastNoise::SmartNode<> gen = FastNoise::New<FastNoise::Checkerboard>();
			FastNoise::SmartNode<> gen = FastNoise::NewFromEncodedNodeTree("FwDsUTg+rkdhPwAAAAAAAIA/GQAbABkAGQAbABcAAAAAAAAAgD8AAIA/KVyPvxMACtcjPQsAAQAAAAAAAAABAAAAAAAAAAAAAIA/AAAAAD4BGwAXAAAAAAAAAIA/AACAPylcj78TAI/CdbwLAAEAAAAAAAAAAQAAAAAAAAAAAACAPwAAAIA+ARsAFwAAAAAAAACAPwAAgD97FK6+FQBxPapAj8K1QDMzc0ATAI/CdTwLAAEAAAAAAAAAAQAAAAAAAAAAAACAPwAAACA/AJqZGT8BGwAZAA0ABAAAAAAAAEATAArXozwHAAAAAAA/AI/C9T0AzczMPgDNzMw+");

			std::vector<float> noiseOutput(width * height * depth);
			FastNoise::OutputMinMax o = gen->GenUniformGrid3D(noiseOutput.data(), 0, 0, 0, width, height, depth, 1.0f, 1337);
			//FastNoise::OutputMinMax bounds = gen->GenUniformGrid2D(noiseOutput.data(), 0, 0, width, height, 0.02f, 1337);

			int index = 0;

			float input_start = o.min;
			float input_end = o.max;
			float output_start = 0.0;
			float output_end = 1.0;

			for (int i = 0; i < width * height * depth; i++)
			{
				float input = noiseOutput[i];
				float output = output_start + ((output_end - output_start) / (input_end - input_start)) * (input - input_start);
				data[i * 4 + 0] = output * 255;
				data[i * 4 + 1] = output * 255;
				data[i * 4 + 2] = output * 255;
				data[i * 4 + 3] = 255;
			}


			std::ofstream stream(filepath, std::ios::binary);
			stream.write((const char*)data, noiseSize);
			stream.close();
		}

		ImageSpecification spec;
		spec.Data = data;
		spec.DebugName = "NoiseTexture";
		spec.Format = ImageFormat::RGBA8;
		spec.Usage = ImageUsage::TEXTURE_2D;
		spec.Width = width;
		spec.Height = height;
		spec.Depth = depth;
		m_NoiseTexture = CreateRef<Image>(spec);

		m_SceneBuffer.AbsorptionFactor.x = 0.8;
		m_SceneBuffer.AbsorptionFactor.y = 0.025;
	}
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
		VkTools::WriteDescriptorSet(m_RayTracingDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10, &m_RadianceMap->GetDescriptorImageInfo()),
		VkTools::WriteDescriptorSet(m_RayTracingDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 11, &m_NoiseTexture->GetDescriptorImageInfo())
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

void RayTracingLayer::CreateAccelerationStructure()
{
	AccelerationStructureSpecification spec;
	spec.Mesh = m_Mesh;
	spec.Transform = m_Transform;
	m_AccelerationStructure = CreateRef<AccelerationStructure>(spec);
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

glm::vec3 Scale(const glm::vec3& v, float desiredLength)
{
	return v * desiredLength / length(v);
}

bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale)
{
	using namespace glm;
	using T = float;

	mat4 LocalMatrix(transform);

	if (epsilonEqual(LocalMatrix[3][3], static_cast<T>(0), epsilon<T>()))
		return false;

	// Assume matrix is already normalized
	//ASSERT(epsilonEqual(LocalMatrix[3][3], static_cast<T>(1), static_cast<T>(0.00001)));

	// Ignore perspective
//ASSERT(
//	epsilonEqual(LocalMatrix[0][3], static_cast<T>(0), epsilon<T>()) &&
//	epsilonEqual(LocalMatrix[1][3], static_cast<T>(0), epsilon<T>()) &&
//	epsilonEqual(LocalMatrix[2][3], static_cast<T>(0), epsilon<T>())
//);
//

	// Next take care of translation (easy).
	translation = vec3(LocalMatrix[3]);
	LocalMatrix[3] = vec4(0, 0, 0, LocalMatrix[3].w);

	vec3 Row[3];

	// Now get scale and shear.
	for (length_t i = 0; i < 3; ++i)
		for (length_t j = 0; j < 3; ++j)
			Row[i][j] = LocalMatrix[i][j];

	// Compute X scale factor and normalize first row.
	scale.x = length(Row[0]);
	Row[0] = Scale(Row[0], static_cast<T>(1));
	scale.y = length(Row[1]);
	Row[1] = Scale(Row[1], static_cast<T>(1));

	scale.z = length(Row[2]);
	Row[2] = Scale(Row[2], static_cast<T>(1));

	// Rotation as quaternion
	int i, j, k = 0;
	T root, trace = Row[0].x + Row[1].y + Row[2].z;
	if (trace > static_cast<T>(0))
	{
		root = sqrt(trace + static_cast<T>(1));
		rotation.w = static_cast<T>(0.5) * root;
		root = static_cast<T>(0.5) / root;
		rotation.x = root * (Row[1].z - Row[2].y);
		rotation.y = root * (Row[2].x - Row[0].z);
		rotation.z = root * (Row[0].y - Row[1].x);
	} // End if > 0
	else
	{
		static int Next[3] = { 1, 2, 0 };
		i = 0;
		if (Row[1].y > Row[0].x) i = 1;
		if (Row[2].z > Row[i][i]) i = 2;
		j = Next[i];
		k = Next[j];

		root = sqrt(Row[i][i] - Row[j][j] - Row[k][k] + static_cast<T>(1.0));

		rotation[i] = static_cast<T>(0.5) * root;
		root = static_cast<T>(0.5) / root;
		rotation[j] = root * (Row[i][j] + Row[j][i]);
		rotation[k] = root * (Row[i][k] + Row[k][i]);
		rotation.w = root * (Row[j][k] - Row[k][j]);
	} // End if <= 0

	return true;
}

static float factor = 1.0f;
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

		ImGui::Text("%s", m_Mesh->GetSubMeshes()[m_SelectedSubMeshIndex].Name.c_str());
		uint32_t materialIndex = m_Mesh->GetSubMeshes()[m_SelectedSubMeshIndex].MaterialIndex;
		MaterialBuffer& materialBuffer = m_Mesh->GetMaterialBuffers()[materialIndex];
		bool updated = false;

		if (ImGui::ColorEdit3("Albdeo", glm::value_ptr(materialBuffer.data.AlbedoValue)))
			updated = true;
		if (ImGui::DragFloat("Metallic", &materialBuffer.data.MetallicValue, 0.01f, 0.0f, 1.0f))
			updated = true;
		if (ImGui::DragFloat("Roughness", &materialBuffer.data.RoughnessValue, 0.01f, 0.0f, 1.0f))
			updated = true;
		if (ImGui::ColorEdit3("Emissive Color", glm::value_ptr(materialBuffer.data.EmissiveValue)))
			updated = true;
		if (ImGui::DragFloat("Emissive Strength", &materialBuffer.data.EmissiveStrength, 0.1f, 0.0f, 10.0f))
			updated = true;

		if (ImGui::DragFloat("Anisotropic", &materialBuffer.Anisotropic, 0.01f, 0.0f, 1.0f))
			updated = true;
		if (ImGui::DragFloat("Subsurface", &materialBuffer.Subsurface, 0.01f, 0.0f, 1.0f))
			updated = true;
		if (ImGui::DragFloat("SpecularTint", &materialBuffer.SpecularTint, 0.01f, 0.0f, 1.0f))
			updated = true;
		if (ImGui::DragFloat("Sheen", &materialBuffer.Sheen, 0.01f, 0.0f, 1.0f))
			updated = true;
		if (ImGui::DragFloat("SheenTint", &materialBuffer.SheenTint, 0.01f, 0.0f, 1.0f))
			updated = true;
		if (ImGui::DragFloat("Clearcoat", &materialBuffer.Clearcoat, 0.01f, 0.0f, 1.0f))
			updated = true;
		if (ImGui::DragFloat("ClearcoatRoughness", &materialBuffer.ClearcoatRoughness, 0.01f, 0.0f, 1.0f))
			updated = true;
		if (ImGui::DragFloat("SpecTrans", &materialBuffer.SpecTrans, 0.01f, 0.0f, 1.0f))
			updated = true;
		if (ImGui::DragFloat("ior", &materialBuffer.ior, 0.01f, 0.0f, 2.0f))
			updated = true;

		ImGui::Separator();

		static bool recreateAS = false;
		ImGui::Checkbox("Automatically update AS", &recreateAS);

		if (ImGui::DragFloat3("Translation", &m_Mesh->GetSubMeshes()[m_SelectedSubMeshIndex].WorldTransform[3][0]))
		{
			if (recreateAS)
				CreateAccelerationStructure();
		}
	
		auto& submeshWorldTransform = m_Mesh->GetSubMeshes()[m_SelectedSubMeshIndex].WorldTransform;
		glm::vec3 translation, scale;
		glm::quat rotation;
		DecomposeTransform(submeshWorldTransform, translation, rotation, scale);

		bool reconstructMatrix = false;

		glm::vec3 rotEuler = glm::degrees(glm::eulerAngles(rotation));
		if (ImGui::DragFloat4("Rotation (quat)", glm::value_ptr(rotation)))
			reconstructMatrix = true;

		if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotEuler)))
		{
			rotation = glm::quat(glm::radians(rotEuler));
			reconstructMatrix = true;
		}

		if (ImGui::DragFloat3("Scale", glm::value_ptr(scale)))
			reconstructMatrix = true;

		if (reconstructMatrix)
		{
			submeshWorldTransform = glm::translate(glm::mat4(1.0f), translation)
				* glm::toMat4(rotation) * glm::scale(glm::mat4(1.0f), scale);

			if (recreateAS)
				CreateAccelerationStructure();
		}

		ImGui::Separator();

		const glm::mat4& matrix = m_Mesh->GetSubMeshes()[m_SelectedSubMeshIndex].WorldTransform;
		glm::vec4 a = matrix[0];
		glm::vec4 b = matrix[1];
		glm::vec4 c = matrix[2];
		glm::vec4 d = matrix[3];
		ImGui::Text("%.2f %.2f %.2f %.2f", a.x, a.y, a.z, a.w);
		ImGui::Text("%.2f %.2f %.2f %.2f", b.x, b.y, b.z, b.w);
		ImGui::Text("%.2f %.2f %.2f %.2f", c.x, c.y, c.z, c.w);
		ImGui::Text("%.2f %.2f %.2f %.2f", d.x, d.y, d.z, d.w);

		if (updated)
		{
			m_SceneBuffer.FrameIndex = 1;
			m_AccelerationStructure->UpdateMaterialData();
		}
	}
	
	ImGui::Separator();
	ImGui::DragFloat("x", &m_SceneBuffer.AbsorptionFactor.x, 0.1f);
	ImGui::DragFloat("y", &m_SceneBuffer.AbsorptionFactor.y, 0.001f);
	ImGui::DragFloat("z", &m_SceneBuffer.AbsorptionFactor.z, 0.1f);

	ImGui::Separator();
	ImGui::Text("Camera");
	ImGui::Text("Position: %.3f, %.3f, %.3f", m_Camera->GetPosition().x, m_Camera->GetPosition().y, m_Camera->GetPosition().z);
	ImGui::Text("Pitch/Yaw: %.3f, %.3f", m_Camera->GetPitchYaw().x, m_Camera->GetPitchYaw().y);
	ImGui::Text("Focal Point: %.3f, %.3f, %.3f", m_Camera->GetFocalPoint().x, m_Camera->GetFocalPoint().y, m_Camera->GetFocalPoint().z);
	ImGui::Text("Distance: %.3f", m_Camera->GetDistance());


	ImGui::End();
}