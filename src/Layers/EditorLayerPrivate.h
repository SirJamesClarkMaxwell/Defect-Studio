#pragma once
#include "Layers/EditorLayer.h"

#include "Core/ApplicationContext.h"
#include "Core/Logger.h"
#include "Core/Profiling.h"
#include "Editor/SceneGroupingBackend.h"
#include "Layers/ImGuiLayer.h"
#include "Renderer/OpenGLRendererBackend.h"
#include "Renderer/OrbitCamera.h"
#include "UI/PropertiesPanel.h"
#include "UI/SettingsPanel.h"

#include <algorithm>

#include <imgui.h>
#include <ImGuizmo.h>
#include <ImViewGuizmo.h>

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>
#endif


namespace ds
{

    namespace
    {
        constexpr float kBaseOrbitSensitivity = 0.01f;
        constexpr float kBasePanSensitivity = 0.18f;
        constexpr float kBaseZoomSensitivity = 0.17f;
        constexpr const char *kSelectionDebugLogPath = "logs/selection_debug.log";

        std::string NormalizeElementSymbol(const std::string &symbol);

        SceneUUID GenerateSceneUUID()
        {
            static std::mt19937_64 rng(std::random_device{}());
            static std::uniform_int_distribution<std::uint64_t> dist(
                std::numeric_limits<std::uint64_t>::min() + 1,
                std::numeric_limits<std::uint64_t>::max());
            return dist(rng);
        }

        float NormalizeAngleRadians(float angle)
        {
            const float twoPi = glm::two_pi<float>();
            while (angle > glm::pi<float>())
            {
                angle -= twoPi;
            }
            while (angle < -glm::pi<float>())
            {
                angle += twoPi;
            }
            return angle;
        }

        float LerpAngleRadians(float from, float to, float t)
        {
            return from + NormalizeAngleRadians(to - from) * t;
        }

        float EaseOutCubic(float t)
        {
            const float x = glm::clamp(t, 0.0f, 1.0f);
            const float inv = 1.0f - x;
            return 1.0f - inv * inv * inv;
        }

        std::uint64_t MakeBondPairKey(std::size_t atomA, std::size_t atomB)
        {
            const std::uint64_t low = static_cast<std::uint64_t>(std::min(atomA, atomB));
            const std::uint64_t high = static_cast<std::uint64_t>(std::max(atomA, atomB));
            return (low << 32) | (high & 0xffffffffull);
        }

        std::string MakeAngleTripletKey(std::size_t atomA, std::size_t atomB, std::size_t atomC)
        {
            std::size_t a = atomA;
            std::size_t c = atomC;
            if (a > c)
            {
                std::swap(a, c);
            }

            return std::to_string(a) + "|" + std::to_string(atomB) + "|" + std::to_string(c);
        }

        std::string BuildElementPairScaleKey(const std::string &elementA, const std::string &elementB)
        {
            std::string a = NormalizeElementSymbol(elementA);
            std::string b = NormalizeElementSymbol(elementB);
            if (a.empty() || b.empty())
            {
                return std::string();
            }

            if (a > b)
            {
                std::swap(a, b);
            }

            return a + "-" + b;
        }

        std::unordered_map<std::string, float> ParsePairScaleOverrides(const std::string &encoded)
        {
            std::unordered_map<std::string, float> overrides;
            if (encoded.empty())
            {
                return overrides;
            }

            std::size_t cursor = 0;
            while (cursor <= encoded.size())
            {
                const std::size_t sep = encoded.find(';', cursor);
                const std::string token = (sep == std::string::npos)
                                              ? encoded.substr(cursor)
                                              : encoded.substr(cursor, sep - cursor);
                if (!token.empty())
                {
                    const std::size_t eq = token.find(':');
                    if (eq != std::string::npos)
                    {
                        const std::string key = token.substr(0, eq);
                        const std::string value = token.substr(eq + 1);
                        try
                        {
                            overrides[key] = std::stof(value);
                        }
                        catch (...)
                        {
                        }
                    }
                }

                if (sep == std::string::npos)
                {
                    break;
                }
                cursor = sep + 1;
            }

            return overrides;
        }

