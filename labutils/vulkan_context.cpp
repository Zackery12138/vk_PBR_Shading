#include "vulkan_context.hpp"

#include <vector>
#include <utility>
#include <optional>

#include <cstdio>
#include <cassert>

#include "error.hpp"
#include "to_string.hpp"
#include "context_helpers.hxx"
namespace lut = labutils;

namespace
{
	VkPhysicalDevice select_device( VkInstance );
	float score_device( VkPhysicalDevice );

	std::optional<std::uint32_t> find_graphics_queue_family( VkPhysicalDevice );

	VkDevice create_device( 
		VkPhysicalDevice,
		std::uint32_t aQueueFamily
	);
}

namespace labutils
{
	// VulkanContext
	VulkanContext::VulkanContext() = default;

	VulkanContext::~VulkanContext()
	{
		// Device-related objects
		if( VK_NULL_HANDLE != device )
			vkDestroyDevice( device, nullptr );

		// Instance-related objects
		if( VK_NULL_HANDLE != debugMessenger )
			vkDestroyDebugUtilsMessengerEXT( instance, debugMessenger, nullptr );

		if( VK_NULL_HANDLE != instance )
			vkDestroyInstance( instance, nullptr );
	}

	VulkanContext::VulkanContext( VulkanContext&& aOther ) noexcept
		/* See https://en.cppreference.com/w/cpp/utility/exchange */
		: instance( std::exchange( aOther.instance, VK_NULL_HANDLE ) )
		, physicalDevice( std::exchange( aOther.physicalDevice, VK_NULL_HANDLE ) )
		, device( std::exchange( aOther.device, VK_NULL_HANDLE ) )
		, graphicsFamilyIndex( aOther.graphicsFamilyIndex )
		, graphicsQueue( std::exchange( aOther.graphicsQueue, VK_NULL_HANDLE ) )
		, debugMessenger( std::exchange( aOther.debugMessenger, VK_NULL_HANDLE ) )
	{}

	VulkanContext& VulkanContext::operator=( VulkanContext&& aOther ) noexcept
	{
		/* We can't just copy over the data from aOther, as we need to ensure that
		 * any potential objects help by `this` are destroyed properly. Swapping
		 * the data of `this` with aOther will do so: the data of `this` ends up in
		 * aOther, and is subsequently destroyed properly by aOther's destructor.
		 *
		 * Advantages are that the move-operation is quite cheap and can trivially
		 * be `noexcept`. Disadvantage is that the destruction of the resources
		 * held by `this` is delayed until aOther's destruction.
		 *
		 * This is a somewhat common way of implementing move assignments.
		 */
		std::swap( instance, aOther.instance );
		std::swap( physicalDevice, aOther.physicalDevice );
		std::swap( device, aOther.device );
		std::swap( graphicsFamilyIndex, aOther.graphicsFamilyIndex );
		std::swap( graphicsQueue, aOther.graphicsQueue );
		std::swap( debugMessenger, aOther.debugMessenger );
		return *this;
	}


	// make_vulkan_context()
	VulkanContext make_vulkan_context()
	{
		VulkanContext ret;

		// Initialize Volk
		if( auto const res = volkInitialize(); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to load Vulkan API\n" 
				"Volk returned error %s", lut::to_string(res).c_str()
			);
		}

		// Check for instance layers and extensions
		auto const supportedLayers = detail::get_instance_layers();
		auto const supportedExtensions = detail::get_instance_extensions();

		bool enableDebugUtils = false;

		std::vector<char const*> enabledLayers, enabledExensions;

#		if !defined(NDEBUG) // debug builds only
		if( supportedLayers.count( "VK_LAYER_KHRONOS_validation" ) )
		{
			enabledLayers.emplace_back( "VK_LAYER_KHRONOS_validation" );
		}

		if( supportedExtensions.count( "VK_EXT_debug_utils" ) )
		{
			enableDebugUtils = true;
			enabledExensions.emplace_back( "VK_EXT_debug_utils" );
		}
#		endif // ~ debug builds

		for( auto const& layer : enabledLayers )
			std::fprintf( stderr, "Enabling layer: %s\n", layer );

		for( auto const& extension : enabledExensions )
			std::fprintf( stderr, "Enabling instance extension: %s\n", extension );

		// Create Vulkan instance
		ret.instance = detail::create_instance( enabledLayers, enabledExensions, enableDebugUtils );


		// Load rest of the Vulkan API
		volkLoadInstance( ret.instance );

