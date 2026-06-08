#include "Viewer.h"
#include <OpenImageIO/imagebufalgo.h>
#include <iostream>

struct ShaderCallbackData {
    GLuint program;
    float exposure;
    float gamma;
    int channel_mask;
    int rotation;
    int flip_h;
    int flip_v;
    float L, R, T, B;
    OcioHelper* ocio;
};

const char* Viewer::s_vertex_shader = R"(
#version 410 core
layout (location = 0) in vec2 Position;
layout (location = 1) in vec2 UV;
layout (location = 2) in vec4 Color;
uniform mat4 ProjMtx;
out vec2 Frag_UV;
out vec4 Frag_Color;
void main() {
    Frag_UV = UV;
    Frag_Color = Color;
    gl_Position = ProjMtx * vec4(Position.xy, 0.0, 1.0);
}
)";

const char* Viewer::s_fragment_shader_base = R"(
in vec2 Frag_UV;
in vec4 Frag_Color;
uniform sampler2D Texture;
layout (location = 0) out vec4 Out_Color;

uniform float uExposure;
uniform float uGamma;
uniform int uChannelMask;
uniform int uRotation;
uniform int uFlipH;
uniform int uFlipV;

void main() {
    vec2 uv = Frag_UV.st;
    
    if (uFlipH == 1) uv.x = 1.0 - uv.x;
    if (uFlipV == 1) uv.y = 1.0 - uv.y;
    
    if (uRotation == 90) uv = vec2(uv.y, 1.0 - uv.x);
    else if (uRotation == 180) uv = vec2(1.0 - uv.x, 1.0 - uv.y);
    else if (uRotation == 270) uv = vec2(1.0 - uv.y, uv.x);

    vec4 texColor = texture(Texture, uv);
    
    if (uChannelMask == 1) { texColor = vec4(texColor.r, texColor.r, texColor.r, 1.0); }
    else if (uChannelMask == 2) { texColor = vec4(texColor.g, texColor.g, texColor.g, 1.0); }
    else if (uChannelMask == 3) { texColor = vec4(texColor.b, texColor.b, texColor.b, 1.0); }
    else if (uChannelMask == 4) { texColor = vec4(texColor.a, texColor.a, texColor.a, 1.0); }
    else if (uChannelMask == 5) { 
        float lum = dot(texColor.rgb, vec3(0.2126, 0.7152, 0.0722));
        texColor = vec4(lum, lum, lum, 1.0); 
    }
    
    texColor.rgb *= uExposure;
    
    if (uGamma > 0.0 && uGamma != 1.0) {
        texColor.rgb = pow(max(texColor.rgb, vec3(0.0)), vec3(1.0 / uGamma));
    }
    
#ifdef USE_OCIO
    texColor = ColorFunc(texColor);
#endif

    Out_Color = Frag_Color * texColor;
}
)";

Viewer::Viewer() {
    if (m_ocio.Init()) {
        if (!m_ocio.GetColorSpaces().empty()) m_ocio_color_space = m_ocio.GetColorSpaces()[0];
        if (!m_ocio.GetDisplays().empty()) {
            m_ocio_display = m_ocio.GetDisplays()[0];
            if (!m_ocio.GetViews(m_ocio_display).empty()) m_ocio_view = m_ocio.GetViews(m_ocio_display)[0];
        }
    }
    InitShader();
}

Viewer::~Viewer() {
    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
    }
    if (m_shader != 0) {
        glDeleteProgram(m_shader);
    }
}

void Viewer::InitShader() {
    if (m_shader != 0) glDeleteProgram(m_shader);

    std::string frag = "#version 410 core\n";
    frag += "#define texture2D texture\n#define texture3D texture\n";
    
    if (m_use_ocio && m_ocio.IsValid()) {
        frag += m_ocio.GetShaderText() + "\n";
        frag += "#define USE_OCIO\n";
    }
    
    frag += s_fragment_shader_base;
    m_shader = gl::CreateProgram(s_vertex_shader, frag.c_str());
}

bool Viewer::LoadFile(const std::string& filepath) {
    m_image_buf = std::make_unique<OIIO::ImageBuf>(filepath);
    m_is_loaded = m_image_buf->read();
    if (m_is_loaded) {
        m_file_path = filepath;
        SetupTexture(*m_image_buf);
        m_pan_offset = ImVec2(0,0);
        m_zoom = 1.0f;
        m_rotation = 0;
        m_flip_h = false;
        m_flip_v = false;
    }
    return m_is_loaded;
}

