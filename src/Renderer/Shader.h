#pragma once

#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace ds
{

    class Shader
    {
    public:
        Shader() = default;
        ~Shader();

        bool LoadFromFiles(const std::string &vertexPath, const std::string &fragmentPath);
        void Destroy();

        void Bind() const;

        void SetMat4(const char *name, const glm::mat4 &value) const;
        void SetFloat3(const char *name, const glm::vec3 &value) const;

        bool IsValid() const { return m_RendererID != 0; }

    private:
        static bool ReadTextFile(const std::string &path, std::string &outText);
        static unsigned int Compile(unsigned int shaderType, const std::string &source, std::string &errorOut);

        unsigned int m_RendererID = 0;
    };

} // namespace ds
