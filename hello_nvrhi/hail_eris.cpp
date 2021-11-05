
#include <SDL.h>
#include <SDL_vulkan.h>
#include <nvrhi/vulkan.h>

#include <iostream>
#include <string>
#include <vector>

#include "vk_errors.hpp"


SDL_Window* Window = nullptr;
VkInstance Instance = VK_NULL_HANDLE;
VkSurfaceKHR Surface = VK_NULL_HANDLE;
VkSurfaceFormatKHR SurfaceFormat;
VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
VkDevice LogicalDevice = VK_NULL_HANDLE;
VkQueue GraphicsQueue = VK_NULL_HANDLE;
int32_t GraphicsQueueFamilyIndex = -1;

nvrhi::DeviceHandle Device = nullptr;

std::vector<const char*> InstanceExtensions;
std::vector<const char*> DeviceExtensions;


void DrawLoop()
{
	bool Live = true;
	while (Live)
	{
		SDL_Event Event;
		while (SDL_PollEvent(&Event))
		{
			if (Event.type == SDL_QUIT ||
				(Event.type == SDL_WINDOWEVENT && Event.window.event == SDL_WINDOWEVENT_CLOSE && Event.window.windowID == SDL_GetWindowID(Window)))
			{
				Live = false;
				break;
			}
		}
	}
}


struct MessageCallback : public nvrhi::IMessageCallback
{
	MessageCallback(uint8_t InMinLogLevel = 0)
		: MinLogLevel(InMinLogLevel)
	{}

	static nvrhi::IMessageCallback* GetDefault()
	{
		static MessageCallback DefaultCallback;
		return &DefaultCallback;
	}

	virtual void message(nvrhi::MessageSeverity Severity, const char* MessageText)
	{
		uint8_t LogLevel = uint8_t(Severity);
		const char* SeverityNames[4] = \
		{
			"Info",
			"Warning",
			"Error",
			"Fatal"
		};

		if (LogLevel >= MinLogLevel && MinLogLevel < 4)
		{
			const char* Severity = SeverityNames[LogLevel];
			std::cout << "NVRHI " << Severity << ": " << MessageText << "\n";
		}
	}

private:
	uint8_t MinLogLevel;
};


