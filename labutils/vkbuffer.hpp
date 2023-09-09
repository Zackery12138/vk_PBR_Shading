#pragma once

#include <volk/volk.h>
#include <vk_mem_alloc.h>

#include <utility>

#include <cassert>

#include "allocator.hpp"

namespace labutils
{
	class Buffer
	{
		public:
			Buffer() noexcept, ~Buffer();

			explicit Buffer( VmaAllocator, VkBuffer = VK_NULL_HANDLE, VmaAllocation = VK_NULL_HANDLE ) noexcept;

			Buffer( Buffer const& ) = delete;
			Buffer& operator= (Buffer const&) = delete;

			Buffer( Buffer&& ) noexcept;
			Buffer& operator = (Buffer&&) noexcept;

		public:
			VkBuffer buffer = VK_NULL_HANDLE;
			VmaAllocation allocation = VK_NULL_HANDLE;

		private:
			VmaAllocator mAllocator = VK_NULL_HANDLE;
	};

	Buffer create_buffer( Allocator const&, VkDeviceSize, VkBufferUsageFlags, VmaMemoryUsage );
}
