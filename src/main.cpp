/**
 * @file main.cpp
 * @brief Runtime shell integrating SDL3, OpenGL, Dear ImGui, and OpenImageIO.
 */

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <iostream>
#include <memory>
#include <string>

#include "Viewer.h"

std::string OpenFileDialog() {
#if defined(_WIN32)
  char buffer[1024] = {0};
  FILE *f = popen(
      "powershell -Command \"Add-Type -AssemblyName System.Windows.Forms; $f = "
      "New-Object System.Windows.Forms.OpenFileDialog; $f.Filter = 'Images "
      "(*.png;*.jpg;*.exr;*.hdr)|*.png;*.jpg;*.jpeg;*.exr;*.hdr'; "
      "if($f.ShowDialog() -eq 'OK') { $f.FileName }\"",
      "r");
  if (f) {
    if (fgets(buffer, sizeof(buffer), f) != nullptr) {
      std::string path = buffer;
      if (!path.empty() && path.back() == '\n')
        path.pop_back();
      if (!path.empty() && path.back() == '\r')
        path.pop_back();
      pclose(f);
      return path;
    }
    pclose(f);
  }
#elif defined(__linux__)
  char buffer[1024] = {0};
  FILE *f = popen("zenity --file-selection --title='Select Image Asset' "
                  "--file-filter='Images | *.png *.jpg *.jpeg *.exr *.hdr "
                  "*.tga' 2>/dev/null",
                  "r");
  if (f) {
    if (fgets(buffer, sizeof(buffer), f) != nullptr) {
      std::string path = buffer;
      if (!path.empty() && path.back() == '\n')
        path.pop_back();
      pclose(f);
      return path;
    }
    pclose(f);
  }
#elif defined(__APPLE__)
  char buffer[1024] = {0};
  FILE *f = popen("osascript -e 'POSIX path of (choose file of type "
                  "{\"public.image\"} with prompt \"Select Image Asset\")'",
                  "r");
  if (f) {
    if (fgets(buffer, sizeof(buffer), f) != nullptr) {
      std::string path = buffer;
      if (!path.empty() && path.back() == '\n')
        path.pop_back();
      pclose(f);
      return path;
    }
    pclose(f);
  }
#endif
  return "";
}

std::string SaveFileDialog() {
#if defined(_WIN32)
  char buffer[1024] = {0};
  FILE *f = popen(
      "powershell -Command \"Add-Type -AssemblyName System.Windows.Forms; $f = "
      "New-Object System.Windows.Forms.SaveFileDialog; $f.Filter = 'Images "
      "(*.png;*.jpg;*.exr;*.hdr)|*.png;*.jpg;*.jpeg;*.exr;*.hdr'; "
      "if($f.ShowDialog() -eq 'OK') { $f.FileName }\"",
      "r");
  if (f) {
    if (fgets(buffer, sizeof(buffer), f) != nullptr) {
      std::string path = buffer;
      if (!path.empty() && path.back() == '\n') path.pop_back();
      if (!path.empty() && path.back() == '\r') path.pop_back();
      pclose(f);
      return path;
    }
    pclose(f);
  }
#elif defined(__linux__)
  char buffer[1024] = {0};
  FILE *f = popen("zenity --file-selection --save --title='Save Image Asset' "
                  "--file-filter='Images | *.png *.jpg *.jpeg *.exr *.hdr "
                  "*.tga' 2>/dev/null",
                  "r");
  if (f) {
    if (fgets(buffer, sizeof(buffer), f) != nullptr) {
      std::string path = buffer;
      if (!path.empty() && path.back() == '\n') path.pop_back();
      pclose(f);
      return path;
    }
    pclose(f);
  }
#elif defined(__APPLE__)
  char buffer[1024] = {0};
  FILE *f = popen("osascript -e 'POSIX path of (choose file name with prompt \"Save Image Asset\")'",
                  "r");
  if (f) {
    if (fgets(buffer, sizeof(buffer), f) != nullptr) {
      std::string path = buffer;
      if (!path.empty() && path.back() == '\n') path.pop_back();
      pclose(f);
      return path;
    }
    pclose(f);
  }
#endif
  return "";
}

void ApplyFlatDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.TabRounding = 0.0f;
    
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.GrabMinSize = 12.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.12f, 0.12f, 0.12f, 0.94f);
    colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.05f, 0.05f, 0.05f, 0.50f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.05f, 0.05f, 0.05f, 0.70f);
}


int main(int argc, char *argv[]) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "Failed to initialize SDL: " << SDL_GetError() << "\n";
    return -1;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                      SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  SDL_Window *window =
      SDL_CreateWindow("OIIO ImGui Runtime Shell", 1280, 720,
                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!window) {
    std::cerr << "Window creation failed: " << SDL_GetError() << "\n";
    SDL_Quit();
    return -1;
  }

  SDL_GLContext gl_context = SDL_GL_CreateContext(window);
  if (!gl_context) {
    std::cerr << "OpenGL Context creation failed: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return -1;
  }

  SDL_GL_MakeCurrent(window, gl_context);
  SDL_GL_SetSwapInterval(1);

  if (!gladLoadGL()) {
    std::cerr << "Failed to initialize Glad loader!\n";
    return -1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ApplyFlatDarkTheme();

  ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
  ImGui_ImplOpenGL3_Init("#version 410 core");

  Viewer viewer;

  bool running = true;
  ImVec4 clear_color = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      }
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(window)) {
        running = false;
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    float sidebar_width = 340.0f;
    float display_w = io.DisplaySize.x;
    float display_h = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(sidebar_width, display_h));
    ImGui::Begin("OIIO Pipeline Monitor", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Application Framerate: %.1f FPS", io.Framerate);
    ImGui::Separator();

    if (ImGui::Button("Open Image...", ImVec2(-1, 30))) {
      std::string chosen_path = OpenFileDialog();
      if (!chosen_path.empty()) {
          viewer.LoadFile(chosen_path);
      }
    }

    if (viewer.IsLoaded()) {
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      if (ImGui::Button("Save Image...", ImVec2(-1, 30))) {
        std::string save_path = SaveFileDialog();
        if (!save_path.empty()) {
            viewer.SaveFile(save_path);
        }
      }
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    bool is_fullscreen =
        (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (ImGui::Button(is_fullscreen ? "Exit Fullscreen"
                                    : "Fullscreen Mode",
                      ImVec2(-1, 30))) {
      SDL_SetWindowFullscreen(window, !is_fullscreen);
    }
    
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    
    // Draw the viewer controls in the side panel
    viewer.DrawControls();

    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(sidebar_width, 0));
    ImGui::SetNextWindowSize(ImVec2(display_w - sidebar_width, display_h));
    // Remove scrollbars from the main viewport, as the viewer handles its own pan/zoom
    ImGui::Begin("Image Viewport", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar);

    viewer.DrawImage(ImVec2(display_w - sidebar_width, display_h));

    ImGui::End();

    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                 clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  SDL_GL_DestroyContext(gl_context);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
