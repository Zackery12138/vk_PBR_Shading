#include "load_data_to_vk.h"

#include <limits>

#include <cstring> // for std::memcpy()
#include <tuple>
#include "../labutils/error.hpp"
#include "../labutils/vkutil.hpp"
#include "../labutils/to_string.hpp"
namespace lut = labutils;



ModelPack set_up_model(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator,BakedModel const& aModel, 
    VkCommandPool& aLoadCmdPool, VkDescriptorPool& aDesPool, VkSampler& aSampler, VkDescriptorSetLayout& descLayout)
{
    ModelPack ret;
    //extract data from BakedModel

    for (auto& mesh : aModel.meshes) {
        std::vector<float> vertexData;

        for (std::size_t i = 0 ; i < mesh.positions.size(); ++i) {
            vertexData.emplace_back(mesh.positions[i].x);
            vertexData.emplace_back(mesh.positions[i].y);
            vertexData.emplace_back(mesh.positions[i].z);
            vertexData.emplace_back(mesh.texcoords[i].x);
            vertexData.emplace_back(mesh.texcoords[i].y);
            vertexData.emplace_back(mesh.normals[i].x);
            vertexData.emplace_back(mesh.normals[i].y);
            vertexData.emplace_back(mesh.normals[i].z);
            vertexData.emplace_back(mesh.tangents[i].x);
            vertexData.emplace_back(mesh.tangents[i].y);
            vertexData.emplace_back(mesh.tangents[i].z);
            vertexData.emplace_back(mesh.tangents[i].w);
        }

        //create buffers
        lut::Buffer vertexGPU = lut::create_buffer(aAllocator, vertexData.size() * sizeof(float), 
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        lut::Buffer indexGPU = lut::create_buffer(aAllocator, mesh.indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        lut::Buffer vertexStaging = lut::create_buffer(aAllocator, vertexData.size() * sizeof(float),
            			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        lut::Buffer indexStaging = lut::create_buffer(aAllocator, mesh.indices.size() * sizeof(uint32_t),
            			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU); 

        void* vertPtr = nullptr;
        if (auto const res = vmaMapMemory(aAllocator.allocator, vertexStaging.allocation, &vertPtr); VK_SUCCESS != res)
        {
            throw lut::Error("Mapping memory for writing\n""vmaMapMemory() returned %s" , lut::to_string(res).c_str());
        }
        std::memcpy(vertPtr, vertexData.data(), vertexData.size() * sizeof(float)); 
        vmaUnmapMemory(aAllocator.allocator, vertexStaging.allocation);

        void* indexPtr = nullptr;
        if (auto const res = vmaMapMemory(aAllocator.allocator, indexStaging.allocation, &indexPtr); VK_SUCCESS != res) 
        {
            throw lut::Error("Mapping memory for writing\n""vmaMapMemory() returned %s", lut::to_string(res).c_str());
        }
        std::memcpy(indexPtr, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
        vmaUnmapMemory(aAllocator.allocator, indexStaging.allocation);

        lut::Fence uploadComplete = lut::create_fence(aWindow);

        lut::CommandPool uploadPool = lut::create_command_pool(aWindow);
        VkCommandBuffer uploadCmd = lut::alloc_command_buffer(aWindow, uploadPool.handle);

        VkCommandBufferBeginInfo beginInfo{}; 
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; 
        beginInfo.flags = 0; 
        beginInfo.pInheritanceInfo = nullptr; 

        if (auto const res = vkBeginCommandBuffer(uploadCmd, &beginInfo); VK_SUCCESS != res) 
        {
            throw lut::Error("Beginning command buffer recording\n" "vkBeginCommandBuffer() returned %s", lut::to_string(res).c_str()); 
        }

        VkBufferCopy vcopy{};
        vcopy.size = vertexData.size() * sizeof(float);

        vkCmdCopyBuffer(uploadCmd, vertexStaging.buffer, vertexGPU.buffer, 1, &vcopy); 
        lut::buffer_barrier(uploadCmd, vertexGPU.buffer, 
            VK_ACCESS_TRANSFER_WRITE_BIT, 
            VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, 
            VK_PIPELINE_STAGE_TRANSFER_BIT, 
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT); 

        VkBufferCopy icopy{}; 
        icopy.size = mesh.indices.size() * sizeof(uint32_t); 

        vkCmdCopyBuffer(uploadCmd, indexStaging.buffer, indexGPU.buffer, 1, &icopy); 
         
        lut::buffer_barrier(uploadCmd, indexGPU.buffer, 
            VK_ACCESS_TRANSFER_WRITE_BIT, 
            VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,  
            VK_PIPELINE_STAGE_TRANSFER_BIT, 
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT); 

        if (auto const res = vkEndCommandBuffer(uploadCmd); VK_SUCCESS != res) 
        {
            throw lut::Error("Ennding command buffer recording\n""vkEndCommandBuffer() returned %s", lut::to_string(res).c_str()); 
        }

        // Submit transfer commands
        VkSubmitInfo submitInfo{}; 
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; 
        submitInfo.commandBufferCount = 1;  
        submitInfo.pCommandBuffers = &uploadCmd; 

        if (auto const res = vkQueueSubmit(aWindow.graphicsQueue, 1, &submitInfo, uploadComplete.handle); VK_SUCCESS != res) 
        {
            throw lut::Error("Submitting commands\n" "vkQueueSubmit() returned %s", lut::to_string(res));
        }

        // Wait for commands to finish before we destroy the temporary resources
        // required for the transfers (staging buffers, command pool, ...)
        //
        // The code doesn¡¯t destory the resources implicitly ¨C the resources are
        // destroyed by the destructors of the labutils wrappers for the various
        // objects once we leave the function¡¯s scope.
        if (auto const res = vkWaitForFences(aWindow.device, 1, &uploadComplete.handle, VK_TRUE, std::numeric_limits<std::uint64_t>::max()); VK_SUCCESS != res) 
        {
            throw lut::Error("Waiting for upload to complete\n" "vkWaitForFences() returned %s", lut::to_string(res).c_str());
        }

        Mesh meshData;
        meshData.vertices = std::move(vertexGPU);
        meshData.indexCount = static_cast<uint32_t>(mesh.indices.size());
        meshData.indices = std::move(indexGPU);
        meshData.matID = mesh.materialId;

        ret.meshes.emplace_back(std::move(meshData));
    }

    for (auto& texture : aModel.textures) 
    {
        
        uint32_t textureId = static_cast<uint32_t>(&texture - &aModel.textures[0]);

        VkFormat format = get_texture_format(aModel, textureId); 
        

        Texture texData;
        lut::Image image;
        lut::ImageView view;

        if (format == VK_FORMAT_R8_UNORM) 
        {
            image = lut::load_single_chanel_image_texture2d(texture.path.c_str(), aWindow, aLoadCmdPool, aAllocator, format);  
            view = lut::create_image_view_texture2d(aWindow, image.image, format); 
        }
        else
        {
            image = lut::load_image_texture2d(texture.path.c_str(), aWindow, aLoadCmdPool, aAllocator, format); 
            view = lut::create_image_view_texture2d(aWindow, image.image, format);  
        }


        texData.image = std::move(image);
        texData.view = std::move(view);

        ret.textures.emplace_back(std::move(texData));
    }

    //create descriptor sets for every material

    std::vector<VkDescriptorSet> matDescs;
    uint32_t materialCount = static_cast<uint32_t>(aModel.materials.size());
    matDescs.resize(materialCount);
    std::vector<VkDescriptorSetLayout> layouts(materialCount, descLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = aDesPool;
    allocInfo.descriptorSetCount = materialCount;
    allocInfo.pSetLayouts = layouts.data();

    if (auto const res = vkAllocateDescriptorSets(aWindow.device, &allocInfo, matDescs.data()); VK_SUCCESS != res) 
    {
        throw lut::Error("Allocating descriptor sets\n" "vkAllocateDescriptorSets() returned %s", lut::to_string(res).c_str());
    }

    Texture dummyNomalMapTex = load_dummy_normal_map(aWindow, aAllocator, aLoadCmdPool);
    ret.textures.emplace_back(std::move(dummyNomalMapTex));

    for (uint32_t i = 0; i < materialCount; ++i)
    {
        VkDescriptorImageInfo imageInfo[4]{};

        for (uint32_t j = 0; j < 4; ++j)
        {
            imageInfo[j].sampler = aSampler;
            imageInfo[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        imageInfo[0].imageView = ret.textures[aModel.materials[i].baseColorTextureId].view.handle;
        imageInfo[1].imageView = ret.textures[aModel.materials[i].roughnessTextureId].view.handle;
        imageInfo[2].imageView = ret.textures[aModel.materials[i].metalnessTextureId].view.handle;
        if (aModel.materials[i].normalMapTextureId != 0xffffffff)
            imageInfo[3].imageView = ret.textures[aModel.materials[i].normalMapTextureId].view.handle;
        else
            imageInfo[3].imageView = ret.textures.back().view.handle;

        VkWriteDescriptorSet desc[4]{};
        for (uint32_t j = 0; j < 4; ++j)
        {
            desc[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            desc[j].dstSet = matDescs[i];
            desc[j].dstBinding = j;
            desc[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            desc[j].descriptorCount = 1;
            desc[j].pImageInfo = &imageInfo[j];
        }

        constexpr auto numSets = sizeof(desc) / sizeof(desc[0]);
        vkUpdateDescriptorSets(aWindow.device, numSets, desc, 0, nullptr);
      
    }

    ret.matDecriptors = std::move(matDescs);

    return ret;

}

VkFormat get_texture_format(const BakedModel& aModel, uint32_t textureId)
{
    for (auto& material : aModel.materials) {
        if (textureId == material.baseColorTextureId || textureId == material.roughnessTextureId ||
            textureId == material.metalnessTextureId || textureId == material.normalMapTextureId) {
            if (textureId == material.baseColorTextureId) { 
                return VK_FORMAT_R8G8B8A8_SRGB; 
            }
            else if (textureId == material.roughnessTextureId || textureId == material.metalnessTextureId) { 
                return VK_FORMAT_R8_UNORM; 
            }
            else if (textureId == material.normalMapTextureId) { 
                return VK_FORMAT_R8G8B8A8_UNORM; 
            }
        }
    }
    return VK_FORMAT_R8G8B8A8_SRGB;

}

Texture load_dummy_normal_map(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator, VkCommandPool& aCmdPool)
{
    std::uint32_t width = 1, height = 1;
    std::uint8_t data[4] = { 128, 128, 255, 255 };//0,0,1,1


    // Create staging buffer and copy image data to it
    auto const sizeInBytes = width * height * 4;
    auto staging = create_buffer(aAllocator, sizeInBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    void* sptr = nullptr;
    if (auto const res = vmaMapMemory(aAllocator.allocator, staging.allocation, &sptr); VK_SUCCESS != res) 
    {
        throw lut::Error("Mapping memory for writing\n""vmaMapMemory() returned %s", lut::to_string(res).c_str());
    }

    std::memcpy(sptr, data, sizeInBytes);
    vmaUnmapMemory(aAllocator.allocator, staging.allocation); 

    lut::Image image = lut::create_image_texture2d(aAllocator, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    VkCommandBuffer cbuff = lut::alloc_command_buffer(aWindow, aCmdPool);

    VkCommandBufferBeginInfo beginInfo{}; 
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; 
    beginInfo.flags = 0; 
    beginInfo.pInheritanceInfo = nullptr; 

    if (auto const res = vkBeginCommandBuffer(cbuff, &beginInfo); VK_SUCCESS != res)
    {
        throw lut::Error("Beginning command buffer recording\n" "vkBeginCommandBuffer() returned %s", lut::to_string(res).c_str());
    }
    // Transition image layout
    lut::image_barrier(cbuff, image.image, 0,
        VK_ACCESS_TRANSFER_WRITE_BIT, 
        VK_IMAGE_LAYOUT_UNDEFINED, 
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
        VK_PIPELINE_STAGE_TRANSFER_BIT, 
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } 
    );

    // Upload data from staging buffer to image
    VkBufferImageCopy copy; 
    copy.bufferOffset = 0; 
    copy.bufferRowLength = 0; 
    copy.bufferImageHeight = 0; 
    copy.imageSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; 
    copy.imageOffset = VkOffset3D{ 0, 0, 0 }; 
    copy.imageExtent = VkExtent3D{ width, height, 1 }; 

    vkCmdCopyBufferToImage(cbuff, staging.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    // Transition image layout for shader read
    lut::image_barrier(cbuff, image.image,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    );

    //End command recording
    if (auto const res = vkEndCommandBuffer(cbuff); VK_SUCCESS != res)
    {
        throw lut::Error("Ending command buffer recording\n" "vkEndCommandBuffer() returned %s", lut::to_string(res).c_str());
    }

    lut::Fence uploadComplete = lut::create_fence(aWindow);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cbuff;

    if (auto const res = vkQueueSubmit(aWindow.graphicsQueue, 1, &submitInfo, uploadComplete.handle); VK_SUCCESS != res)
    {
        throw lut::Error("Submitting command\n" "vkQueueSubmit() returned %s", lut::to_string(res).c_str());
    }

    if (auto const res = vkWaitForFences(aWindow.device, 1, &uploadComplete.handle,
        VK_TRUE, std::numeric_limits<std::uint64_t>::max()); VK_SUCCESS != res)
    {
        throw lut::Error("waiting for upload to complete\n""vkWaitForFences() returned %s", lut::to_string(res).c_str());
    }
    // Return resulting image
    // Most temporary resources are destroyed automatically through their
    // destructors. However, the command buffer we must free manually.
    vkFreeCommandBuffers(aWindow.device, aCmdPool, 1, &cbuff);

    lut::ImageView view = lut::create_image_view_texture2d(aWindow, image.image, VK_FORMAT_R8G8B8A8_UNORM);

    Texture ret;
    ret.image = std::move(image);
    ret.view = std::move(view);
    return ret;

}
