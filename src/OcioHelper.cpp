#include "OcioHelper.h"
#include <iostream>

OcioHelper::OcioHelper() {}

OcioHelper::~OcioHelper() {
    Reset();
}

bool OcioHelper::Init() {
    try {
        m_config = OCIO::GetCurrentConfig();
        
        for (int i = 0; i < m_config->getNumColorSpaces(); ++i) {
            m_color_spaces.push_back(m_config->getColorSpaceNameByIndex(i));
        }

        for (int i = 0; i < m_config->getNumDisplays(); ++i) {
            m_displays.push_back(m_config->getDisplay(i));
        }
        return true;
    } catch (const OCIO::Exception& e) {
        std::cerr << "OCIO Init Error: " << e.what() << "\n";
        return false;
    }
}

std::vector<std::string> OcioHelper::GetViews(const std::string& display) const {
    std::vector<std::string> views;
    if (!m_config) return views;
    for (int i = 0; i < m_config->getNumViews(display.c_str()); ++i) {
        views.push_back(m_config->getView(display.c_str(), i));
    }
    return views;
}

void OcioHelper::Reset() {
    m_shader_desc.reset();
    for (auto& tex : m_textures) {
        glDeleteTextures(1, &tex.uid);
    }
    m_textures.clear();
}

bool OcioHelper::UpdateShader(const std::string& color_space, const std::string& display, const std::string& view) {
    if (!m_config) return false;

    try {
        OCIO::ConstColorSpaceRcPtr scene_linear_space = m_config->getColorSpace(OCIO::ROLE_SCENE_LINEAR);
        if (!scene_linear_space) {
            scene_linear_space = m_config->getColorSpace(OCIO::ROLE_DEFAULT);
        }
        if (!scene_linear_space && m_config->getNumColorSpaces() > 0) {
            scene_linear_space = m_config->getColorSpace(m_config->getColorSpaceNameByIndex(0));
        }

        if (!scene_linear_space) {
            std::cerr << "OCIO missing any valid color space.\n";
            return false;
        }

        OCIO::ColorSpaceTransformRcPtr input_transform = OCIO::ColorSpaceTransform::Create();
        input_transform->setSrc(color_space.c_str());
        input_transform->setDst(scene_linear_space->getName());

        OCIO::ExposureContrastTransformRcPtr exposure_transform = OCIO::ExposureContrastTransform::Create();
        exposure_transform->makeExposureDynamic();

        OCIO::DisplayViewTransformRcPtr display_transform = OCIO::DisplayViewTransform::Create();
        display_transform->setSrc(scene_linear_space->getName());
        display_transform->setDisplay(display.c_str());
        display_transform->setView(view.c_str());

        OCIO::ExposureContrastTransformRcPtr gamma_transform = OCIO::ExposureContrastTransform::Create();
        gamma_transform->makeGammaDynamic();
        gamma_transform->setPivot(1.0);

        OCIO::GroupTransformRcPtr group_transform = OCIO::GroupTransform::Create();
        group_transform->appendTransform(input_transform);
        group_transform->appendTransform(exposure_transform);
        group_transform->appendTransform(display_transform);
        group_transform->appendTransform(gamma_transform);

        OCIO::ConstProcessorRcPtr processor = m_config->getProcessor(group_transform);
        
        Reset();

        OCIO::GpuShaderDescRcPtr shaderDesc = OCIO::GpuShaderDesc::CreateShaderDesc();
        shaderDesc->setLanguage(OCIO::GPU_LANGUAGE_GLSL_1_2);
        shaderDesc->setFunctionName("ColorFunc");
        shaderDesc->setResourcePrefix("ocio_");

        OCIO::ConstGPUProcessorRcPtr gpuProcessor = processor->getOptimizedGPUProcessor(OCIO::OPTIMIZATION_DEFAULT);
        gpuProcessor->extractGpuShaderInfo(shaderDesc);

        m_shader_desc = shaderDesc;

        OCIO::DynamicPropertyRcPtr propGamma = shaderDesc->getDynamicProperty(OCIO::DYNAMIC_PROPERTY_GAMMA);
        m_gamma_property = OCIO::DynamicPropertyValue::AsDouble(propGamma);
        
        OCIO::DynamicPropertyRcPtr propExposure = shaderDesc->getDynamicProperty(OCIO::DYNAMIC_PROPERTY_EXPOSURE);
        m_exposure_property = OCIO::DynamicPropertyValue::AsDouble(propExposure);

        return true;
    } catch (const OCIO::Exception& e) {
        std::cerr << "OCIO Update Shader Error: " << e.what() << "\n";
        Reset();
        return false;
    }
}

std::string OcioHelper::GetShaderText() const {
    if (m_shader_desc) {
        return m_shader_desc->getShaderText();
    }
    return "";
}

