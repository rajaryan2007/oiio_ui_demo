/**
 * @file main.cpp
 * @brief Runtime shell integrating SDL3, OpenGL, Dear ImGui, and OpenImageIO.
 */

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

bool CreateTextureFromImageBuf(OIIO::ImageBuf &img_buf, GLuint *out_texture) {
  if (!img_buf.initialized())
    return false;

  OIIO::ImageSpec spec = img_buf.spec();
  int width = spec.width;
  int height = spec.height;
  int channels = spec.nchannels;

  std::vector<unsigned char> pixels(width * height * channels);

  if (!img_buf.get_pixels(img_buf.roi(), OIIO::TypeDesc::UINT8,
                          pixels.data())) {
    std::cerr << "Failed to extract pixels from OpenImageIO buffer.\n";
    return false;
  }

  GLenum gl_format = GL_RGBA;
  if (channels == 1)
    gl_format = GL_RED;
  else if (channels == 2)
    gl_format = GL_RG;
  else if (channels == 3)
    gl_format = GL_RGB;
  else if (channels == 4)
    gl_format = GL_RGBA;

  GLuint texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

  glTexImage2D(GL_TEXTURE_2D, 0, gl_format, width, height, 0, gl_format,
               GL_UNSIGNED_BYTE, pixels.data());

  *out_texture = texture_id;
  return true;
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

  ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
  ImGui_ImplOpenGL3_Init("#version 410 core");

  std::string current_file_path = "";
  std::unique_ptr<OIIO::ImageBuf> image_buffer = nullptr;
  bool file_loaded = false;

  OIIO::ImageBuf processed_buffer;
  float exposure_multiplier = 1.0f;
  float previous_exposure = 1.0f;

  GLuint gl_image_texture = 0;
  bool texture_ready = false;

  bool running = true;
  ImVec4 clear_color = ImVec4(0.15f, 0.16f, 0.21f, 1.00f);
  bool fit_to_screen = true;

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

    ImGui::Text("Core Application Metrics:");
    ImGui::Text("Application Framerate: %.1f FPS", io.Framerate);
    ImGui::Separator();

    ImGui::Text("Asset Controls:");
    if (ImGui::Button("Open Image...", ImVec2(-1, 30))) {
      std::string chosen_path = OpenFileDialog();
      if (!chosen_path.empty()) {
        if (gl_image_texture != 0) {
          glDeleteTextures(1, &gl_image_texture);
          gl_image_texture = 0;
          texture_ready = false;
        }

        current_file_path = chosen_path;
        image_buffer = std::make_unique<OIIO::ImageBuf>(current_file_path);
        file_loaded = image_buffer->read();

        if (file_loaded) {
          exposure_multiplier = 1.0f;
          previous_exposure = 1.0f;
          processed_buffer.clear();
          processed_buffer.copy(*image_buffer);

          texture_ready =
              CreateTextureFromImageBuf(processed_buffer, &gl_image_texture);
        }
      }
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    bool is_fullscreen =
        (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (ImGui::Button(is_fullscreen ? "Exit App Fullscreen"
                                    : "App Fullscreen Mode",
                      ImVec2(-1, 30))) {
      SDL_SetWindowFullscreen(window, !is_fullscreen);
    }

    ImGui::Separator();

    if (file_loaded) {
      ImGui::Text("Active File: %s", std::string(image_buffer->name()).c_str());
      ImGui::Text("Format: %s", image_buffer->spec().format.c_str());
      ImGui::Text("Resolution: %dx%d", image_buffer->spec().width,
                  image_buffer->spec().height);
    } else {
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                         "Status: Waiting for image...");
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(sidebar_width, 0));
    ImGui::SetNextWindowSize(ImVec2(display_w - sidebar_width, display_h));
    ImGui::Begin("Image Viewport", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_HorizontalScrollbar);

    if (texture_ready && gl_image_texture != 0 && file_loaded) {
      ImTextureID tex_id = (ImTextureID)(intptr_t)gl_image_texture;
      float img_w = (float)image_buffer->spec().width;
      float img_h = (float)image_buffer->spec().height;

      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      ImGui::Checkbox("Fit Image to Window", &fit_to_screen);
      ImGui::SameLine();
      ImGui::Text(" | Native Resolution: %.0fx%.0f", img_w, img_h);

      ImGui::SetNextItemWidth(200.0f);
      if (ImGui::SliderFloat("Exposure Multiplier", &exposure_multiplier, 0.0f,
                             4.0f, "%.2f")) {
        if (exposure_multiplier != previous_exposure) {
          OIIO::ImageBufAlgo::mul(processed_buffer, *image_buffer,
                                  exposure_multiplier);

          OIIO::ImageSpec spec = processed_buffer.spec();
          std::vector<unsigned char> update_pixels(spec.width * spec.height *
                                                   spec.nchannels);
          processed_buffer.get_pixels(processed_buffer.roi(),
                                      OIIO::TypeDesc::UINT8,
                                      update_pixels.data());

          GLenum gl_fmt = GL_RGBA;
          if (spec.nchannels == 1)
            gl_fmt = GL_RED;
          else if (spec.nchannels == 2)
            gl_fmt = GL_RG;
          else if (spec.nchannels == 3)
            gl_fmt = GL_RGB;
          else if (spec.nchannels == 4)
            gl_fmt = GL_RGBA;

          glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
          glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

          glBindTexture(GL_TEXTURE_2D, gl_image_texture);
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, spec.width, spec.height,
                          gl_fmt, GL_UNSIGNED_BYTE, update_pixels.data());

          previous_exposure = exposure_multiplier;
        }
      }

      ImGui::Separator();
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      float render_w = img_w;
      float render_h = img_h;

      if (fit_to_screen) {
        ImVec2 avail_size = ImGui::GetContentRegionAvail();

        if (avail_size.x > 0 && avail_size.y > 0) {
          float image_aspect = img_w / img_h;
          float avail_aspect = avail_size.x / avail_size.y;

          if (image_aspect > avail_aspect) {
            render_w = avail_size.x;
            render_h = avail_size.x / image_aspect;
          } else {
            render_h = avail_size.y;
            render_w = avail_size.y * image_aspect;
          }

          float cursor_x =
              ImGui::GetCursorPosX() + (avail_size.x - render_w) * 0.5f;
          float cursor_y =
              ImGui::GetCursorPosY() + (avail_size.y - render_h) * 0.5f;
          ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
        }
      }

      ImGui::Image(tex_id, ImVec2(render_w, render_h));
    } else {
      ImGui::Text("No active hardware texture bound.");
    }
    ImGui::End();

    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                 clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
  }

  if (gl_image_texture != 0) {
    glDeleteTextures(1, &gl_image_texture);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  SDL_GL_DestroyContext(gl_context);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
