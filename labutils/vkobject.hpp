#pragma once

#include <volk/volk.h>

#include <utility>

#include <cassert>

namespace labutils
{
	// Provide a small wrapper around Vulkan handles that destroys these when
	// the object goes out of scope. 
	//
	// The wrappers are move-only, i.e., we cannot make copies of them. We are
	// only allowed to pass ownership ("move") of the underlying handle to a 
	// different object. This is similar to how std::unique_ptr<> works. See
	// https://en.cppreference.com/w/cpp/memory/unique_ptr
	// for a detailed description and examples of unique_ptr.
	//
	// We could implement an indivdual class for each type of Vulkan handle.
	//
	// Example for VkRenderPass:
#	if 0
	class RenderPass final
	{
		public:
			RenderPass() noexcept, ~RenderPass();

			explicit RenderPass( VkDevice, VkRenderPass = VK_NULL_HANDLE ) noexcept;

			RenderPass( RenderPass const& ) = delete;
			RenderPass& operator= (RenderPass const&) = delete;

			RenderPass( RenderPass&& ) noexcept;
			RenderPass& operator = (RenderPass&&) noexcept;

		public:
			VkRenderPass handle = VK_NULL_HANDLE;

		private:
			VkDevice mParent = VK_NULL_HANDLE;
	};
#	endif

	// Defining a custom class like the one shown above for the various Vulkan 
	// object types is quite tedious. Instead, we can solve the problem with a 
	// C++ template.
	//
	// If you're unfamiliar with C++ templates, think of the UniqueHandle<>
	// template below as a receipe to create a class such as the one shown above.
	// 
	// We can then define instances (=classes) of the template, as
	//
	// using RenderPass = UniqueHandle< VkRenderPass, VkDevice, vkDestroyRenderPass >;
	//
	// (see list below the UniqueHandle template definition).
	//
	// The template takes three arguments:
	//   - The type of Vulkan handle we want to wrap (VkRenderPass in the example)
	//   - The type of the parent object (here VkDevice)
	//   - The function that is used to destroy the Vulkan handle, which is 
	//     vkDestroyRenderPass in this case. (Recall that this function takes
	//     a VkDevice handle as its first argument.)

	template< typename tParent, typename tHandle >
	using DestroyFn = void (*)( tParent, tHandle, VkAllocationCallbacks const* );

	template< typename tHandle, typename tParent, DestroyFn<tParent,tHandle>& tDestroyFn >
	class UniqueHandle final
	{
		public:
			UniqueHandle() noexcept = default;
			explicit UniqueHandle( tParent, tHandle = VK_NULL_HANDLE ) noexcept;

			~UniqueHandle();

			UniqueHandle( UniqueHandle const& ) = delete;
			UniqueHandle& operator= (UniqueHandle const&) = delete;

			UniqueHandle( UniqueHandle&& ) noexcept;
			UniqueHandle& operator = (UniqueHandle&&) noexcept;

		public:
			tHandle handle = VK_NULL_HANDLE;

		private:
			tParent mParent = VK_NULL_HANDLE;
	};

	// Pre-defined wrapper types
	using RenderPass = UniqueHandle< VkRenderPass, VkDevice, vkDestroyRenderPass >;
	using Framebuffer = UniqueHandle< VkFramebuffer, VkDevice, vkDestroyFramebuffer >;

	using DescriptorPool = UniqueHandle< VkDescriptorPool, VkDevice, vkDestroyDescriptorPool >;
	using DescriptorSetLayout = UniqueHandle< VkDescriptorSetLayout, VkDevice, vkDestroyDescriptorSetLayout >;

	using Pipeline = UniqueHandle< VkPipeline, VkDevice, vkDestroyPipeline >;
	using PipelineLayout = UniqueHandle< VkPipelineLayout, VkDevice, vkDestroyPipelineLayout >;

	using ShaderModule = UniqueHandle< VkShaderModule, VkDevice, vkDestroyShaderModule >;

	using CommandPool = UniqueHandle< VkCommandPool, VkDevice, vkDestroyCommandPool >;

	using Fence = UniqueHandle< VkFence, VkDevice, vkDestroyFence >;
	using Semaphore = UniqueHandle< VkSemaphore, VkDevice, vkDestroySemaphore >;

	using ImageView = UniqueHandle< VkImageView, VkDevice, vkDestroyImageView >;
	using Sampler = UniqueHandle< VkSampler, VkDevice, vkDestroySampler >;
}

#include "vkobject.inl"
