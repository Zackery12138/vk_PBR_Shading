#pragma once

/* context_helpers.hxx is an internal header that defines functions used
 * internally by both vulkan_context.cpp and vulkan_window.cpp. These
 * functions were previously defined locally in vulkan_context.cpp
 */

#include <volk/volk.h>

#include <string>
#include <vector>
#include <unordered_set>

namespace labutils
{
	namespace detail
	{
		std::unordered_set<std::string> get_instance_layers();
		std::unordered_set<std::string> get_instance_extensions();

		VkInstance create_instance(
			std::vector<char const*> const& aEnabledLayers = {}, 
			std::vector<char const*> const& aEnabledInstanceExtensions = {} ,
			bool aEnableDebugUtils = false
		);

		VkDebugUtilsMessengerEXT create_debug_messenger( VkInstance );

		VKAPI_ATTR VkBool32 VKAPI_CALL debug_util_callback( VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT, VkDebugUtilsMessengerCallbackDataEXT const*, void* );


		std::unordered_set<std::string> get_device_extensions( VkPhysicalDevice );
	}
}