void Viewer::SaveFile(const std::string& filepath) {
    if (!m_is_loaded) return;
    
    OIIO::ImageBuf temp_buf;
    temp_buf.copy(*m_image_buf);

    if (m_flip_h) OIIO::ImageBufAlgo::flop(temp_buf, temp_buf);
    if (m_flip_v) OIIO::ImageBufAlgo::flip(temp_buf, temp_buf);
    
    if (m_rotation == 90) OIIO::ImageBufAlgo::rotate90(temp_buf, temp_buf);
    else if (m_rotation == 180) OIIO::ImageBufAlgo::rotate180(temp_buf, temp_buf);
    else if (m_rotation == 270) OIIO::ImageBufAlgo::rotate270(temp_buf, temp_buf);

    temp_buf.write(filepath);
}

void Viewer::RotateLeft() {
    m_rotation = (m_rotation + 270) % 360;
}

void Viewer::RotateRight() {
    m_rotation = (m_rotation + 90) % 360;
}

void Viewer::FlipHorizontal() {
    m_flip_h = !m_flip_h;
}

void Viewer::FlipVertical() {
    m_flip_v = !m_flip_v;
}

void Viewer::SetupTexture(const OIIO::ImageBuf& image_buffer) {
    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }

    OIIO::ImageSpec spec = image_buffer.spec();
    int width = spec.width;
    int height = spec.height;
    int channels = spec.nchannels;

    std::vector<float> pixels(width * height * channels);
    image_buffer.get_pixels(image_buffer.roi(), OIIO::TypeDesc::FLOAT, pixels.data());

    GLenum gl_format = GL_RGBA;
    if (channels == 1) gl_format = GL_RED;
    else if (channels == 2) gl_format = GL_RG;
    else if (channels == 3) gl_format = GL_RGB;
    else if (channels == 4) gl_format = GL_RGBA;

    GLenum internal_format = GL_RGBA32F;
    if (channels == 1) internal_format = GL_R32F;
    else if (channels == 2) internal_format = GL_RG32F;
    else if (channels == 3) internal_format = GL_RGB32F;

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, gl_format, GL_FLOAT, pixels.data());
}