bool WindowSetup(bool Debug)
{
	std::string ApplicationName = "NVRHI Hello World";

	// Create the SDL window.
	{
		SDL_SetMainReady();
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0)
		{
			Window = SDL_CreateWindow(
				ApplicationName.c_str(),
				SDL_WINDOWPOS_CENTERED,
				SDL_WINDOWPOS_CENTERED,
				512, 512,
				SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
		}
		if (Window == nullptr)
		{
			std::cout << "Failed to create SDL2 window.\n";
			return false;
		}
	}

	// Create Vulkan Instance.
	{
		VkApplicationInfo AppInfo = {};
		AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		AppInfo.pNext = nullptr;
		AppInfo.pApplicationName = ApplicationName.c_str();
		AppInfo.applicationVersion = 1;
		AppInfo.pEngineName = ApplicationName.c_str();
		AppInfo.engineVersion = 0;
		AppInfo.apiVersion = VK_API_VERSION_1_1;

		{
			uint32_t Count;
			SDL_Vulkan_GetInstanceExtensions(Window, &Count, nullptr);
			InstanceExtensions.resize(Count);
			SDL_Vulkan_GetInstanceExtensions(Window, &Count, InstanceExtensions.data());
		}
		InstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

		const char* ValidationLayers[] = {
			"VK_LAYER_KHRONOS_validation",
		};

		VkInstanceCreateInfo CreateInfo;
		CreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		CreateInfo.pNext = nullptr;
		CreateInfo.flags = 0;
		CreateInfo.pApplicationInfo = &AppInfo;
		CreateInfo.enabledExtensionCount = (uint32_t)InstanceExtensions.size();
		CreateInfo.ppEnabledExtensionNames = InstanceExtensions.data();
		if (Debug)
		{
			CreateInfo.enabledLayerCount = 1;
			CreateInfo.ppEnabledLayerNames = ValidationLayers;
		}
		else
		{
			CreateInfo.enabledLayerCount = 0;
			CreateInfo.ppEnabledLayerNames = nullptr;
		}
		if (VK_CALL(vkCreateInstance, &CreateInfo, nullptr, &Instance))
		{
			return false;
		}
	}

	// Select a GPU.
	{
		std::vector<VkPhysicalDevice> PhysicalDevices;
		{
			uint32_t PhysicalDeviceCount = 0;
			vkEnumeratePhysicalDevices(Instance, &PhysicalDeviceCount, nullptr);
			PhysicalDevices.resize(PhysicalDeviceCount);
			vkEnumeratePhysicalDevices(Instance, &PhysicalDeviceCount, PhysicalDevices.data());
		}
		if (PhysicalDevices.size() == 0)
		{
			std::cout << "No GPUs found.\n";
			return false;
		}
		else
		{
			VkPhysicalDeviceProperties PhysicalDeviceProperties;
			// Use the first available discrete GPU.
			for (VkPhysicalDevice& Candidate : PhysicalDevices)
			{
				vkGetPhysicalDeviceProperties(Candidate, &PhysicalDeviceProperties);
				if (PhysicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				{
					PhysicalDevice = Candidate;
					goto FoundPhysicalDevice;
				}
			}
			// Or the first available integrated GPU.
			for (VkPhysicalDevice& Candidate : PhysicalDevices)
			{
				vkGetPhysicalDeviceProperties(Candidate, &PhysicalDeviceProperties);
				if (PhysicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
				{
					PhysicalDevice = Candidate;
					goto FoundPhysicalDevice;
				}
			}
			// Pick whatever was listed first and hope it works!
			PhysicalDevice = PhysicalDevices[0];
			vkGetPhysicalDeviceProperties(PhysicalDevice, &PhysicalDeviceProperties);
		FoundPhysicalDevice:
			VkPhysicalDeviceMemoryProperties PhysicalDeviceMemoryProperties;
			vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &PhysicalDeviceMemoryProperties);
		}
	}

	// Create the Vulkan rendering surface.
	if (!SDL_Vulkan_CreateSurface(Window, Instance, &Surface))
	{
		Surface = VK_NULL_HANDLE;
		std::cout << "Failed to Vulkan rendering surface.\n";
		return false;
	}
	else
	{
		std::vector<VkSurfaceFormatKHR> SurfaceFormats;
		{
			uint32_t SurfaceFormatCount;
			vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &SurfaceFormatCount, nullptr);
			SurfaceFormats.resize(SurfaceFormatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &SurfaceFormatCount, SurfaceFormats.data());
			SurfaceFormat = SurfaceFormats[0];
		}
	}

	// Find the graphics queue family index.
	{
		std::vector<VkQueueFamilyProperties> QueueFamilies;
		{
			uint32_t QueueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &QueueFamilyCount, nullptr);
			QueueFamilies.resize(QueueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &QueueFamilyCount, QueueFamilies.data());
		}
		for (uint32_t i = 0; i < QueueFamilies.size(); ++i)
		{
			VkQueueFamilyProperties& QueueFamily = QueueFamilies[i];
			VkBool32 SupportsPresent;
			vkGetPhysicalDeviceSurfaceSupportKHR(PhysicalDevice, i, Surface, &SupportsPresent);
			const uint32_t Match = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
			if ((QueueFamily.queueFlags & Match) == Match && SupportsPresent)
			{
				GraphicsQueueFamilyIndex = i;
				break;
			}
		}
		if (GraphicsQueueFamilyIndex == -1)
		{
			std::cout << "No compatible queue family found.\n";
			return false;
		}
	}

	// Create the Vulkan device.
	{
		float QueuePriority = 0.0;
		VkDeviceQueueCreateInfo QueueInfo;
		QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		QueueInfo.pNext = nullptr;
		QueueInfo.flags = 0;
		QueueInfo.queueFamilyIndex = GraphicsQueueFamilyIndex;
		QueueInfo.queueCount = 1;
		QueueInfo.pQueuePriorities = &QueuePriority;

		DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		DeviceExtensions.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);

		VkDeviceCreateInfo DeviceInfo;
		DeviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		DeviceInfo.pNext = nullptr;
		DeviceInfo.flags = 0;
		DeviceInfo.queueCreateInfoCount = 1;
		DeviceInfo.pQueueCreateInfos = &QueueInfo;
		DeviceInfo.enabledExtensionCount = uint32_t(DeviceExtensions.size());
		DeviceInfo.ppEnabledExtensionNames = DeviceExtensions.data();
		DeviceInfo.enabledLayerCount = 0;
		DeviceInfo.ppEnabledLayerNames = nullptr;
		DeviceInfo.pEnabledFeatures = nullptr;
		if (VK_CALL(vkCreateDevice, PhysicalDevice, &DeviceInfo, nullptr, &LogicalDevice))
		{
			LogicalDevice = VK_NULL_HANDLE;
			return false;
		}
		else
		{
			vkGetDeviceQueue(LogicalDevice, uint32_t(GraphicsQueueFamilyIndex), 0, &GraphicsQueue);
		}
	}

	// Create the NVIDIA RHI Device.
	{
		nvrhi::vulkan::DeviceDesc DeviceDesc;
		DeviceDesc.errorCB = MessageCallback::GetDefault();
		DeviceDesc.instance = Instance;
		DeviceDesc.instanceExtensions = InstanceExtensions.data();
		DeviceDesc.numInstanceExtensions = InstanceExtensions.size();
		DeviceDesc.physicalDevice = PhysicalDevice;
		DeviceDesc.device = LogicalDevice;
		DeviceDesc.graphicsQueue = GraphicsQueue;
		DeviceDesc.graphicsQueueIndex = GraphicsQueueFamilyIndex;
		DeviceDesc.deviceExtensions = DeviceExtensions.data();
		DeviceDesc.numDeviceExtensions = DeviceExtensions.size();

		Device = nvrhi::vulkan::createDevice(DeviceDesc);
	}
	return true;
}


int main(int argc, char* argv[])
{
	if (WindowSetup(true))
	{
		DrawLoop();
	}

	if (Device != nullptr)
	{
		Device.Reset();
	}
	if (LogicalDevice != VK_NULL_HANDLE)
	{
		vkDestroyDevice(LogicalDevice, nullptr);
	}
	if (Surface != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR(Instance, Surface, nullptr);
	}
	if (Instance != VK_NULL_HANDLE)
	{
		vkDestroyInstance(Instance, nullptr);
	}
	if (Window != nullptr)
	{
		SDL_Quit();
	}
	return 0;
}
