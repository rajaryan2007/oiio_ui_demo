#pragma once

#include <OpenColorIO/OpenColorIO.h>
#include <glad/glad.h>
#include <string>
#include <vector>

namespace OCIO = OCIO_NAMESPACE;

class OcioHelper {
public:
    OcioHelper();
    ~OcioHelper();

    bool Init();
    bool UpdateShader(const std::string& color_space, const std::string& display, const std::string& view);
    
    std::string GetShaderText() const;
    void BindShaderState(GLuint program, GLuint start_texture_unit);

    std::vector<std::string> GetColorSpaces() const { return m_color_spaces; }
    std::vector<std::string> GetDisplays() const { return m_displays; }
    std::vector<std::string> GetViews(const std::string& display) const;

    void SetExposure(double exp);
    void SetGamma(double gam);

    bool IsValid() const { return m_shader_desc != nullptr; }

private:
    void Reset();
    void AllocateAllTextures(unsigned start_index);

    struct TextureDesc {
        unsigned uid;
        std::string textureName;
        std::string samplerName;
        unsigned type;
    };

    struct UniformDesc {
        std::string name;
        OCIO::GpuShaderDesc::UniformData data;
        unsigned handle;
    };

    OCIO::ConstConfigRcPtr m_config;
    OCIO::GpuShaderDescRcPtr m_shader_desc;
    
    std::vector<TextureDesc> m_textures;
    std::vector<UniformDesc> m_uniforms;

    std::vector<std::string> m_color_spaces;
    std::vector<std::string> m_displays;

    OCIO::DynamicPropertyDoubleRcPtr m_gamma_property;
    OCIO::DynamicPropertyDoubleRcPtr m_exposure_property;

    double m_exposure = 0.0;
    double m_gamma = 1.0;
};