        std::string SerializePairScaleOverrides(const std::unordered_map<std::string, float> &overrides)
        {
            if (overrides.empty())
            {
                return std::string();
            }

            std::vector<std::string> keys;
            keys.reserve(overrides.size());
            for (const auto &[key, value] : overrides)
            {
                (void)value;
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());

            std::ostringstream out;
            for (std::size_t i = 0; i < keys.size(); ++i)
            {
                if (i > 0)
                {
                    out << ';';
                }

                const auto valueIt = overrides.find(keys[i]);
                if (valueIt == overrides.end())
                {
                    continue;
                }

                out << keys[i] << ':' << valueIt->second;
            }
            return out.str();
        }

        std::unordered_map<std::string, glm::vec3> ParseElementColorOverrides(const std::string &encoded)
        {
            std::unordered_map<std::string, glm::vec3> overrides;
            if (encoded.empty())
            {
                return overrides;
            }

            std::size_t cursor = 0;
            while (cursor <= encoded.size())
            {
                const std::size_t sep = encoded.find(';', cursor);
                const std::string token = (sep == std::string::npos)
                                              ? encoded.substr(cursor)
                                              : encoded.substr(cursor, sep - cursor);
                if (!token.empty())
                {
                    const std::size_t eq = token.find(':');
                    if (eq != std::string::npos)
                    {
                        const std::string element = NormalizeElementSymbol(token.substr(0, eq));
                        const std::string values = token.substr(eq + 1);
                        std::stringstream stream(values);
                        std::string part;
                        std::array<float, 3> rgb = {0.0f, 0.0f, 0.0f};
                        int componentIndex = 0;
                        while (std::getline(stream, part, ',') && componentIndex < 3)
                        {
                            try
                            {
                                rgb[componentIndex] = std::stof(part);
                                ++componentIndex;
                            }
                            catch (...)
                            {
                                componentIndex = 0;
                                break;
                            }
                        }

                        if (!element.empty() && componentIndex == 3)
                        {
                            overrides[element] = glm::clamp(glm::vec3(rgb[0], rgb[1], rgb[2]), glm::vec3(0.0f), glm::vec3(1.0f));
                        }
                    }
                }

                if (sep == std::string::npos)
                {
                    break;
                }
                cursor = sep + 1;
            }

            return overrides;
        }

        std::string SerializeElementColorOverrides(const std::unordered_map<std::string, glm::vec3> &overrides)
        {
            if (overrides.empty())
            {
                return std::string();
            }

            std::vector<std::string> keys;
            keys.reserve(overrides.size());
            for (const auto &[key, value] : overrides)
            {
                (void)value;
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());

            std::ostringstream out;
            out << std::fixed << std::setprecision(4);
            for (std::size_t i = 0; i < keys.size(); ++i)
            {
                const auto it = overrides.find(keys[i]);
                if (it == overrides.end())
                {
                    continue;
                }
                if (i > 0)
                {
                    out << ';';
                }
                const glm::vec3 c = glm::clamp(it->second, glm::vec3(0.0f), glm::vec3(1.0f));
                out << keys[i] << ':' << c.r << ',' << c.g << ',' << c.b;
            }
            return out.str();
        }

        std::unordered_map<std::string, float> ParseElementScaleOverrides(const std::string &encoded)
        {
            std::unordered_map<std::string, float> overrides;
            if (encoded.empty())
            {
                return overrides;
            }

            std::size_t cursor = 0;
            while (cursor <= encoded.size())
            {
                const std::size_t sep = encoded.find(';', cursor);
                const std::string token = (sep == std::string::npos)
                                              ? encoded.substr(cursor)
                                              : encoded.substr(cursor, sep - cursor);
                if (!token.empty())
                {
                    const std::size_t eq = token.find(':');
                    if (eq != std::string::npos)
                    {
                        const std::string element = NormalizeElementSymbol(token.substr(0, eq));
                        const std::string value = token.substr(eq + 1);
                        if (!element.empty())
                        {
                            try
                            {
                                overrides[element] = std::clamp(std::stof(value), 0.1f, 4.0f);
                            }
                            catch (...)
                            {
                            }
                        }
                    }
                }

                if (sep == std::string::npos)
                {
                    break;
                }
                cursor = sep + 1;
            }

            return overrides;
        }