void OcioHelper::AllocateAllTextures(unsigned start_index) {
    if (!m_shader_desc) return;
    unsigned currIndex = start_index;

    // Process 3D LUTs
    unsigned maxTexture3D = m_shader_desc->getNum3DTextures();
    for (unsigned idx = 0; idx < maxTexture3D; ++idx) {
        const char* textureName = nullptr;
        const char* samplerName = nullptr;
        unsigned edgelen = 0;
        OCIO::Interpolation interpolation = OCIO::INTERP_LINEAR;
        m_shader_desc->get3DTexture(idx, textureName, samplerName, edgelen, interpolation);

        const float* values = nullptr;
        m_shader_desc->get3DTextureValues(idx, values);

        unsigned texId = 0;
        glGenTextures(1, &texId);
        glActiveTexture(GL_TEXTURE0 + currIndex);
        glBindTexture(GL_TEXTURE_3D, texId);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F, edgelen, edgelen, edgelen, 0, GL_RGB, GL_FLOAT, values);

        m_textures.push_back({texId, textureName, samplerName, GL_TEXTURE_3D});
        currIndex++;
    }

    // Process 1D/2D LUTs
    unsigned maxTexture2D = m_shader_desc->getNumTextures();
    for (unsigned idx = 0; idx < maxTexture2D; ++idx) {
        const char* textureName = nullptr;
        const char* samplerName = nullptr;
        unsigned width = 0;
        unsigned height = 0;
        OCIO::GpuShaderDesc::TextureType channel = OCIO::GpuShaderDesc::TEXTURE_RGB_CHANNEL;
        OCIO::Interpolation interpolation = OCIO::INTERP_LINEAR;

#if OCIO_VERSION_HEX >= 0x02030000
        OCIO::GpuShaderCreator::TextureDimensions dimensions = OCIO::GpuShaderCreator::TEXTURE_2D;
        m_shader_desc->getTexture(idx, textureName, samplerName, width, height, channel, dimensions, interpolation);
#else
        m_shader_desc->getTexture(idx, textureName, samplerName, width, height, channel, interpolation);
#endif

        const float* values = nullptr;
        m_shader_desc->getTextureValues(idx, values);

        GLint internalformat = GL_RGB32F;
        GLenum format = GL_RGB;
        if (channel == OCIO::GpuShaderCreator::TEXTURE_RED_CHANNEL) {
            internalformat = GL_R32F;
            format = GL_RED;
        }

        unsigned texId = 0;
        glGenTextures(1, &texId);
        glActiveTexture(GL_TEXTURE0 + currIndex);

        if (height > 1) {
            glBindTexture(GL_TEXTURE_2D, texId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, internalformat, width, height, 0, format, GL_FLOAT, values);
            m_textures.push_back({texId, textureName, samplerName, GL_TEXTURE_2D});
        } else {
            glBindTexture(GL_TEXTURE_1D, texId);
            glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage1D(GL_TEXTURE_1D, 0, internalformat, width, 0, format, GL_FLOAT, values);
            m_textures.push_back({texId, textureName, samplerName, GL_TEXTURE_1D});
        }
        currIndex++;
    }
}

void OcioHelper::BindShaderState(GLuint program, GLuint start_texture_unit) {
    if (!m_shader_desc) return;

    if (m_textures.empty()) {
        AllocateAllTextures(start_texture_unit);
    }

    if (m_gamma_property) m_gamma_property->setValue(m_gamma);
    if (m_exposure_property) m_exposure_property->setValue(m_exposure);

    m_uniforms.clear();
    unsigned maxUniforms = m_shader_desc->getNumUniforms();
    for (unsigned idx = 0; idx < maxUniforms; ++idx) {
        OCIO::GpuShaderDesc::UniformData data;
        const char* name = m_shader_desc->getUniform(idx, data);
        unsigned handle = glGetUniformLocation(program, name);
        if (handle != (unsigned)-1) {
            if (data.m_getDouble) {
                glUniform1f(handle, (GLfloat)data.m_getDouble());
            } else if (data.m_getBool) {
                glUniform1f(handle, (GLfloat)(data.m_getBool() ? 1.0f : 0.0f));
            } else if (data.m_getFloat3) {
                glUniform3f(handle, (GLfloat)data.m_getFloat3()[0], (GLfloat)data.m_getFloat3()[1], (GLfloat)data.m_getFloat3()[2]);
            }
        }
    }

    for (size_t i = 0; i < m_textures.size(); ++i) {
        glActiveTexture(GL_TEXTURE0 + start_texture_unit + i);
        glBindTexture(m_textures[i].type, m_textures[i].uid);
        glUniform1i(glGetUniformLocation(program, m_textures[i].samplerName.c_str()), start_texture_unit + i);
    }
}

void OcioHelper::SetExposure(double exp) { m_exposure = exp; }
void OcioHelper::SetGamma(double gam) { m_gamma = gam; }
