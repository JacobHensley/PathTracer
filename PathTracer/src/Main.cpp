#include "Core/Application.h"
#include "RayTracingLayer.h"

using namespace VkLibrary;

int main()
{
	Application app = Application("VulkanLibrary Template");

	Ref<RayTracingLayer> layer = CreateRef<RayTracingLayer>("RayTracingLayer");
	app.AddLayer(layer);

	app.Run();

	return 0;
}