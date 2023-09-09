#pragma once
#include <cstdint>

#include "../labutils/vulkan_context.hpp"

#include "../labutils/vkbuffer.hpp"
#include "../labutils/allocator.hpp" 
#include "baked_model.hpp"
#include "../labutils/vkimage.hpp"
#include "../labutils/vkobject.hpp"
#include "../labutils/vulkan_window.hpp"
namespace lut = labutils;

struct Texture {
	lut::Image image;
	lut::ImageView view;
};


struct Mesh {
	lut::Buffer vertices; // pos(3), tex(2), norm(3), tangent(4)
	lut::Buffer indices;
	std::uint32_t indexCount = 0;
	std::uint32_t matID = 0;
};

struct ModelPack {
	std::vector<Mesh> meshes;
	std::vector<VkDescriptorSet> matDecriptors;
	std::vector<Texture> textures;
};



ModelPack set_up_model(lut::VulkanWindow const&, lut::Allocator const&, BakedModel const&, VkCommandPool&, VkDescriptorPool&, VkSampler&, VkDescriptorSetLayout&);
VkFormat get_texture_format(const BakedModel& aModel, uint32_t textureId);

Texture load_dummy_normal_map(lut::VulkanWindow const&, lut::Allocator const&, VkCommandPool&);


