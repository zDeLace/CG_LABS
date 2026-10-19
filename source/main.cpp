#include <cstdint>
#include <climits>

#include <iostream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>

#include "graphics_internal.hpp"
#include "application.hpp"
#include <fstream>

namespace {

constexpr int32_t default_window_width = 1280;
constexpr int32_t default_window_height = 720;

constexpr char default_window_title[] = "Vulkan Starter App";

GLFWwindow* glfw_window;

} // namespace

int main() {
	std::ofstream log("startup.log");
log << "[1] main started" << std::endl;
	std::cerr << "[1] main started\n";
	int status = EXIT_SUCCESS;

	if (!glfwInit()) {
		std::cerr << "[2] GLFW initialized\n";
		log << "[2] GLFW initialized" << std::endl;
		std::cerr << "Failed to initialize GLFW\n";
		return EXIT_FAILURE;
	}
	log << "[2] GLFW initialized" << std::endl;

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	glfw_window = glfwCreateWindow(default_window_width, default_window_height,
	                               default_window_title, nullptr, nullptr);
	if (glfw_window == nullptr) {
		log << "[3] Window created" << std::endl;
		status = EXIT_FAILURE;
		goto err_null_window;
	}
	glfwSetMouseButtonCallback(glfw_window, application::mouseButtonCallback);
glfwSetCursorPosCallback(glfw_window, application::cursorPositionCallback);
	log << "[3] Window created" << std::endl;
	std::cerr << "[3] Window created\n";

	glfwSetFramebufferSizeCallback(glfw_window, [](GLFWwindow*, int width, int height){
		if (width == 0 || height == 0) {
			return;
		}

		graphics::internal::resize(width, height);
	});

	if (ImGui::CreateContext() == nullptr) {
		std::cerr << "Failed to create ImGUI context\n";
		status = EXIT_FAILURE;
		goto err_imgui_init;
	}

	if (!ImGui_ImplGlfw_InitForVulkan(glfw_window, true)) {
		std::cerr << "Failed to initialize ImGUI GLFW backend for Vulkan renderer\n";
		status = EXIT_FAILURE;
		goto err_imgui_glfw_init;
	}

	if (!graphics::internal::initialize(glfw_window)) {
		std::cerr << "Failed to initialize graphics\n";
		log << "[4] Graphics initialized" << std::endl;
		status = EXIT_FAILURE;
		goto err_graphics_init;
	}
	std::cerr << "[4] Graphics initialized\n";
	log << "[4] Graphics initialized" << std::endl;
	log << "[5a] Before application::initialize()" << std::endl;

if (!application::initialize()) {
    log << "[ERROR] application::initialize() returned false" << std::endl;
    status = EXIT_FAILURE;
    goto err_application_init;
}

	log << "[5b] application::initialize() returned true" << std::endl;
	log << "[6] Entering render loop" << std::endl;	
	log << "[5] Application initialized" << std::endl;
	log << "[6] Entering render loop" << std::endl;

	while (!glfwWindowShouldClose(glfw_window)) {
		const double time = glfwGetTime();

		glfwPollEvents();
		ImGui_ImplGlfw_NewFrame();

		ImGui::NewFrame();
		application::update(time);
		ImGui::Render();

		graphics::internal::FrameData fd = graphics::internal::prepare();
		application::render(fd);
		graphics::internal::submitAndPresent();
	}

	application::shutdown();
err_application_init:
	graphics::internal::shutdown();
err_graphics_init:
	ImGui_ImplGlfw_Shutdown();
err_imgui_glfw_init:
	ImGui::DestroyContext();
err_imgui_init:
	glfwDestroyWindow(glfw_window);
err_null_window:
	glfwTerminate();

	return status;
}