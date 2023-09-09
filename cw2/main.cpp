#include <tuple>
#include <chrono>
#include <limits>
#include <vector>
#include <stdexcept>

#include <cstdio>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <volk/volk.h>

#if !defined(GLM_FORCE_RADIANS)
#	define GLM_FORCE_RADIANS
#endif
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../labutils/to_string.hpp"
#include "../labutils/vulkan_window.hpp"

#include "../labutils/angle.hpp"
using namespace labutils::literals;

#include "../labutils/error.hpp"
#include "../labutils/vkutil.hpp"
#include "../labutils/vkimage.hpp"
#include "../labutils/vkobject.hpp"
#include "../labutils/vkbuffer.hpp"
#include "../labutils/allocator.hpp" 
namespace lut = labutils;

#include "baked_model.hpp"
#include "load_data_to_vk.h"
#include <iostream>


namespace
{
	using Clock_ = std::chrono::steady_clock;
	using Secondsf_ = std::chrono::duration<float, std::ratio<1>>;

	namespace cfg
	{
		// Compiled shader code for the graphics pipeline
		// See sources in exercise4/shaders/*. 
#		define SHADERDIR_ "assets/cw2/shaders/"
		constexpr char const* kVertShaderPath = SHADERDIR_ "default.vert.spv";
		constexpr char const* kFragShaderPath = SHADERDIR_ "default.frag.spv";
		//constexpr char const* kAlphaFragShaderPath = SHADERDIR_ "defaultAlpha.frag.spv";
#		undef SHADERDIR_

#		define ASSETDIR_ "assets/cw2/"
		constexpr char const* kBakedModelPath = ASSETDIR_"sponza-pbr.comp5822mesh";
#		undef ASSETDIR_

		constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;


		// General rule: with a standard 24 bit or 32 bit float depth buffer,
		// you can support a 1:1000 ratio between the near and far plane with
		// minimal depth fighting. Larger ratios will introduce more depth
		// fighting problems; smaller ratios will increase the depth buffer's
		// resolution but will also limit the view distance.
		constexpr float kCameraNear = 0.01f;
		constexpr float kCameraFar = 100.f;

		constexpr auto kCameraFov = 60.0_degf;

		// Camera settings.
		// These are determined empirically (i.e., by testing and picking something
		// that felt OK).
		//
		// General rule: for debugging, you want to be able to move around quickly
		// in the scene (but slow down if necessary). The exact settings here
		// depend on the scene scale and similar settings.
		constexpr float kCameraBaseSpeed = 1.7f; // units/second
		constexpr float kCameraFastMult = 5.f; // speed multiplier
		constexpr float kCameraSlowMult = 0.05f; // speed multiplier

		constexpr float kCameraMouseSensitivity = 0.01f; // radians per pixel
	}

	// GLFW callbacks
	void glfw_callback_key_press(GLFWwindow*, int, int, int, int);
	void glfw_callback_button(GLFWwindow*, int, int, int);
	void glfw_callback_motion(GLFWwindow*, double, double);

	enum class EInputState
	{
		forward,
		backward,
		strafeLeft,
		strafeRight,
		levitate,
		sink,
		fast,
		slow,
		mousing,
		lightRotate,
		max
	};

	struct  UserState
	{
		bool inputMap[std::size_t(EInputState::max)] = {};

		float mouseX = 0.f, mouseY = 0.f;
		float previousX = 0.f, previousY = 0.f;

		bool wasMousing = false;
		//bool wasLightOrbiting = false;

		glm::mat4 camera2world = glm::identity<glm::mat4>();
		glm::vec3 light_pos = glm::vec3(0, 2, 0);
	};

	//Also declare a update_user_state function to update the state based on the elapsed time:
	void update_user_state(UserState&, float aElapsedTime);


	// Uniform data
	namespace glsl
	{
		struct alignas(16) SceneUniform
		{
			glm::mat4 camera;
			glm::mat4 projection;
			glm::mat4 projCam;
			glm::vec3 cameraPos;
			float _pad0;
			glm::vec3 lightPos;
			float _pad1;
			glm::vec3 lightColor;
			float _pad2;
		};

		// We want to use vkCmdUpdateBuffer() to update the contents of our uniform
		// buffers. vkCmdUpdateBuffer() has a number of requirements, including
		// the two below. See https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdUpdateBuffer.html
		static_assert(sizeof(SceneUniform) <= 65536, "SceneUniform must be less than 65536 bytes for vkCmdUpdateBuffer");
		static_assert(sizeof(SceneUniform) % 4 == 0, "SceneUniform size must be a multiple of 4 bytes");
	}

	// Helpers:
	lut::RenderPass create_render_pass(lut::VulkanWindow const&);

	lut::DescriptorSetLayout create_scene_descriptor_layout(lut::VulkanWindow const&);

