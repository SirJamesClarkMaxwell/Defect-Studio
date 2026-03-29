#include "Renderer/Shader.h"

#include "Core/Logger.h"

#include <glad/gl.h>

#include <fstream>
#include <sstream>

namespace ds
{

    Shader::~Shader()
    {
        Destroy();
    }

    bool Shader::ReadTextFile(const std::string &path, std::string &outText)
    {
        std::ifstream in(path, std::ios::in);
        if (!in.is_open())
        {
            return false;
        }

        std::stringstream stream;
        stream << in.rdbuf();
        outText = stream.str();
        return true;
    }

    unsigned int Shader::Compile(unsigned int shaderType, const std::string &source, std::string &errorOut)
    {
        unsigned int id = glCreateShader(shaderType);
        const char *src = source.c_str();
        glShaderSource(id, 1, &src, nullptr);
        glCompileShader(id);

        int success = 0;
        glGetShaderiv(id, GL_COMPILE_STATUS, &success);
        if (success == GL_FALSE)
        {
            int length = 0;
            glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
            errorOut.resize(static_cast<std::size_t>(length));
            glGetShaderInfoLog(id, length, &length, errorOut.data());
            glDeleteShader(id);
            return 0;
        }

        return id;
    }

    bool Shader::LoadFromFiles(const std::string &vertexPath, const std::string &fragmentPath)
    {
        Destroy();

        std::string vertexSource;
        std::string fragmentSource;

        if (!ReadTextFile(vertexPath, vertexSource))
        {
            LogError("Failed to read vertex shader: " + vertexPath);
            return false;
        }

        if (!ReadTextFile(fragmentPath, fragmentSource))
        {
            LogError("Failed to read fragment shader: " + fragmentPath);
            return false;
        }

        std::string error;
        const unsigned int vs = Compile(GL_VERTEX_SHADER, vertexSource, error);
        if (vs == 0)
        {
            LogError("Vertex shader compile error: " + error);
            return false;
        }

        const unsigned int fs = Compile(GL_FRAGMENT_SHADER, fragmentSource, error);
        if (fs == 0)
        {
            glDeleteShader(vs);
            LogError("Fragment shader compile error: " + error);
            return false;
        }

        m_RendererID = glCreateProgram();
        glAttachShader(m_RendererID, vs);
        glAttachShader(m_RendererID, fs);
        glLinkProgram(m_RendererID);

        glDeleteShader(vs);
        glDeleteShader(fs);

        int linked = 0;
        glGetProgramiv(m_RendererID, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE)
        {
            int length = 0;
            glGetProgramiv(m_RendererID, GL_INFO_LOG_LENGTH, &length);
            std::string log;
            log.resize(static_cast<std::size_t>(length));
            glGetProgramInfoLog(m_RendererID, length, &length, log.data());
            LogError("Shader link error: " + log);
            Destroy();
            return false;
        }

        return true;
    }

    void Shader::Destroy()
    {
        if (m_RendererID != 0)
        {
            glDeleteProgram(m_RendererID);
            m_RendererID = 0;
        }
    }

    void Shader::Bind() const
    {
        glUseProgram(m_RendererID);
    }

    void Shader::SetMat4(const char *name, const glm::mat4 &value) const
    {
        const int location = glGetUniformLocation(m_RendererID, name);
        glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
    }

    void Shader::SetFloat3(const char *name, const glm::vec3 &value) const
    {
        const int location = glGetUniformLocation(m_RendererID, name);
        glUniform3f(location, value.x, value.y, value.z);
    }

    void Shader::SetFloat4(const char *name, float x, float y, float z, float w) const
    {
        const int location = glGetUniformLocation(m_RendererID, name);
        glUniform4f(location, x, y, z, w);
    }

    void Shader::SetFloat(const char *name, float value) const
    {
        const int location = glGetUniformLocation(m_RendererID, name);
        glUniform1f(location, value);
    }

} // namespace ds
