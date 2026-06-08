#pragma once

#include "Shader.h"
#include <OpenImageIO/imagebuf.h>
#include <glad/glad.h>
#include <imgui.h>
#include <memory>
#include <string>
#include <vector>
#include "OcioHelper.h"

class Viewer {
public:
    Viewer();
    ~Viewer();

    bool LoadFile(const std::string& filepath);
    void SaveFile(const std::string& filepath);
    bool IsLoaded() const { return m_is_loaded; }
    void DrawControls();
    void DrawImage(const ImVec2& size);

    void RotateLeft();
    void RotateRight();
    void FlipHorizontal();
    void FlipVertical();

private:
    void InitShader();
    void SetupTexture(const OIIO::ImageBuf& image_buffer);

    std::unique_ptr<OIIO::ImageBuf> m_image_buf;
    std::string m_file_path;
    bool m_is_loaded = false;

    GLuint m_texture = 0;
    GLuint m_shader = 0;

    // Viewport State
    ImVec2 m_pan_offset = ImVec2(0, 0);
    float m_zoom = 1.0f;
    bool m_fit_to_screen = true;
    
    // Transforms
    int m_rotation = 0; // 0, 90, 180, 270
    bool m_flip_h = false;
    bool m_flip_v = false;

    // Color processing
    float m_exposure = 1.0f;
    float m_gamma = 1.0f;
    int m_channel_mask = 0; // 0=RGB, 1=R, 2=G, 3=B, 4=A, 5=Lum

    // OCIO
    OcioHelper m_ocio;
    bool m_use_ocio = false;
    std::string m_ocio_color_space;
    std::string m_ocio_display;
    std::string m_ocio_view;

    // GLSL Shader strings
    static const char* s_vertex_shader;
    static const char* s_fragment_shader_base;
};