	lut::DescriptorSetLayout create_material_descriptor_layout(lut::VulkanWindow const& aWindow);

	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const&, VkDescriptorSetLayout, VkDescriptorSetLayout);
	lut::Pipeline create_pipeline(lut::VulkanWindow const&, VkRenderPass, VkPipelineLayout);
	lut::Pipeline create_alpha_pipeline(lut::VulkanWindow const&, VkRenderPass, VkPipelineLayout);

	std::tuple<lut::Image, lut::ImageView> create_depth_buffer(lut::VulkanWindow const&, lut::Allocator const&);

	void create_swapchain_framebuffers(
		lut::VulkanWindow const&,
		VkRenderPass,
		std::vector<lut::Framebuffer>&,
		VkImageView aDepthView
	);

	void update_scene_uniforms(
		glsl::SceneUniform&,
		std::uint32_t aFramebufferWidth,
		std::uint32_t aFramebufferHeight,
		UserState aState
	);

	void record_commands(
		VkCommandBuffer,
		VkRenderPass,
		VkFramebuffer,
		VkPipeline,
		VkExtent2D const&,
		VkBuffer aSceneUBO,
		glsl::SceneUniform const&,
		VkPipelineLayout,
		VkDescriptorSet aSceneDescriptors,
		ModelPack& aModel,
		VkPipeline aSecondGraphicsPipe, 
		BakedModel& bakedModel
	);
	void submit_commands(
		lut::VulkanWindow const&,
		VkCommandBuffer,
		VkFence,
		VkSemaphore,
		VkSemaphore
	);
	void present_results(
		VkQueue,
		VkSwapchainKHR,
		std::uint32_t aImageIndex,
		VkSemaphore,
		bool& aNeedToRecreateSwapchain
	);
}


