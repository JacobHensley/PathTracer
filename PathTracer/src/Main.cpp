#include "Core/Application.h"
#include "AppLayer.h"

using namespace VkLibrary;

int main()
{
	Application app = Application("VulkanLibrary Template");

	Ref<AppLayer> layer = CreateRef<AppLayer>("AppLayer");
	app.AddLayer(layer);

	app.Run();

	return 0;
}