void Viewer::DrawControls() {
    if (!m_is_loaded || m_texture == 0) {
        ImGui::TextDisabled("No image loaded.");
        return;
    }

    ImGui::Text("Viewport Controls:");
    ImGui::Checkbox("Fit to Window", &m_fit_to_screen);
    if (ImGui::Button("Reset View", ImVec2(-1, 0))) {
        m_pan_offset = ImVec2(0, 0);
        m_zoom = 1.0f;
        m_rotation = 0;
        m_flip_h = false;
        m_flip_v = false;
    }
    
    ImGui::Dummy(ImVec2(0, 4.0f));
    ImGui::Text("Orientation:");
    if (ImGui::Button("Rot L", ImVec2(60, 0))) RotateLeft();
    ImGui::SameLine();
    if (ImGui::Button("Rot R", ImVec2(60, 0))) RotateRight();
    ImGui::SameLine();
    if (ImGui::Button("Flip H", ImVec2(60, 0))) FlipHorizontal();
    ImGui::SameLine();
    if (ImGui::Button("Flip V", ImVec2(60, 0))) FlipVertical();

    ImGui::Dummy(ImVec2(0, 4.0f));
    ImGui::BeginDisabled(m_fit_to_screen);
    ImGui::Text("Zoom:");
    if (ImGui::Button("-", ImVec2(30, 0))) { m_zoom = std::max(0.1f, m_zoom / 1.1f); }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::SliderFloat("##ZoomSlider", &m_zoom, 0.1f, 10.0f, "%.2fx");
    ImGui::SameLine();
    if (ImGui::Button("+", ImVec2(30, 0))) { m_zoom = std::min(50.0f, m_zoom * 1.1f); }
    ImGui::SameLine();
    if (ImGui::Button("1:1", ImVec2(30, 0))) { m_zoom = 1.0f; m_pan_offset = ImVec2(0,0); }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::Text("Color Adjustments:");

    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Exposure", &m_exposure, 0.0f, 4.0f, "Exposure: %.2f");
    
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Gamma", &m_gamma, 0.1f, 3.0f, "Gamma: %.2f");
    
    ImGui::SetNextItemWidth(-1);
    const char* channels[] = { "RGB", "Red", "Green", "Blue", "Alpha", "Luminance" };
    ImGui::Combo("##Channels", &m_channel_mask, channels, IM_ARRAYSIZE(channels));

    ImGui::Separator();
    
    // OCIO Controls
    if (ImGui::Checkbox("Enable OpenColorIO", &m_use_ocio)) {
        if (m_use_ocio && m_ocio.UpdateShader(m_ocio_color_space, m_ocio_display, m_ocio_view)) {
            InitShader();
        } else if (!m_use_ocio) {
            InitShader();
        }
    }
    
    if (m_use_ocio) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##ColorSpace", m_ocio_color_space.c_str())) {
            for (const auto& cs : m_ocio.GetColorSpaces()) {
                bool selected = (cs == m_ocio_color_space);
                if (ImGui::Selectable(cs.c_str(), selected)) {
                    m_ocio_color_space = cs;
                    if (m_ocio.UpdateShader(m_ocio_color_space, m_ocio_display, m_ocio_view)) InitShader();
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##Display", m_ocio_display.c_str())) {
            for (const auto& d : m_ocio.GetDisplays()) {
                bool selected = (d == m_ocio_display);
                if (ImGui::Selectable(d.c_str(), selected)) {
                    m_ocio_display = d;
                    auto views = m_ocio.GetViews(m_ocio_display);
                    if (!views.empty()) m_ocio_view = views[0];
                    if (m_ocio.UpdateShader(m_ocio_color_space, m_ocio_display, m_ocio_view)) InitShader();
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##View", m_ocio_view.c_str())) {
            for (const auto& v : m_ocio.GetViews(m_ocio_display)) {
                bool selected = (v == m_ocio_view);
                if (ImGui::Selectable(v.c_str(), selected)) {
                    m_ocio_view = v;
                    if (m_ocio.UpdateShader(m_ocio_color_space, m_ocio_display, m_ocio_view)) InitShader();
                }
            }
            ImGui::EndCombo();
        }
    }
}

void Viewer::DrawImage(const ImVec2& size) {
    if (!m_is_loaded || m_texture == 0) return;

    ImVec2 avail = size;
    ImVec2 cursor_start = ImGui::GetCursorScreenPos();
    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsWindowHovered()) {
        if (!m_fit_to_screen) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                m_pan_offset.x += io.MouseDelta.x;
                m_pan_offset.y += io.MouseDelta.y;
            }
            if (io.MouseWheel != 0.0f) {
                float zoom_factor = io.MouseWheel > 0 ? 1.1f : 0.9f;
                ImVec2 mouse_pos = io.MousePos;
                
                m_pan_offset.x = (m_pan_offset.x - (mouse_pos.x - cursor_start.x)) * zoom_factor + (mouse_pos.x - cursor_start.x);
                m_pan_offset.y = (m_pan_offset.y - (mouse_pos.y - cursor_start.y)) * zoom_factor + (mouse_pos.y - cursor_start.y);
                m_zoom *= zoom_factor;
            }
        }
    }

    float img_w = (float)m_image_buf->spec().width;
    float img_h = (float)m_image_buf->spec().height;
    
    if (m_rotation == 90 || m_rotation == 270) {
        std::swap(img_w, img_h);
    }
    
    ImVec2 render_pos = cursor_start;
    ImVec2 render_size(img_w, img_h);

    if (m_fit_to_screen) {
        float image_aspect = img_w / img_h;
        float avail_aspect = avail.x / avail.y;

        if (image_aspect > avail_aspect) {
            render_size.x = avail.x;
            render_size.y = avail.x / image_aspect;
        } else {
            render_size.y = avail.y;
            render_size.x = avail.y * image_aspect;
        }
        render_pos.x += (avail.x - render_size.x) * 0.5f;
        render_pos.y += (avail.y - render_size.y) * 0.5f;
    } else {
        render_size.x *= m_zoom;
        render_size.y *= m_zoom;
        render_pos.x += m_pan_offset.x;
        render_pos.y += m_pan_offset.y;
    }
    
    ImGui::SetCursorScreenPos(render_pos);

    // Render with custom shader via ImDrawList Callback
    ShaderCallbackData cbData;
    cbData.program = m_shader;
    cbData.exposure = m_exposure;
    cbData.gamma = m_gamma;
    cbData.channel_mask = m_channel_mask;
    cbData.rotation = m_rotation;
    cbData.flip_h = m_flip_h ? 1 : 0;
    cbData.flip_v = m_flip_v ? 1 : 0;
    cbData.L = 0.0f;
    cbData.R = io.DisplaySize.x;
    cbData.T = 0.0f;
    cbData.B = io.DisplaySize.y;
    cbData.ocio = (m_use_ocio && m_ocio.IsValid()) ? &m_ocio : nullptr;

    if (cbData.ocio) {
        cbData.ocio->SetExposure(m_exposure);
        cbData.ocio->SetGamma(m_gamma);
    }

    auto callback = [](const ImDrawList*, const ImDrawCmd* cmd) {
        auto* data = (const ShaderCallbackData*)cmd->UserCallbackData;
        glUseProgram(data->program);
        glUniform1i(glGetUniformLocation(data->program, "Texture"), 0);
        glUniform1f(glGetUniformLocation(data->program, "uExposure"), data->exposure);
        glUniform1f(glGetUniformLocation(data->program, "uGamma"), data->gamma);
        glUniform1i(glGetUniformLocation(data->program, "uChannelMask"), data->channel_mask);
        glUniform1i(glGetUniformLocation(data->program, "uRotation"), data->rotation);
        glUniform1i(glGetUniformLocation(data->program, "uFlipH"), data->flip_h);
        glUniform1i(glGetUniformLocation(data->program, "uFlipV"), data->flip_v);
        
        float L = data->L;
        float R = data->R;
        float T = data->T;
        float B = data->B;
        const float ortho_projection[4][4] = {
            { 2.0f/(R-L),   0.0f,         0.0f,   0.0f },
            { 0.0f,         2.0f/(T-B),   0.0f,   0.0f },
            { 0.0f,         0.0f,        -1.0f,   0.0f },
            { (R+L)/(L-R),  (T+B)/(B-T),  0.0f,   1.0f },
        };
        glUniformMatrix4fv(glGetUniformLocation(data->program, "ProjMtx"), 1, GL_FALSE, &ortho_projection[0][0]);

        if (data->ocio) {
            data->ocio->BindShaderState(data->program, 1);
        }
    };

    ImGui::GetWindowDrawList()->AddCallback(callback, &cbData, sizeof(ShaderCallbackData));
    ImGui::Image((ImTextureID)(intptr_t)m_texture, render_size);
    ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    // Pixel Probe functionality
    if (ImGui::IsWindowHovered()) {
        ImVec2 mouse_pos = ImGui::GetMousePos();
        float img_x = (mouse_pos.x - render_pos.x) / render_size.x * img_w;
        float img_y = (mouse_pos.y - render_pos.y) / render_size.y * img_h;
        
        if (img_x >= 0 && img_x < img_w && img_y >= 0 && img_y < img_h) {
            // Apply reverse rotation / flip
            float orig_x = img_x;
            float orig_y = img_y;
            
            if (m_rotation == 90) {
                orig_x = img_h - img_y;
                orig_y = img_x;
            } else if (m_rotation == 180) {
                orig_x = img_w - img_x;
                orig_y = img_h - img_y;
            } else if (m_rotation == 270) {
                orig_x = img_y;
                orig_y = img_w - img_x;
            }
            
            if (m_flip_h) orig_x = m_image_buf->spec().width - orig_x;
            if (m_flip_v) orig_y = m_image_buf->spec().height - orig_y;

            int px = (int)orig_x;
            int py = (int)orig_y;
            int nchannels = m_image_buf->spec().nchannels;
            std::vector<float> pixel(nchannels);
            m_image_buf->getpixel(px, py, 0, pixel.data());
            
            ImGui::BeginTooltip();
            ImGui::Text("Pixel [%d, %d]", px, py);
            ImGui::Separator();
            for (int c = 0; c < nchannels; c++) {
                if (c > 0) ImGui::SameLine();
                ImGui::Text("%.3f", pixel[c]);
            }
            ImGui::EndTooltip();
        }
    }
}
