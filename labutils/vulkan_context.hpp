#pragma once

#include <volk/volk.h>

#include <cstdint>

namespace labutils
{
	class VulkanContext
	{
		public:
			VulkanContext(), ~VulkanContext();

			// Move-only
			VulkanContext( VulkanContext const& ) = delete;
			VulkanContext& operator= (VulkanContext const&) = delete;

			VulkanContext( VulkanContext&& ) noexcept;
			VulkanContext& operator= (VulkanContext&&) noexcept;

		public:
			VkInstance instance = VK_NULL_HANDLE;
			VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;


			VkDevice device = VK_NULL_HANDLE;

			std::uint32_t graphicsFamilyIndex = 0;
			VkQueue graphicsQueue = VK_NULL_HANDLE;

			
			//bool haveDebugUtils = false;
			VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
	};

	VulkanContext make_vulkan_context();
}

//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