		// Setup debug messenger
		if( enableDebugUtils )
			ret.debugMessenger = detail::create_debug_messenger( ret.instance );

		// Select appropriate Vulkan device
		ret.physicalDevice = select_device( ret.instance );
		if( VK_NULL_HANDLE == ret.physicalDevice )
			throw lut::Error( "No suitable physical device found!" );

		{
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties( ret.physicalDevice, &props );
			std::fprintf( stderr, "Selected device: %s (%d.%d.%d)\n", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion) );
		}

		// Create a logical device
		if( auto const index = find_graphics_queue_family( ret.physicalDevice ) )
		{
			ret.graphicsFamilyIndex = *index;
		}
		else
		{
			throw lut::Error( "No queue family with GRAPHICS" );
		}

		ret.device = create_device( ret.physicalDevice, ret.graphicsFamilyIndex );

		// Retrieve VkQueue
		vkGetDeviceQueue( ret.device, ret.graphicsFamilyIndex, 0, &ret.graphicsQueue );

		assert( VK_NULL_HANDLE != ret.graphicsQueue );

		// Done
		return ret;
	}
}

namespace
{
	std::optional<std::uint32_t> find_graphics_queue_family( VkPhysicalDevice aPhysicalDev )
	{
		std::uint32_t numQueues = 0;
		vkGetPhysicalDeviceQueueFamilyProperties( aPhysicalDev, &numQueues, nullptr );

		std::vector<VkQueueFamilyProperties> families( numQueues );
		vkGetPhysicalDeviceQueueFamilyProperties( aPhysicalDev, &numQueues, families.data() );

		for( std::uint32_t i = 0; i < numQueues; ++i )
		{
			auto const& family = families[i];

			if( VK_QUEUE_GRAPHICS_BIT & family.queueFlags )
				return i;
		}

		return {};
	}

	VkDevice create_device( VkPhysicalDevice aPhysicalDev, std::uint32_t aQueueFamily )
	{
		float queuePriorities[1] = { 1.f };

		VkDeviceQueueCreateInfo queueInfo{};
		queueInfo.sType  = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfo.queueFamilyIndex  = aQueueFamily;
		queueInfo.queueCount        = 1;
		queueInfo.pQueuePriorities  = queuePriorities;

		VkPhysicalDeviceFeatures deviceFeatures{};
		// No extra features for now.
		
		VkDeviceCreateInfo deviceInfo{};
		deviceInfo.sType  = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

		deviceInfo.queueCreateInfoCount  = 1;
		deviceInfo.pQueueCreateInfos     = &queueInfo;

		deviceInfo.pEnabledFeatures      = &deviceFeatures;

		VkDevice device = VK_NULL_HANDLE;
		if( auto const res = vkCreateDevice( aPhysicalDev, &deviceInfo, nullptr, &device ); VK_SUCCESS != res )
		{
			throw lut::Error( "Can't create logical device\n"
				"vkCreateDevice() returned %s", lut::to_string(res).c_str() 
			);
		}

		return device;
	}
}

namespace
{
	float score_device( VkPhysicalDevice aPhysicalDev )
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties( aPhysicalDev, &props );

		// Only consider Vulkan 1.1 devices
		auto const major = VK_API_VERSION_MAJOR( props.apiVersion );
		auto const minor = VK_API_VERSION_MINOR( props.apiVersion );

		if( major < 1 || (major == 1 && minor < 2) )
			return -1.f;

		// Discrete GPU > Integrated GPU > others
		float score = 0.f;

		if( VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU == props.deviceType )
			score += 500.f;
		else if( VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU == props.deviceType )
			score += 100.f;

		return score;
	}
	
	VkPhysicalDevice select_device( VkInstance aInstance )
	{
		std::uint32_t numDevices = 0;
		if( auto const res = vkEnumeratePhysicalDevices( aInstance, &numDevices, nullptr ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to get physical device count\n"
				"vkEnumeratePhysicalDevices() returned %s", lut::to_string(res).c_str()
			);
		}

		std::vector<VkPhysicalDevice> devices( numDevices, VK_NULL_HANDLE );
		if( auto const res = vkEnumeratePhysicalDevices( aInstance, &numDevices, devices.data() ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to get physical device list\n"
				"vkEnumeratePhysicalDevices() returned %s", lut::to_string(res).c_str()
			);
		}

		float bestScore = -1.f;
		VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

		for( auto const device : devices )
		{
			auto const score = score_device( device );
			if( score > bestScore )
			{
				bestScore = score;
				bestDevice = device;
			}
		}

		return bestDevice;
	}
}