int main() try
{
	// Create Vulkan Window
	auto window = lut::make_vulkan_window();

	// Configure the GLFW window
	UserState state{};

	glfwSetWindowUserPointer(window.window, &state);

	glfwSetKeyCallback(window.window, &glfw_callback_key_press);
	glfwSetMouseButtonCallback(window.window, &glfw_callback_button);
	glfwSetCursorPosCallback(window.window, &glfw_callback_motion);


	// Create VMA allocator
	lut::Allocator allocator = lut::create_allocator(window);

	// Intialize resources
	lut::RenderPass renderPass = create_render_pass(window);

	//TODO- (Section 3) create scene descriptor set layout
	lut::DescriptorSetLayout sceneLayout = create_scene_descriptor_layout(window);
	lut::DescriptorSetLayout objectLayout = create_material_descriptor_layout(window);

	lut::PipelineLayout pipeLayout = create_pipeline_layout(window, sceneLayout.handle, objectLayout.handle);
	lut::Pipeline pipe = create_pipeline(window, renderPass.handle, pipeLayout.handle);
	lut::Pipeline alphaPipe = create_alpha_pipeline(window, renderPass.handle, pipeLayout.handle);


	auto [depthBuffer, depthBufferView] = create_depth_buffer(window, allocator);

	std::vector<lut::Framebuffer> framebuffers;
	create_swapchain_framebuffers(window, renderPass.handle, framebuffers, depthBufferView.handle);

	lut::CommandPool cpool = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	std::vector<VkCommandBuffer> cbuffers;
	std::vector<lut::Fence> cbfences;

	for (std::size_t i = 0; i < framebuffers.size(); ++i)
	{
		cbuffers.emplace_back(lut::alloc_command_buffer(window, cpool.handle));
		cbfences.emplace_back(lut::create_fence(window, VK_FENCE_CREATE_SIGNALED_BIT));
	}

	lut::Semaphore imageAvailable = lut::create_semaphore(window);
	lut::Semaphore renderFinished = lut::create_semaphore(window);


	ModelPack ourModel;
	BakedModel bakedModel;
	lut::DescriptorPool dPool = lut::create_descriptor_pool(window);
	lut::Sampler defaultSampler = lut::create_default_sampler(window);
	{
		lut::CommandPool loadCmdPool = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
		bakedModel = load_baked_model(cfg::kBakedModelPath);
		ourModel = set_up_model(window, allocator, bakedModel, loadCmdPool.handle, dPool.handle, defaultSampler.handle, objectLayout.handle);
	}

	//TODO- (Section 3) create scene uniform buffer with lut::create_buffer()
	lut::Buffer sceneUBO = lut::create_buffer(allocator,
		sizeof(glsl::SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);


	//TODO- (Section 3) allocate descriptor set for uniform buffer
	VkDescriptorSet sceneDescriptors = lut::alloc_desc_set(window, dPool.handle, sceneLayout.handle);

	//TODO- (Section 3) initialize descriptor set with vkUpdateDescriptorSets
	{
		VkWriteDescriptorSet desc[1]{};

		VkDescriptorBufferInfo sceneUboInfo{};
		sceneUboInfo.buffer = sceneUBO.buffer;
		sceneUboInfo.range = VK_WHOLE_SIZE;

		desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc[0].dstSet = sceneDescriptors;
		desc[0].dstBinding = 0;
		desc[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		desc[0].descriptorCount = 1;
		desc[0].pBufferInfo = &sceneUboInfo;

		constexpr auto numSets = sizeof(desc) / sizeof(desc[0]);
		vkUpdateDescriptorSets(window.device, numSets, desc, 0, nullptr);
	}


	// Application main loop
	bool recreateSwapchain = false;

	//timing
	auto previousClock = Clock_::now();

	while (!glfwWindowShouldClose(window.window))
	{
		// Let GLFW process events.
		// glfwPollEvents() checks for events, processes them. If there are no
		// events, it will return immediately. Alternatively, glfwWaitEvents()
		// will wait for any event to occur, process it, and only return at
		// that point. The former is useful for applications where you want to
		// render as fast as possible, whereas the latter is useful for
		// input-driven applications, where redrawing is only needed in
		// reaction to user input (or similar).
		glfwPollEvents(); // or: glfwWaitEvents()

		// Recreate swap chain?
		if (recreateSwapchain)
		{
			//TODO: (Section 1) re-create swapchain and associated resources - see Exercise 3!

			// We need to destroy several objects, which may still be in use by
			// the GPU. Therefore, first wait for the GPU to finish processing
			vkDeviceWaitIdle(window.device);

			//Recreate them
			auto const changes = recreate_swapchain(window);

			if (changes.changedFormat)
				renderPass = create_render_pass(window);

			if (changes.changedSize)
				std::tie(depthBuffer, depthBufferView) = create_depth_buffer(window, allocator);

			framebuffers.clear();
			create_swapchain_framebuffers(window, renderPass.handle, framebuffers, depthBufferView.handle);

			if (changes.changedSize)
			{
				pipe = create_pipeline(window, renderPass.handle, pipeLayout.handle);
				alphaPipe = create_alpha_pipeline(window, renderPass.handle, pipeLayout.handle);
			}
			recreateSwapchain = false;
			continue;
		}

		//acquire swapchain image.
		//wait for command buffer to be available
		//record and submit commands
		//present rendered images (note: use the present_results() method)
		std::uint32_t imageIndex = 0;
		auto const acquireRes = vkAcquireNextImageKHR(window.device,
			window.swapchain, std::numeric_limits<std::uint64_t>::max(),
			imageAvailable.handle, VK_NULL_HANDLE, &imageIndex);

		if (VK_SUBOPTIMAL_KHR == acquireRes || VK_ERROR_OUT_OF_DATE_KHR == acquireRes)
		{
			//This occurs e.g., when the window has been resized. In this case we needs to recreate
			//the swap chain match the new dimensions. Any resources that directly depend on the swap chain
			//need to be recreated as well. While rare, re_creating the swap chain may give us a different 
			//image format, which we should handle.
			//In both cases, we set the falg that the swap chain has to be re-created and jump to the top of 
			// the loop. Technically, with the VK_SUBOPTIMAL_KHR return code, we could continue rendering with the current
			// swapchain (unlike VK_ERROR_OUT_OF_DATA_KHR, which does require us to recreate the swap chain).
			recreateSwapchain = true;
			continue;
		}

		if (VK_SUCCESS != acquireRes)
		{
			throw lut::Error("Unable to acquire next swapchain image\n" "vkAcquireNextImageKHR() returned %s", lut::to_string(acquireRes).c_str());
		}

		//Make sure that the command buffer is no loger in use
		assert(std::size_t(imageIndex) < cbfences.size());

		if (auto const res = vkWaitForFences(window.device, 1, &cbfences[imageIndex].handle, VK_TRUE, std::numeric_limits<std::uint64_t>::max()); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to wait for command buffer fence %u\n" "vkWaitForFences() returned %s", imageIndex, lut::to_string(res).c_str());
		}

		if (auto const res = vkResetFences(window.device, 1, &cbfences[imageIndex].handle); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to reset command buffer fence %u\n" "vkResetFences() returned %s", imageIndex, lut::to_string(res).c_str());
		}

		//Update state
		auto const now = Clock_::now();
		auto const dt = std::chrono::duration_cast<Secondsf_>(now - previousClock).count();
		previousClock = now;

		update_user_state(state, dt);

		//prepare data for this frame(section 3)
		glsl::SceneUniform sceneUniforms{};
		update_scene_uniforms(sceneUniforms, window.swapchainExtent.width, window.swapchainExtent.height, state);

		assert(std::size_t(imageIndex) < cbuffers.size());
		assert(std::size_t(imageIndex) < framebuffers.size());

		record_commands(cbuffers[imageIndex], renderPass.handle, framebuffers[imageIndex].handle, pipe.handle,
			window.swapchainExtent, sceneUBO.buffer, sceneUniforms, pipeLayout.handle, sceneDescriptors, ourModel, alphaPipe.handle, bakedModel);

		submit_commands(window, cbuffers[imageIndex], cbfences[imageIndex].handle, imageAvailable.handle, renderFinished.handle);

		present_results(window.presentQueue, window.swapchain, imageIndex, renderFinished.handle, recreateSwapchain);


	}

	// Cleanup takes place automatically in the destructors, but we sill need
	// to ensure that all Vulkan commands have finished before that.
	vkDeviceWaitIdle(window.device);

	return 0;
}
catch (std::exception const& eErr)
{
	std::fprintf(stderr, "\n");
	std::fprintf(stderr, "Error: %s\n", eErr.what());
	return 1;
}

namespace
{
	void glfw_callback_key_press(GLFWwindow* aWindow, int aKey, int /*aScanCode*/, int aAction, int /*aModifierFlags*/)
	{
		if (GLFW_KEY_ESCAPE == aKey && GLFW_PRESS == aAction)
		{
			glfwSetWindowShouldClose(aWindow, GLFW_TRUE);
		}

		auto state = static_cast<UserState*>(glfwGetWindowUserPointer(aWindow));
		assert(state);

		bool const isReleased = (GLFW_RELEASE == aAction);

		switch (aKey)
		{
		case GLFW_KEY_W:
			state->inputMap[std::size_t(EInputState::forward)] = !isReleased;
			break;
		case GLFW_KEY_S:
			state->inputMap[std::size_t(EInputState::backward)] = !isReleased;
			break;
		case GLFW_KEY_A:
			state->inputMap[std::size_t(EInputState::strafeLeft)] = !isReleased;
			break;
		case GLFW_KEY_D:
			state->inputMap[std::size_t(EInputState::strafeRight)] = !isReleased;
			break;
		case GLFW_KEY_E:
			state->inputMap[std::size_t(EInputState::levitate)] = !isReleased;
			break;
		case GLFW_KEY_Q:
			state->inputMap[std::size_t(EInputState::sink)] = !isReleased;
			break;

		case GLFW_KEY_LEFT_SHIFT: [[fallthrough]];
		case GLFW_KEY_RIGHT_SHIFT:
			state->inputMap[std::size_t(EInputState::fast)] = !isReleased;
			break;

		case GLFW_KEY_LEFT_CONTROL: [[fallthrough]];
		case GLFW_KEY_RIGHT_CONTROL:
			state->inputMap[std::size_t(EInputState::slow)] = !isReleased;
			break;

		case GLFW_KEY_SPACE:
			if (aAction == GLFW_PRESS) 
			{
				state->inputMap[std::size_t(EInputState::lightRotate)] = !state->inputMap[std::size_t(EInputState::lightRotate)]; 
			}
			break;

		default:
			;
		}

	}

	void glfw_callback_button(GLFWwindow* aWin, int aBut, int aAct, int)
	{
		auto state = static_cast<UserState*>(glfwGetWindowUserPointer(aWin));
		assert(state);

		if (GLFW_MOUSE_BUTTON_RIGHT == aBut && GLFW_PRESS == aAct)
		{
			auto& flag = state->inputMap[std::size_t(EInputState::mousing)];

			flag = !flag;
			if (flag)
				glfwSetInputMode(aWin, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			else
				glfwSetInputMode(aWin, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}

	void glfw_callback_motion(GLFWwindow* aWin, double aX, double aY)
	{
		auto state = static_cast<UserState*>(glfwGetWindowUserPointer(aWin));
		assert(state);

		state->mouseX = float(aX);
		state->mouseY = float(aY);
	}
}

namespace
{
	void update_scene_uniforms(glsl::SceneUniform& aSceneUniforms, std::uint32_t aFramebufferWidth, std::uint32_t aFramebufferHeight, UserState aState)
	{
		//TODO- (Section 3) initialize SceneUniform members
		float const aspect = aFramebufferWidth / float(aFramebufferHeight);

		aSceneUniforms.projection = glm::perspectiveRH_ZO(lut::Radians(cfg::kCameraFov).value(),
			aspect, cfg::kCameraNear, cfg::kCameraFar);
		aSceneUniforms.projection[1][1] *= -1.f;// mirror Y axis

		aSceneUniforms.camera = glm::inverse(aState.camera2world);

		aSceneUniforms.projCam = aSceneUniforms.projection * aSceneUniforms.camera;
		aSceneUniforms.cameraPos = glm::vec3(aState.camera2world[3]);
		aSceneUniforms.lightColor = glm::vec3(1,1,1);
		aSceneUniforms.lightPos = aState.light_pos;
	}
}

namespace
{
	void update_user_state(UserState& aState, float aElapsedTime)
	{
		auto& cam = aState.camera2world;

		if (aState.inputMap[std::size_t(EInputState::mousing)])
		{
			// Only update the rotation on the second frame of mouse
			// navigation. This ensures that the previousX and Y variables are
			// initialized to sensible values.
			if (aState.wasMousing)
			{
				auto const sens = cfg::kCameraMouseSensitivity;
				auto const dx = sens * (aState.mouseX - aState.previousX);
				auto const dy = sens * (aState.mouseY - aState.previousY);

				cam *= glm::rotate(-dy, glm::vec3(1.f, 0.f, 0.f));
				cam *= glm::rotate(-dx, glm::vec3(0.f, 1.f, 0.f));
			}

			aState.previousX = aState.mouseX;
			aState.previousY = aState.mouseY;
			aState.wasMousing = true;
		}
		else
		{
			aState.wasMousing = false;
		}

		if (aState.inputMap[std::size_t(EInputState::lightRotate)])
		{
			glm::mat4 rotationMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f) * aElapsedTime, glm::vec3(0.0f, 1.0f, 0.0f));

			glm::vec3 center(0.0f, 0.0f, -2.0f); 
			aState.light_pos = glm::vec3(rotationMatrix * glm::vec4(aState.light_pos - center, 1.0f)) + center;
		}

		auto const move = aElapsedTime * cfg::kCameraBaseSpeed *
			(aState.inputMap[std::size_t(EInputState::fast)] ? cfg::kCameraFastMult : 1.f) *
			(aState.inputMap[std::size_t(EInputState::slow)] ? cfg::kCameraSlowMult : 1.f);

		if (aState.inputMap[std::size_t(EInputState::forward)])
			cam = cam * glm::translate(glm::vec3(0.f, 0.f, -move));
		if (aState.inputMap[std::size_t(EInputState::backward)])
			cam = cam * glm::translate(glm::vec3(0.f, 0.f, +move));

		if (aState.inputMap[std::size_t(EInputState::strafeLeft)])
			cam = cam * glm::translate(glm::vec3(-move, 0.f, 0.f));
		if (aState.inputMap[std::size_t(EInputState::strafeRight)])
			cam = cam * glm::translate(glm::vec3(+move, 0.f, 0.f));

		if (aState.inputMap[std::size_t(EInputState::levitate)])
			cam = cam * glm::translate(glm::vec3(0.f, +move, 0.f));
		if (aState.inputMap[std::size_t(EInputState::sink)])
			cam = cam * glm::translate(glm::vec3(0.f, -move, 0.f));

	}

	lut::RenderPass create_render_pass(lut::VulkanWindow const& aWindow)
	{
		//TODO- (Section 1 / Exercise 3) implement me!
		VkAttachmentDescription attachments[2]{};
		attachments[0].format = aWindow.swapchainFormat;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		attachments[1].format = cfg::kDepthFormat;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference subpassAttachments[1]{};
		subpassAttachments[0].attachment = 0; //this refers to attachment[0]
		subpassAttachments[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthAttachment{};
		depthAttachment.attachment = 1; //this refers to attachments[1]
		depthAttachment.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpasses[1]{};
		subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpasses[0].colorAttachmentCount = 1;
		subpasses[0].pColorAttachments = subpassAttachments;
		subpasses[0].pDepthStencilAttachment = &depthAttachment;

		//changed: no explicit subpass dependencies
		VkRenderPassCreateInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		passInfo.attachmentCount = 2;
		passInfo.pAttachments = attachments;
		passInfo.subpassCount = 1;
		passInfo.pSubpasses = subpasses;
		passInfo.dependencyCount = 0;  //changed
		passInfo.pDependencies = nullptr;  //changed

		VkRenderPass rpass = VK_NULL_HANDLE;
		if (auto const res = vkCreateRenderPass(aWindow.device, &passInfo, nullptr, &rpass); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create render pass\n" "vkCreateRenderPass() returned %s", lut::to_string(res).c_str());
		}
		return lut::RenderPass(aWindow.device, rpass);
	}

	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout aSceneLayout, VkDescriptorSetLayout aObjectLayout)
	{
		//TODO- (Section 1 / Exercise 3) implement me!
		VkDescriptorSetLayout layouts[] =
		{  //Order must match the set = N in the shaders
			aSceneLayout ,//set 0
			aObjectLayout //set1
		};

		//TODO: implement me!
		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = sizeof(layouts) / sizeof(layouts[0]);
		layoutInfo.pSetLayouts = layouts;
		layoutInfo.pushConstantRangeCount = 0;
		layoutInfo.pPushConstantRanges = nullptr;

		VkPipelineLayout layout = VK_NULL_HANDLE;
		if (auto const res = vkCreatePipelineLayout(aContext.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create pipeline layout\n""vkCreatePipelineLayout() returned %s", lut::to_string(res).c_str());
		}

		return lut::PipelineLayout(aContext.device, layout);
	}


	lut::Pipeline create_pipeline(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout)
	{
		//TODO: implement me!
		lut::ShaderModule vert = lut::load_shader_module(aWindow, cfg::kVertShaderPath);
		lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::kFragShaderPath);

		//Define shader stages in the pipeline
		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vert.handle;
		stages[0].pName = "main";

		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = frag.handle;
		stages[1].pName = "main";

		//define depth and stencil state
		VkPipelineDepthStencilStateCreateInfo depthInfo{};
		depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthInfo.depthTestEnable = VK_TRUE;
		depthInfo.depthWriteEnable = VK_TRUE;
		depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthInfo.minDepthBounds = 0.f;
		depthInfo.maxDepthBounds = 1.f;


		VkVertexInputBindingDescription vertexInputs[1]{};
		//textured meshes
		vertexInputs[0].binding = 0;
		vertexInputs[0].stride = sizeof(float) * 12;
		vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription vertexAttribs[4]{};

		//positions
		vertexAttribs[0].binding = 0; // must match binding above
		vertexAttribs[0].location = 0; // must match shader
		vertexAttribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertexAttribs[0].offset = 0;

		////texcoords
		vertexAttribs[1].binding = 0;
		vertexAttribs[1].location = 1;
		vertexAttribs[1].format = VK_FORMAT_R32G32_SFLOAT;
		vertexAttribs[1].offset = sizeof(float) * 3;

		//normals
		vertexAttribs[2].binding = 0;
		vertexAttribs[2].location = 2;
		vertexAttribs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertexAttribs[2].offset = sizeof(float) * 5;

		//tangent
		vertexAttribs[3].binding = 0;
		vertexAttribs[3].location = 3;
		vertexAttribs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		vertexAttribs[3].offset = sizeof(float) * 8;

		VkPipelineVertexInputStateCreateInfo inputInfo{};
		inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		inputInfo.vertexBindingDescriptionCount = 1;
		inputInfo.pVertexBindingDescriptions = vertexInputs;
		inputInfo.vertexAttributeDescriptionCount = 4;
		inputInfo.pVertexAttributeDescriptions = vertexAttribs;

		// Define which primitive (point, line, triangle, ...) the input is
		// assembled into for rasterization. 
		VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
		assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		assemblyInfo.primitiveRestartEnable = VK_FALSE;

		//Define viewport and scissor regions
		VkViewport viewport{};
		viewport.x = 0.f;
		viewport.y = 0.f;
		viewport.width = static_cast<float>(aWindow.swapchainExtent.width);
		viewport.height = static_cast<float>(aWindow.swapchainExtent.height);
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;

		VkRect2D scissor{};
		scissor.offset = VkOffset2D{ 0,0 };
		scissor.extent = aWindow.swapchainExtent;

		VkPipelineViewportStateCreateInfo viewportInfo{};
		viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportInfo.viewportCount = 1;
		viewportInfo.pViewports = &viewport;
		viewportInfo.scissorCount = 1;
		viewportInfo.pScissors = &scissor;

		//Define rasterization options
		VkPipelineRasterizationStateCreateInfo rasterInfo{};
		rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterInfo.depthClampEnable = VK_FALSE;
		rasterInfo.rasterizerDiscardEnable = VK_FALSE;
		rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
		rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterInfo.depthBiasEnable = VK_FALSE;
		rasterInfo.lineWidth = 1.f;

		//Define multisampling state
		VkPipelineMultisampleStateCreateInfo samplingInfo{};
		samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Define blend state
		// We define one blend state per color attachment - this example uses a
		// single color attachment, so we only need one. Right now, we don¡¯t do any
		// blending, so we can ignore most of the members.
		VkPipelineColorBlendAttachmentState blendStates[1]{};
		blendStates[0].blendEnable = VK_FALSE;
		blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo blendInfo{};
		blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blendInfo.logicOpEnable = VK_FALSE;
		blendInfo.attachmentCount = 1;
		blendInfo.pAttachments = blendStates;

		//Create pipeline
		VkGraphicsPipelineCreateInfo pipeInfo{};
		pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeInfo.stageCount = 2; // vert + frag stages
		pipeInfo.pStages = stages;
		pipeInfo.pVertexInputState = &inputInfo;
		pipeInfo.pInputAssemblyState = &assemblyInfo;
		pipeInfo.pTessellationState = nullptr;  // no tesselation
		pipeInfo.pViewportState = &viewportInfo;
		pipeInfo.pRasterizationState = &rasterInfo;
		pipeInfo.pMultisampleState = &samplingInfo;
		pipeInfo.pDepthStencilState = &depthInfo;
		pipeInfo.pColorBlendState = &blendInfo;
		pipeInfo.pDynamicState = nullptr;   // no dynamic states

		pipeInfo.layout = aPipelineLayout;
		pipeInfo.renderPass = aRenderPass;
		pipeInfo.subpass = 0;  // first subpass of aRenderPass

		VkPipeline pipe = VK_NULL_HANDLE;
		if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create graphics pipeline\n" "vkCreateGraphicsPipelines() returned %s", lut::to_string(res).c_str());
		}

		return lut::Pipeline(aWindow.device, pipe);
	}

	lut::Pipeline create_alpha_pipeline(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout)
	{
		lut::ShaderModule vert = lut::load_shader_module(aWindow, cfg::kVertShaderPath);
		lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::kFragShaderPath);

		//Define shader stages in the pipeline
		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vert.handle;
		stages[0].pName = "main";

		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = frag.handle;
		stages[1].pName = "main";

		
		//We are reading from two separate buffers, therefore use two VkVertexInputBindingDescription instances :
		VkVertexInputBindingDescription vertexInputs[1]{};
		vertexInputs[0].binding = 0;
		vertexInputs[0].stride = sizeof(float) * 12;
		vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;


		VkVertexInputAttributeDescription vertexAttribs[4]{};

		//positions
		vertexAttribs[0].binding = 0; // must match binding above
		vertexAttribs[0].location = 0; // must match shader
		vertexAttribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertexAttribs[0].offset = 0;

		////texcoords
		vertexAttribs[1].binding = 0;
		vertexAttribs[1].location = 1;
		vertexAttribs[1].format = VK_FORMAT_R32G32_SFLOAT;
		vertexAttribs[1].offset = sizeof(float) * 3;

		//normals
		vertexAttribs[2].binding = 0;
		vertexAttribs[2].location = 2;
		vertexAttribs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertexAttribs[2].offset = sizeof(float) * 5;

		//tangent
		vertexAttribs[3].binding = 0;
		vertexAttribs[3].location = 3;
		vertexAttribs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		vertexAttribs[3].offset = sizeof(float) * 8;

		//define depth and stencil state
		VkPipelineDepthStencilStateCreateInfo depthInfo{};
		depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthInfo.depthTestEnable = VK_TRUE;
		depthInfo.depthWriteEnable = VK_TRUE;
		depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthInfo.minDepthBounds = 0.f;
		depthInfo.maxDepthBounds = 1.f;


		VkPipelineVertexInputStateCreateInfo inputInfo{};
		inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		inputInfo.vertexBindingDescriptionCount = 1;
		inputInfo.pVertexBindingDescriptions = vertexInputs;
		inputInfo.vertexAttributeDescriptionCount = 4;
		inputInfo.pVertexAttributeDescriptions = vertexAttribs;


		// Define which primitive (point, line, triangle, ...) the input is
		// assembled into for rasterization.
		VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
		assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		assemblyInfo.primitiveRestartEnable = VK_FALSE;

		//Define viewport and scissor regions
		VkViewport viewport{};
		viewport.x = 0.f;
		viewport.y = 0.f;
		viewport.width = aWindow.swapchainExtent.width;
		viewport.height = aWindow.swapchainExtent.height;
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;

		VkRect2D scissor{};
		scissor.offset = VkOffset2D{ 0,0 };
		scissor.extent = aWindow.swapchainExtent;

		VkPipelineViewportStateCreateInfo viewportInfo{};
		viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportInfo.viewportCount = 1;
		viewportInfo.pViewports = &viewport;
		viewportInfo.scissorCount = 1;
		viewportInfo.pScissors = &scissor;

		//Define rasterization options
		VkPipelineRasterizationStateCreateInfo rasterInfo{};
		rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterInfo.depthClampEnable = VK_FALSE;
		rasterInfo.rasterizerDiscardEnable = VK_FALSE;
		rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
		rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterInfo.depthBiasEnable = VK_FALSE;
		rasterInfo.lineWidth = 1.f;

		//Define multisampling state
		VkPipelineMultisampleStateCreateInfo samplingInfo{};
		samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;


		VkPipelineColorBlendAttachmentState blendStates[1]{};
		blendStates[0].blendEnable = VK_FALSE;
		//blendStates[0].colorBlendOp = VK_BLEND_OP_ADD;
		//blendStates[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		//blendStates[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo blendInfo{};
		blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blendInfo.logicOpEnable = VK_FALSE;
		blendInfo.attachmentCount = 1;
		blendInfo.pAttachments = blendStates;

		//Create pipeline
		VkGraphicsPipelineCreateInfo pipeInfo{};
		pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

		pipeInfo.stageCount = 2; // vert + frag
		pipeInfo.pStages = stages;

		pipeInfo.pVertexInputState = &inputInfo;
		pipeInfo.pInputAssemblyState = &assemblyInfo;
		pipeInfo.pTessellationState = nullptr;  // no tesselation
		pipeInfo.pViewportState = &viewportInfo;
		pipeInfo.pRasterizationState = &rasterInfo;
		pipeInfo.pMultisampleState = &samplingInfo;
		pipeInfo.pDepthStencilState = &depthInfo;
		pipeInfo.pColorBlendState = &blendInfo;
		pipeInfo.pDynamicState = nullptr;   // no dynamic states

		pipeInfo.layout = aPipelineLayout;
		pipeInfo.renderPass = aRenderPass;
		pipeInfo.subpass = 0;  // first subpass of aRenderPass

		VkPipeline pipe = VK_NULL_HANDLE;
		if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create graphics pipeline\n" "vkCreateGraphicsPipelines() returned %s", lut::to_string(res).c_str());
		}

		return lut::Pipeline(aWindow.device, pipe);
	}


	std::tuple<lut::Image, lut::ImageView> create_depth_buffer(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator)
	{
		VkImageCreateInfo imgInfo{};
		imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imgInfo.imageType = VK_IMAGE_TYPE_2D;
		imgInfo.format = cfg::kDepthFormat;
		imgInfo.extent.width = aWindow.swapchainExtent.width;
		imgInfo.extent.height = aWindow.swapchainExtent.height;
		imgInfo.extent.depth = 1;
		imgInfo.mipLevels = 1;
		imgInfo.arrayLayers = 1;
		imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;

		if (auto const res = vmaCreateImage(aAllocator.allocator, &imgInfo, &allocInfo, &image, &allocation, nullptr); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to allocate depth buffer image.\n" "vmaCreateImage() returned %s", lut::to_string(res).c_str());
		}

		lut::Image depthImage(aAllocator.allocator, image, allocation);

		//create the image view
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = depthImage.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = cfg::kDepthFormat;
		viewInfo.components = VkComponentMapping{}; //identity
		viewInfo.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

		VkImageView view = VK_NULL_HANDLE;
		if (auto const res = vkCreateImageView(aWindow.device, &viewInfo, nullptr, &view); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create image view\n" "vkCreateImageView() returned %s", lut::to_string(res).c_str());
		}

		return { std::move(depthImage), lut::ImageView(aWindow.device, view) };
	}

	void create_swapchain_framebuffers(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, std::vector<lut::Framebuffer>& aFramebuffers, VkImageView aDepthView)
	{
		assert(aFramebuffers.empty());

		//TODO- (Section 1/Exercise 3) implement me!
		for (std::size_t i = 0; i < aWindow.swapViews.size(); ++i)
		{
			VkImageView attachments[2] = { aWindow.swapViews[i], aDepthView };

			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.flags = 0;
			fbInfo.renderPass = aRenderPass;
			fbInfo.attachmentCount = 2;
			fbInfo.pAttachments = attachments;
			fbInfo.width = aWindow.swapchainExtent.width;
			fbInfo.height = aWindow.swapchainExtent.height;
			fbInfo.layers = 1;
			VkFramebuffer fb = VK_NULL_HANDLE;
			if (auto const res = vkCreateFramebuffer(aWindow.device, &fbInfo, nullptr, &fb); VK_SUCCESS != res)
			{
				throw lut::Error("Unable to create framebuffer for swap chain image %zu\n" "vkCreateFramebuffer returned %s", i, lut::to_string(res).c_str());
			}

			aFramebuffers.emplace_back(lut::Framebuffer(aWindow.device, fb));
		}
		assert(aWindow.swapViews.size() == aFramebuffers.size());
	}

	lut::DescriptorSetLayout create_scene_descriptor_layout(lut::VulkanWindow const& aWindow)
	{
		VkDescriptorSetLayoutBinding bindings[1]{};
		bindings[0].binding = 0; // number must match the index of the corresponding binding = N declaration in the shader(s)

		bindings[0].descriptorCount = 1;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = sizeof(bindings) / sizeof(bindings[0]);
		layoutInfo.pBindings = bindings;

		VkDescriptorSetLayout layout = VK_NULL_HANDLE;
		if (auto const res = vkCreateDescriptorSetLayout(aWindow.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create decriptor set layout\n" "vkCreateDescriptorSetLayout() returned %s", lut::to_string(res).c_str());
		}
		return lut::DescriptorSetLayout(aWindow.device, layout);

	}

	void record_commands(VkCommandBuffer aCmdBuff, VkRenderPass aRenderPass, VkFramebuffer aFramebuffer,
		VkPipeline aGraphicsPipe, VkExtent2D const& aImageExtent, VkBuffer aSceneUBO, glsl::SceneUniform const& aSceneUniform,
		VkPipelineLayout aGraphicsLayout, VkDescriptorSet aSceneDescriptors, ModelPack& aModel, VkPipeline aSecondGraphicsPipe, BakedModel& bakedModel)
	{
		//Begin recording commands
		VkCommandBufferBeginInfo begInfo{};
		begInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		begInfo.pInheritanceInfo = nullptr;

		if (auto const res = vkBeginCommandBuffer(aCmdBuff, &begInfo); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to begin recording command buffer\n" "vkBeginCommandBuffer() returned %s", lut::to_string(res).c_str());
		}

		//Upload scene uniforms
		lut::buffer_barrier(aCmdBuff, aSceneUBO, VK_ACCESS_UNIFORM_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

		vkCmdUpdateBuffer(aCmdBuff, aSceneUBO, 0, sizeof(glsl::SceneUniform), &aSceneUniform);

		lut::buffer_barrier(aCmdBuff, aSceneUBO, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_UNIFORM_READ_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);

		//Begin render pass
		VkClearValue clearValues[2]{};
		clearValues[0].color.float32[0] = 0.1f;
		clearValues[0].color.float32[1] = 0.1f;
		clearValues[0].color.float32[2] = 0.1f;
		clearValues[0].color.float32[3] = 1.f;

		clearValues[1].depthStencil.depth = 1.f;

		VkRenderPassBeginInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		passInfo.renderPass = aRenderPass;
		passInfo.framebuffer = aFramebuffer;
		passInfo.renderArea.offset = VkOffset2D{ 0, 0 };
		passInfo.renderArea.extent = aImageExtent;
		passInfo.clearValueCount = 2;
		passInfo.pClearValues = clearValues;

		vkCmdBeginRenderPass(aCmdBuff, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

		//Begin drawing with our graphics pipeline
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);
		for (auto& mesh : aModel.meshes)
		{
			if (bakedModel.materials[mesh.matID].alphaMaskTextureId == 0xffffffff)
			{
				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aModel.matDecriptors[mesh.matID], 0, nullptr);
				VkDeviceSize offsets[1]{};
				vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &mesh.vertices.buffer, offsets);
				vkCmdBindIndexBuffer(aCmdBuff, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(aCmdBuff, mesh.indexCount, 1, 0, 0, 0);
			}
		}
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSecondGraphicsPipe);
		for (auto& mesh : aModel.meshes)
		{
			if (bakedModel.materials[mesh.matID].alphaMaskTextureId != 0xffffffff)
			{
				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aModel.matDecriptors[mesh.matID], 0, nullptr); 
				VkDeviceSize offsets[1]{}; 
				vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &mesh.vertices.buffer, offsets); 
				vkCmdBindIndexBuffer(aCmdBuff, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32); 
				vkCmdDrawIndexed(aCmdBuff, mesh.indexCount, 1, 0, 0, 0); 
			}
		}
		vkCmdEndRenderPass(aCmdBuff);

		//End command recording
		if (auto const res = vkEndCommandBuffer(aCmdBuff); VK_SUCCESS != res)
			throw lut::Error("Unable to end recording command buffer\n" "vkEndCoomandBuffer{} returned %s", lut::to_string(res).c_str());
	}

	void submit_commands(lut::VulkanWindow const& aWindow, VkCommandBuffer aCmdBuff, VkFence aFence, VkSemaphore aWaitSemaphore, VkSemaphore aSignalSemaphore)
	{
		//TODO: (Section 1/Exercise 3) implement me!
		VkPipelineStageFlags waitPipelineStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &aCmdBuff;

		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &aWaitSemaphore;
		submitInfo.pWaitDstStageMask = &waitPipelineStages;

		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &aSignalSemaphore;

		if (auto const res = vkQueueSubmit(aWindow.graphicsQueue, 1, &submitInfo, aFence); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to submit command buffer to queue\n" "vkQueueSubmit() returned %s", lut::to_string(res).c_str());
		}
	}

	void present_results(VkQueue aPresentQueue, VkSwapchainKHR aSwapchain, std::uint32_t aImageIndex, VkSemaphore aRenderFinished, bool& aNeedToRecreateSwapchain)
	{
		//TODO: (Section 1/Exercise 3) implement me!
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &aRenderFinished;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &aSwapchain;
		presentInfo.pImageIndices = &aImageIndex;
		presentInfo.pResults = nullptr;
		auto const presentRes = vkQueuePresentKHR(aPresentQueue, &presentInfo);
		if (VK_SUBOPTIMAL_KHR == presentRes || VK_ERROR_OUT_OF_DATE_KHR == presentRes)
		{
			aNeedToRecreateSwapchain = true;
		}
		else if (VK_SUCCESS != presentRes)
		{
			throw lut::Error("Unable present swapchain image %u\n"
				"vkQueuePresentKHR() returned %s", aImageIndex, lut::to_string(presentRes).
				c_str());
		}
	}


	lut::DescriptorSetLayout create_material_descriptor_layout(lut::VulkanWindow const& aWindow)
	{
		VkDescriptorSetLayoutBinding bindings[4]{};

		// basecolor
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		// roughness
		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		// metalness
		bindings[2].binding = 2;
		bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[2].descriptorCount = 1;
		bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		//normal
		bindings[3].binding = 3;
		bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[3].descriptorCount = 1;
		bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutCreateInfo{};
		layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutCreateInfo.bindingCount = sizeof(bindings) / sizeof(bindings[0]);
		layoutCreateInfo.pBindings = bindings;

		VkDescriptorSetLayout layout = VK_NULL_HANDLE;

		if (auto const res = vkCreateDescriptorSetLayout(aWindow.device, &layoutCreateInfo, nullptr, &layout); VK_SUCCESS != res) {
			throw lut::Error("Unable to create decriptor set layout\n" "vkCreateDescriptorSetLayout() returned %s", lut::to_string(res).c_str());
		}

		return lut::DescriptorSetLayout(aWindow.device, layout);
	}
}




//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