        std::string SerializeElementScaleOverrides(const std::unordered_map<std::string, float> &overrides)
        {
            if (overrides.empty())
            {
                return std::string();
            }

            std::vector<std::string> keys;
            keys.reserve(overrides.size());
            for (const auto &[key, value] : overrides)
            {
                (void)value;
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());

            std::ostringstream out;
            out << std::fixed << std::setprecision(4);
            for (std::size_t i = 0; i < keys.size(); ++i)
            {
                const auto it = overrides.find(keys[i]);
                if (it == overrides.end())
                {
                    continue;
                }
                if (i > 0)
                {
                    out << ';';
                }

                out << keys[i] << ':' << std::clamp(it->second, 0.1f, 4.0f);
            }

            return out.str();
        }

        std::string BuildDebugTimestampNow()
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

            std::tm localTime{};
#ifdef _WIN32
            localtime_s(&localTime, &nowTime);
#else
            localtime_r(&nowTime, &localTime);
#endif

            std::ostringstream stream;
            stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
            return stream.str();
        }

        std::string NormalizeElementSymbol(const std::string &symbol)
        {
            if (symbol.empty())
            {
                return std::string();
            }

            std::string compact;
            compact.reserve(symbol.size());
            for (char c : symbol)
            {
                if (std::isalpha(static_cast<unsigned char>(c)))
                {
                    compact.push_back(c);
                }
            }

            if (compact.empty())
            {
                return std::string();
            }

            std::string lower;
            lower.reserve(compact.size());
            for (char c : compact)
            {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }

            static const std::unordered_map<std::string, std::string> kNameToSymbol = {
                {"hydrogen", "H"}, {"helium", "He"}, {"lithium", "Li"}, {"beryllium", "Be"}, {"boron", "B"}, {"carbon", "C"}, {"nitrogen", "N"}, {"oxygen", "O"}, {"fluorine", "F"}, {"neon", "Ne"}, {"sodium", "Na"}, {"magnesium", "Mg"}, {"aluminium", "Al"}, {"aluminum", "Al"}, {"silicon", "Si"}, {"krzem", "Si"}, {"phosphorus", "P"}, {"sulfur", "S"}, {"sulphur", "S"}, {"chlorine", "Cl"}, {"argon", "Ar"}, {"potassium", "K"}, {"calcium", "Ca"}, {"scandium", "Sc"}, {"titanium", "Ti"}, {"vanadium", "V"}, {"chromium", "Cr"}, {"manganese", "Mn"}, {"iron", "Fe"}, {"cobalt", "Co"}, {"nickel", "Ni"}, {"copper", "Cu"}, {"zinc", "Zn"}, {"gallium", "Ga"}, {"germanium", "Ge"}, {"german", "Ge"}, {"germaniu", "Ge"}, {"germanium", "Ge"}, {"arsenic", "As"}, {"selenium", "Se"}, {"bromine", "Br"}, {"krypton", "Kr"}, {"rubidium", "Rb"}, {"strontium", "Sr"}, {"yttrium", "Y"}, {"zirconium", "Zr"}};

            const auto aliasIt = kNameToSymbol.find(lower);
            if (aliasIt != kNameToSymbol.end())
            {
                return aliasIt->second;
            }

            std::string normalized;
            normalized.reserve(compact.size());
            normalized.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(compact[0]))));
            for (std::size_t i = 1; i < compact.size(); ++i)
            {
                normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(compact[i]))));
            }

            static const std::unordered_set<std::string> kKnownSymbols = {
                "H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne", "Na", "Mg", "Al", "Si", "P", "S", "Cl", "Ar", "K", "Ca", "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr", "Rb", "Sr", "Y", "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn", "Sb", "Te", "I", "Xe", "Cs", "Ba", "La", "Ce", "Pr", "Nd", "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu", "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg", "Tl", "Pb", "Bi", "Po", "At", "Rn", "Fr", "Ra", "Ac", "Th", "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm", "Md", "No", "Lr", "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds", "Rg"};

            if (kKnownSymbols.find(normalized) == kKnownSymbols.end())
            {
                return std::string();
            }

            return normalized;
        }

        std::vector<std::string> SplitCsv(const std::string &value)
        {
            std::vector<std::string> items;
            if (value.empty())
            {
                return items;
            }

            std::size_t start = 0;
            while (start <= value.size())
            {
                const std::size_t sep = value.find(',', start);
                if (sep == std::string::npos)
                {
                    items.push_back(value.substr(start));
                    break;
                }
                items.push_back(value.substr(start, sep - start));
                start = sep + 1;
            }
            return items;
        }

        template <typename T>
        std::string JoinCsv(const std::vector<T> &values)
        {
            std::ostringstream out;
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0)
                {
                    out << ',';
                }
                out << values[i];
            }
            return out.str();
        }

        bool DrawColoredVec3Control(const char *label, glm::vec3 &value, float speed, float minValue, float maxValue, const char *format)
        {
            bool changed = false;

            ImGui::PushID(label);
            ImGui::Columns(2, nullptr, false);
            ImGui::SetColumnWidth(0, 110.0f);
            ImGui::TextUnformatted(label);
            ImGui::NextColumn();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0f, 0.0f});

            const float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
            const ImVec2 buttonSize = {lineHeight + 2.0f, lineHeight};
            const float itemWidth = std::max(10.0f, (ImGui::GetContentRegionAvail().x - buttonSize.x * 3.0f - 8.0f) / 3.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.23f, 0.20f, 1.0f));
            if (ImGui::Button("X", buttonSize))
            {
                value.x = 0.0f;
                changed = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(itemWidth);
            changed |= ImGui::DragFloat("##X", &value.x, speed, minValue, maxValue, format);
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.62f, 0.29f, 1.0f));
            if (ImGui::Button("Y", buttonSize))
            {
                value.y = 0.0f;
                changed = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(itemWidth);
            changed |= ImGui::DragFloat("##Y", &value.y, speed, minValue, maxValue, format);
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.40f, 0.75f, 1.0f));
            if (ImGui::Button("Z", buttonSize))
            {
                value.z = 0.0f;
                changed = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(itemWidth);
            changed |= ImGui::DragFloat("##Z", &value.z, speed, minValue, maxValue, format);

            ImGui::PopStyleVar();
            ImGui::Columns(1);
            ImGui::PopID();

            return changed;
        }

        float AtomicMassByElementSymbol(const std::string &symbol)
        {
            static const std::unordered_map<std::string, float> kAtomicMasses = {
                {"H", 1.008f},
                {"He", 4.0026f},
                {"Li", 6.94f},
                {"Be", 9.0122f},
                {"B", 10.81f},
                {"C", 12.011f},
                {"N", 14.007f},
                {"O", 15.999f},
                {"F", 18.998f},
                {"Ne", 20.180f},
                {"Na", 22.990f},
                {"Mg", 24.305f},
                {"Al", 26.982f},
                {"Si", 28.085f},
                {"P", 30.974f},
                {"S", 32.06f},
                {"Cl", 35.45f},
                {"Ar", 39.948f},
                {"K", 39.098f},
                {"Ca", 40.078f},
                {"Sc", 44.956f},
                {"Ti", 47.867f},
                {"V", 50.942f},
                {"Cr", 51.996f},
                {"Mn", 54.938f},
                {"Fe", 55.845f},
                {"Co", 58.933f},
                {"Ni", 58.693f},
                {"Cu", 63.546f},
                {"Zn", 65.38f},
                {"Ga", 69.723f},
                {"Ge", 72.630f},
                {"As", 74.922f},
                {"Se", 78.971f},
                {"Br", 79.904f},
                {"Kr", 83.798f},
                {"Rb", 85.468f},
                {"Sr", 87.62f},
                {"Y", 88.906f},
                {"Zr", 91.224f}};

            const std::string normalized = NormalizeElementSymbol(symbol);
            const auto it = kAtomicMasses.find(normalized);
            if (it != kAtomicMasses.end())
            {
                return it->second;
            }

            return 1.0f;
        }

        bool OpenNativeFilesDialog(std::vector<std::string> &outPaths)
        {
#ifdef _WIN32
            constexpr DWORD kDialogBufferSize = 32u * 1024u;
            std::vector<char> pathBuffer(static_cast<std::size_t>(kDialogBufferSize), '\0');

            OPENFILENAMEA dialog = {};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = nullptr;
            dialog.lpstrFile = pathBuffer.data();
            dialog.nMaxFile = kDialogBufferSize;
            dialog.lpstrFilter = "VASP files (*.vasp;*.poscar;*.contcar)\0*.vasp;*.poscar;*.contcar\0All files (*.*)\0*.*\0";
            dialog.nFilterIndex = 2;
            dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
            dialog.lpstrDefExt = "vasp";

            if (GetOpenFileNameA(&dialog) != FALSE)
            {
                outPaths.clear();

                const char *firstEntry = pathBuffer.data();
                if (*firstEntry == '\0')
                {
                    return false;
                }

                const char *secondEntry = firstEntry + std::strlen(firstEntry) + 1;
                if (*secondEntry == '\0')
                {
                    outPaths.emplace_back(firstEntry);
                    return true;
                }

                const std::string directory = firstEntry;
                const char *cursor = secondEntry;
                while (*cursor != '\0')
                {
                    outPaths.push_back(directory + "\\" + cursor);
                    cursor += std::strlen(cursor) + 1;
                }

                return !outPaths.empty();
            }

            return false;
#else
            (void)outPaths;
            return false;
#endif
        }

        bool OpenNativeFileDialog(std::string &outPath)
        {
            std::vector<std::string> paths;
            if (!OpenNativeFilesDialog(paths) || paths.empty())
            {
                return false;
            }

            outPath = paths.front();
            return true;
        }

        bool SaveNativeFileDialog(std::string &outPath)
        {
#ifdef _WIN32
            char pathBuffer[MAX_PATH] = "CONTCAR.vasp";

            OPENFILENAMEA dialog = {};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = nullptr;
            dialog.lpstrFile = pathBuffer;
            dialog.nMaxFile = static_cast<DWORD>(sizeof(pathBuffer));
            dialog.lpstrFilter = "VASP files (*.vasp;*.poscar;*.contcar)\0*.vasp;*.poscar;*.contcar\0All files (*.*)\0*.*\0";
            dialog.nFilterIndex = 1;
            dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;
            dialog.lpstrDefExt = "vasp";

            if (GetSaveFileNameA(&dialog) != FALSE)
            {
                outPath = pathBuffer;
                return true;
            }

            return false;
#else
            (void)outPath;
            return false;
#endif
        }

        bool SaveNativeImageDialog(std::string &outPath)
        {
#ifdef _WIN32
            char pathBuffer[MAX_PATH] = "exports/render.png";

            OPENFILENAMEA dialog = {};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = nullptr;
            dialog.lpstrFile = pathBuffer;
            dialog.nMaxFile = static_cast<DWORD>(sizeof(pathBuffer));
            dialog.lpstrFilter = "PNG image (*.png)\0*.png\0JPEG image (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0All files (*.*)\0*.*\0";
            dialog.nFilterIndex = 1;
            dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;
            dialog.lpstrDefExt = "png";

            if (GetSaveFileNameA(&dialog) != FALSE)
            {
                outPath = pathBuffer;
                return true;
            }

            return false;
#else
            (void)outPath;
            return false;
#endif
        }

        bool OpenNativeYamlDialog(std::string &outPath)
        {
#ifdef _WIN32
            char pathBuffer[MAX_PATH] = "project_appearance.yaml";

            OPENFILENAMEA dialog = {};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = nullptr;
            dialog.lpstrFile = pathBuffer;
            dialog.nMaxFile = static_cast<DWORD>(sizeof(pathBuffer));
            dialog.lpstrFilter = "YAML files (*.yaml;*.yml)\0*.yaml;*.yml\0All files (*.*)\0*.*\0";
            dialog.nFilterIndex = 1;
            dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
            dialog.lpstrDefExt = "yaml";

            if (GetOpenFileNameA(&dialog) != FALSE)
            {
                outPath = pathBuffer;
                return true;
            }

            return false;
#else
            (void)outPath;
            return false;
#endif
        }

        bool SaveNativeYamlDialog(std::string &outPath)
        {
#ifdef _WIN32
            char pathBuffer[MAX_PATH] = "project_appearance.yaml";

            OPENFILENAMEA dialog = {};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = nullptr;
            dialog.lpstrFile = pathBuffer;
            dialog.nMaxFile = static_cast<DWORD>(sizeof(pathBuffer));
            dialog.lpstrFilter = "YAML files (*.yaml;*.yml)\0*.yaml;*.yml\0All files (*.*)\0*.*\0";
            dialog.nFilterIndex = 1;
            dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;
            dialog.lpstrDefExt = "yaml";

            if (GetSaveFileNameA(&dialog) != FALSE)
            {
                outPath = pathBuffer;
                return true;
            }

            return false;
#else
            (void)outPath;
            return false;
#endif
        }

        glm::vec3 ColorFromElement(const std::string &element)
        {
            std::uint32_t hash = 2166136261u;
            for (unsigned char c : element)
            {
                hash ^= static_cast<std::uint32_t>(c);
                hash *= 16777619u;
            }

            const float r = 0.35f + 0.50f * static_cast<float>((hash >> 0) & 0xFF) / 255.0f;
            const float g = 0.35f + 0.50f * static_cast<float>((hash >> 8) & 0xFF) / 255.0f;
            const float b = 0.35f + 0.50f * static_cast<float>((hash >> 16) & 0xFF) / 255.0f;
            return glm::vec3(r, g, b);
        }

        float CovalentRadiusPmByElementSymbol(const std::string &element)
        {
            static const std::unordered_map<std::string, float> kCovalentRadiusPm = {
                {"H", 31.0f}, {"He", 28.0f}, {"Li", 128.0f}, {"Be", 96.0f}, {"B", 84.0f}, {"C", 76.0f}, {"N", 71.0f}, {"O", 66.0f}, {"F", 57.0f}, {"Ne", 58.0f}, {"Na", 166.0f}, {"Mg", 141.0f}, {"Al", 121.0f}, {"Si", 111.0f}, {"P", 107.0f}, {"S", 105.0f}, {"Cl", 102.0f}, {"Ar", 106.0f}, {"K", 203.0f}, {"Ca", 176.0f}, {"Sc", 170.0f}, {"Ti", 160.0f}, {"V", 153.0f}, {"Cr", 139.0f}, {"Mn", 139.0f}, {"Fe", 132.0f}, {"Co", 126.0f}, {"Ni", 124.0f}, {"Cu", 132.0f}, {"Zn", 122.0f}, {"Ga", 122.0f}, {"Ge", 120.0f}, {"As", 119.0f}, {"Se", 120.0f}, {"Br", 120.0f}, {"Kr", 116.0f}};

            const std::string normalized = NormalizeElementSymbol(element);
            const auto it = kCovalentRadiusPm.find(normalized);
            if (it == kCovalentRadiusPm.end())
            {
                return 111.0f;
            }

            return it->second;
        }

        float ElementRadiusScale(const std::string &element)
        {
            const float baseRadiusPm = 111.0f; // Si as a neutral baseline for current default scenes.
            const float radiusPm = CovalentRadiusPmByElementSymbol(element);

            return glm::clamp(radiusPm / baseRadiusPm, 0.45f, 1.95f);
        }

        void DrawInlineHelpMarker(const char *description)
        {
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
                ImGui::TextUnformatted(description);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
    }

} // namespace ds
