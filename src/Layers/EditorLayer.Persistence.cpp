#include "Layers/EditorLayerPrivate.h"

namespace ds
{
    bool EditorLayer::LoadStructureFromPath(const std::string &path)
    {
        Structure parsed;
        std::string error;
        if (!m_PoscarParser.ParseFromFile(path, parsed, error))
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Import failed: " + error;
            LogError(m_LastStructureMessage);
            return false;
        }

        m_WorkingStructure = parsed;
        m_OriginalStructure = parsed;
        m_HasStructureLoaded = true;
        EnsureAtomNodeIds();
        m_SelectedAtomIndices.clear();
        m_OutlinerAtomSelectionAnchor.reset();
        m_SelectedBondKeys.clear();
        m_DeletedBondKeys.clear();
        m_HiddenBondKeys.clear();
        m_HiddenAtomIndices.clear();
        m_BondLabelStates.clear();
        m_AngleLabelStates.clear();
        m_AtomColorOverrides.clear();
        m_SelectedBondLabelKey = 0;
        m_AtomCollectionIndices.assign(m_WorkingStructure.atoms.size(), 0);
        EnsureAtomCollectionAssignments();
        m_ActiveCollectionIndex = 0;
        m_AutoBondsDirty = true;
        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "Imported structure from: " + path;

        if (m_Camera && !m_WorkingStructure.atoms.empty())
        {
            glm::vec3 boundsMin(std::numeric_limits<float>::max());
            glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

            for (const Atom &atom : m_WorkingStructure.atoms)
            {
                glm::vec3 position = atom.position;
                if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
                {
                    position = m_WorkingStructure.DirectToCartesian(position);
                }

                boundsMin = glm::min(boundsMin, position);
                boundsMax = glm::max(boundsMax, position);
            }

            const glm::vec3 center = 0.5f * (boundsMin + boundsMax);
            m_SceneSettings.gridOrigin = glm::vec3(center.x, center.y, 0.0f);

            const float halfSpanXZ = 0.5f * std::max(boundsMax.x - boundsMin.x, boundsMax.z - boundsMin.z);
            const int recommendedHalfExtent = static_cast<int>(std::ceil(halfSpanXZ / std::max(0.1f, m_SceneSettings.gridSpacing))) + 2;
            m_SceneSettings.gridHalfExtent = std::max(m_SceneSettings.gridHalfExtent, recommendedHalfExtent);

            m_Camera->FrameBounds(boundsMin, boundsMax);
        }

        LogInfo(m_LastStructureMessage + " (atoms=" + std::to_string(m_WorkingStructure.GetAtomCount()) + ")");
        return true;
    }

    bool EditorLayer::AppendStructureFromPathAsCollection(const std::string &path)
    {
        if (!m_HasStructureLoaded)
        {
            const bool loaded = LoadStructureFromPath(path);
            if (loaded && !m_Collections.empty())
            {
                const std::string stem = std::filesystem::path(path).stem().string();
                m_Collections[0].name = stem.empty() ? "Collection 1" : stem;
                m_ActiveCollectionIndex = 0;
            }
            return loaded;
        }

        Structure parsed;
        std::string error;
        if (!m_PoscarParser.ParseFromFile(path, parsed, error))
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Append import failed: " + error;
            LogError(m_LastStructureMessage);
            return false;
        }

        if (parsed.atoms.empty())
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Append import failed: file has no atoms.";
            LogWarn(m_LastStructureMessage);
            return false;
        }

        SceneCollection collection;
        collection.id = GenerateSceneUUID();
        const std::string stem = std::filesystem::path(path).stem().string();
        collection.name = stem.empty() ? ("Collection " + std::to_string(m_CollectionCounter++)) : stem;
        m_Collections.push_back(collection);
        const int collectionIndex = static_cast<int>(m_Collections.size()) - 1;
        m_ActiveCollectionIndex = collectionIndex;

        std::size_t appendedCount = 0;
        for (const Atom &sourceAtom : parsed.atoms)
        {
            Atom atom = sourceAtom;
            glm::vec3 cart = sourceAtom.position;
            if (parsed.coordinateMode == CoordinateMode::Direct)
            {
                cart = parsed.DirectToCartesian(sourceAtom.position);
            }

            if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
            {
                atom.position = m_WorkingStructure.CartesianToDirect(cart);
            }
            else
            {
                atom.position = cart;
            }

            m_WorkingStructure.atoms.push_back(atom);
            m_AtomNodeIds.push_back(GenerateSceneUUID());
            m_AtomCollectionIndices.push_back(collectionIndex);
            ++appendedCount;
        }

        m_WorkingStructure.RebuildSpeciesFromAtoms();
        m_AutoBondsDirty = true;
        m_SelectedAtomIndices.clear();
        m_OutlinerAtomSelectionAnchor.reset();
        m_SelectedBondKeys.clear();
        m_SelectedBondLabelKey = 0;
        EnsureAtomCollectionAssignments();

        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "Appended " + std::to_string(appendedCount) + " atom(s) from: " + path +
                                 " into collection '" + m_Collections[static_cast<std::size_t>(collectionIndex)].name + "'.";
        LogInfo(m_LastStructureMessage);
        return true;
    }

    bool EditorLayer::ExportStructureToPath(const std::string &path, CoordinateMode mode, int precision)
    {
        if (!m_HasStructureLoaded)
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Export failed: no structure loaded.";
            LogWarn(m_LastStructureMessage);
            return false;
        }

        PoscarWriteOptions options;
        options.coordinateMode = mode;
        options.precision = precision;

        std::string error;
        if (!m_PoscarSerializer.WriteToFile(m_WorkingStructure, path, options, error))
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Export failed: " + error;
            LogError(m_LastStructureMessage);
            return false;
        }

        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "Exported structure to: " + path;
        LogInfo(m_LastStructureMessage);
        return true;
    }

    bool EditorLayer::AddAtomToStructure(const std::string &elementSymbol, const glm::vec3 &position, CoordinateMode inputMode)
    {
        if (!m_HasStructureLoaded)
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Add atom failed: no structure loaded.";
            LogWarn(m_LastStructureMessage);
            return false;
        }

        const std::string symbol = NormalizeElementSymbol(elementSymbol);
        if (symbol.empty())
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Add atom failed: invalid element symbol.";
            LogWarn(m_LastStructureMessage);
            return false;
        }

        Atom atom;
        atom.element = symbol;
        atom.selectiveDynamics = false;
        atom.selectiveFlags = {true, true, true};

        if (inputMode == m_WorkingStructure.coordinateMode)
        {
            atom.position = position;
        }
        else if (inputMode == CoordinateMode::Direct)
        {
            atom.position = m_WorkingStructure.DirectToCartesian(position);
        }
        else
        {
            atom.position = m_WorkingStructure.CartesianToDirect(position);
        }

        m_WorkingStructure.atoms.push_back(atom);
        m_AtomNodeIds.push_back(GenerateSceneUUID());
        const int collectionIndex = (m_ActiveCollectionIndex >= 0 && m_ActiveCollectionIndex < static_cast<int>(m_Collections.size()))
                                        ? m_ActiveCollectionIndex
                                        : 0;
        m_AtomCollectionIndices.push_back(collectionIndex);
        m_WorkingStructure.RebuildSpeciesFromAtoms();
        m_AutoBondsDirty = true;

        m_SelectedAtomIndices.clear();
        m_SelectedAtomIndices.push_back(m_WorkingStructure.atoms.size() - 1);

        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "Added atom " + symbol + " at (" +
                                 std::to_string(position.x) + ", " +
                                 std::to_string(position.y) + ", " +
                                 std::to_string(position.z) + ") [" +
                                 (inputMode == CoordinateMode::Direct ? "Direct" : "Cartesian") + "]";
        LogInfo(m_LastStructureMessage + ", atom count=" + std::to_string(m_WorkingStructure.GetAtomCount()));
        return true;
    }

    bool EditorLayer::ApplyElementToSelectedAtoms(const std::string &elementInput, std::size_t *outChangedCount)
    {
        if (!m_HasStructureLoaded || m_SelectedAtomIndices.empty())
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Change atom type failed: no selected atoms.";
            return false;
        }

        const std::string symbol = NormalizeElementSymbol(elementInput);
        if (symbol.empty())
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Change atom type failed: invalid element symbol.";
            return false;
        }

        std::size_t changedCount = 0;
        for (std::size_t atomIndex : m_SelectedAtomIndices)
        {
            if (atomIndex >= m_WorkingStructure.atoms.size())
            {
                continue;
            }

            Atom &atom = m_WorkingStructure.atoms[atomIndex];
            if (atom.element == symbol)
            {
                continue;
            }

            atom.element = symbol;
            ++changedCount;
        }

        if (changedCount == 0)
        {
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Change atom type: selection already uses " + symbol + ".";
            if (outChangedCount)
            {
                *outChangedCount = 0;
            }
            return true;
        }

        m_WorkingStructure.RebuildSpeciesFromAtoms();
        m_AutoBondsDirty = true;
        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "Changed atom type to " + symbol + " for " + std::to_string(changedCount) + " atom(s).";
        if (outChangedCount)
        {
            *outChangedCount = changedCount;
        }
        return true;
    }

    void EditorLayer::RebuildAutoBonds(const std::vector<glm::vec3> &atomCartesianPositions)
    {
        DS_PROFILE_SCOPE_N("EditorLayer::RebuildAutoBonds");
        m_GeneratedBonds.clear();
        std::unordered_set<std::uint64_t> seenLabelKeys;

        if (!m_HasStructureLoaded)
        {
            return;
        }

        const std::size_t atomCount = std::min(atomCartesianPositions.size(), m_WorkingStructure.atoms.size());
        if (atomCount < 2 && m_ManualBondKeys.empty())
        {
            if (m_LastLoggedBondCount != 0)
            {
                m_LastLoggedBondCount = 0;
                LogTrace("Auto bond generation skipped: fewer than 2 atoms.");
            }
            return;
        }

        const float thresholdScale = glm::clamp(m_BondThresholdScale, 0.80f, 1.80f);
        constexpr float kPmToAngstrom = 0.01f;
        constexpr float kMinBondDistance = 0.08f;
        constexpr std::size_t kMaxBondCount = 40000;

        m_GeneratedBonds.reserve(std::min<std::size_t>(atomCount * 8 + m_ManualBondKeys.size(), kMaxBondCount));

        if (m_AutoBondGenerationEnabled)
        {
            for (std::size_t i = 0; i < atomCount; ++i)
            {
                const Atom &atomA = m_WorkingStructure.atoms[i];
                const float radiusA = CovalentRadiusPmByElementSymbol(atomA.element);

                for (std::size_t j = i + 1; j < atomCount; ++j)
                {
                    const Atom &atomB = m_WorkingStructure.atoms[j];
                    const float radiusB = CovalentRadiusPmByElementSymbol(atomB.element);

                    const glm::vec3 delta = atomCartesianPositions[j] - atomCartesianPositions[i];
                    const float distance = glm::length(delta);
                    if (distance < kMinBondDistance)
                    {
                        continue;
                    }

                    const float pairScale = ResolveBondThresholdScaleForPair(atomA.element, atomB.element);
                    const float cutoff = (radiusA + radiusB) * kPmToAngstrom * pairScale;
                    if (distance > cutoff)
                    {
                        continue;
                    }

                    BondSegment bond;
                    bond.atomA = i;
                    bond.atomB = j;
                    bond.start = atomCartesianPositions[i];
                    bond.end = atomCartesianPositions[j];
                    bond.midpoint = (bond.start + bond.end) * 0.5f;
                    bond.length = distance;
                    m_GeneratedBonds.push_back(bond);

                    const std::uint64_t labelKey = MakeBondPairKey(i, j);
                    seenLabelKeys.insert(labelKey);
                    BondLabelState &labelState = m_BondLabelStates[labelKey];
                    labelState.atomA = i;
                    labelState.atomB = j;
                    labelState.scale = glm::clamp(labelState.scale, 0.25f, 4.0f);

                    if (m_GeneratedBonds.size() >= kMaxBondCount)
                    {
                        if (m_LastLoggedBondCount != m_GeneratedBonds.size())
                        {
                            m_LastLoggedBondCount = m_GeneratedBonds.size();
                            LogWarn("Auto bond generation reached hard cap of " + std::to_string(kMaxBondCount) + " bonds.");
                        }
                        return;
                    }
                }
            }
        }

        for (auto manualIt = m_ManualBondKeys.begin(); manualIt != m_ManualBondKeys.end();)
        {
            const std::size_t atomA = static_cast<std::size_t>((*manualIt >> 32) & 0xffffffffull);
            const std::size_t atomB = static_cast<std::size_t>(*manualIt & 0xffffffffull);
            if (atomA >= atomCount || atomB >= atomCount || atomA == atomB)
            {
                manualIt = m_ManualBondKeys.erase(manualIt);
                continue;
            }

            const glm::vec3 start = atomCartesianPositions[atomA];
            const glm::vec3 end = atomCartesianPositions[atomB];
            const float distance = glm::length(end - start);
            if (distance < kMinBondDistance)
            {
                ++manualIt;
                continue;
            }

            const std::uint64_t manualKey = MakeBondPairKey(atomA, atomB);
            if (seenLabelKeys.find(manualKey) == seenLabelKeys.end())
            {
                BondSegment manualBond;
                manualBond.atomA = atomA;
                manualBond.atomB = atomB;
                manualBond.start = start;
                manualBond.end = end;
                manualBond.midpoint = (start + end) * 0.5f;
                manualBond.length = distance;
                m_GeneratedBonds.push_back(manualBond);

                BondLabelState &labelState = m_BondLabelStates[manualKey];
                labelState.atomA = atomA;
                labelState.atomB = atomB;
                labelState.scale = glm::clamp(labelState.scale, 0.25f, 4.0f);
                labelState.deleted = false;
                labelState.hidden = false;
            }

            seenLabelKeys.insert(manualKey);

            if (m_GeneratedBonds.size() >= kMaxBondCount)
            {
                if (m_LastLoggedBondCount != m_GeneratedBonds.size())
                {
                    m_LastLoggedBondCount = m_GeneratedBonds.size();
                    LogWarn("Bond generation reached hard cap of " + std::to_string(kMaxBondCount) + " bonds.");
                }
                return;
            }

            ++manualIt;
        }

        for (auto it = m_BondLabelStates.begin(); it != m_BondLabelStates.end();)
        {
            if (seenLabelKeys.find(it->first) == seenLabelKeys.end())
            {
                if (m_SelectedBondLabelKey == it->first)
                {
                    m_SelectedBondLabelKey = 0;
                }
                it = m_BondLabelStates.erase(it);
                continue;
            }
            ++it;
        }

        for (auto it = m_SelectedBondKeys.begin(); it != m_SelectedBondKeys.end();)
        {
            if (seenLabelKeys.find(*it) == seenLabelKeys.end())
            {
                it = m_SelectedBondKeys.erase(it);
                continue;
            }
            ++it;
        }

        for (auto it = m_DeletedBondKeys.begin(); it != m_DeletedBondKeys.end();)
        {
            if (seenLabelKeys.find(*it) == seenLabelKeys.end())
            {
                it = m_DeletedBondKeys.erase(it);
                continue;
            }
            ++it;
        }

        for (auto it = m_HiddenBondKeys.begin(); it != m_HiddenBondKeys.end();)
        {
            if (seenLabelKeys.find(*it) == seenLabelKeys.end())
            {
                it = m_HiddenBondKeys.erase(it);
                continue;
            }
            ++it;
        }

        if (m_LastLoggedBondCount != m_GeneratedBonds.size())
        {
            m_LastLoggedBondCount = m_GeneratedBonds.size();
            LogTrace("Auto bond generation updated: bonds=" + std::to_string(m_GeneratedBonds.size()) +
                     ", atoms=" + std::to_string(atomCount) +
                     ", threshold_scale=" + std::to_string(thresholdScale));
        }
    }

    void EditorLayer::ApplyFontScale(float scale)
    {
        if (scale < 0.7f)
            scale = 0.7f;
        if (scale > 2.0f)
            scale = 2.0f;

        m_FontScale = scale;
        ImGui::GetIO().FontGlobalScale = m_FontScale;
    }

    void EditorLayer::ApplyCameraSensitivity()
    {
        if (!m_Camera)
        {
            return;
        }

        m_Camera->SetSensitivity(
            m_CameraOrbitSensitivity * kBaseOrbitSensitivity,
            m_CameraPanSensitivity * kBasePanSensitivity,
            m_CameraZoomSensitivity * kBaseZoomSensitivity);
    }

    const char *EditorLayer::ThemeName(ThemePreset preset) const
    {
        switch (preset)
        {
        case ThemePreset::Dark:
            return "Dark";
        case ThemePreset::Light:
            return "Light";
        case ThemePreset::Classic:
            return "Classic";
        case ThemePreset::PhotoshopStyle:
            return "PhotoshopStyle";
        case ThemePreset::WarmSlate:
            return "WarmSlate";
        default:
            return "Dark";
        }
    }

    void EditorLayer::ApplyTheme(ThemePreset preset)
    {
        ImGuiStyle &style = ImGui::GetStyle();

        switch (preset)
        {
        case ThemePreset::Dark:
        {
            ImGui::StyleColorsDark();
            break;
        }
        case ThemePreset::Light:
        {
            ImGui::StyleColorsLight();
            break;
        }
        case ThemePreset::Classic:
        {
            ImGui::StyleColorsClassic();
            break;
        }
        case ThemePreset::PhotoshopStyle:
        {
            ImGui::StyleColorsDark();
            style.FrameRounding = 3.0f;
            style.WindowRounding = 2.0f;
            style.GrabRounding = 2.0f;
            style.ScrollbarRounding = 2.0f;

            ImVec4 *colors = style.Colors;
            colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.16f, 0.17f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.23f, 0.39f, 0.55f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.46f, 0.64f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.34f, 0.48f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.24f, 0.41f, 0.59f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.29f, 0.49f, 0.69f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.35f, 0.51f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.19f, 0.20f, 0.22f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.26f, 0.29f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.30f, 0.33f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.21f, 0.25f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.27f, 0.41f, 0.56f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.23f, 0.35f, 0.48f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.36f, 0.67f, 0.97f, 1.00f);
            break;
        }
        case ThemePreset::WarmSlate:
        {
            ImGui::StyleColorsDark();
            style.FrameRounding = 5.0f;
            style.WindowRounding = 4.0f;

            ImVec4 *colors = style.Colors;
            colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.10f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.54f, 0.34f, 0.22f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.63f, 0.41f, 0.27f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.47f, 0.29f, 0.19f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.49f, 0.31f, 0.20f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.60f, 0.39f, 0.25f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.44f, 0.27f, 0.18f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.74f, 0.39f, 1.00f);
            break;
        }
        default:
        {
            ImGui::StyleColorsDark();
            break;
        }
        }
    }

    void EditorLayer::LoadSettings()
    {
        std::ifstream in(kSettingsPath);
        if (!in.is_open())
        {
            return;
        }

        bool hasSensitivityMode = false;
        bool relativeSensitivity = true;

        std::string line;
        while (std::getline(in, line))
        {
            const std::size_t sep = line.find('=');
            if (sep == std::string::npos)
            {
                continue;
            }

            const std::string key = line.substr(0, sep);
            const std::string value = line.substr(sep + 1);

            if (key == "theme")
            {
                if (value == "Dark")
                    m_CurrentTheme = ThemePreset::Dark;
                else if (value == "Light")
                    m_CurrentTheme = ThemePreset::Light;
                else if (value == "Classic")
                    m_CurrentTheme = ThemePreset::Classic;
                else if (value == "PhotoshopStyle")
                    m_CurrentTheme = ThemePreset::PhotoshopStyle;
                else if (value == "WarmSlate")
                    m_CurrentTheme = ThemePreset::WarmSlate;
            }
            else if (key == "show_demo_window")
            {
                m_ShowDemoWindow = (value == "1");
            }
            else if (key == "show_log_panel")
            {
                m_ShowLogPanel = (value == "1");
            }
            else if (key == "show_tools_panel")
            {
                m_ShowActionsPanel = (value == "1");
            }
            else if (key == "show_actions_panel")
            {
                m_ShowActionsPanel = (value == "1");
            }
            else if (key == "show_appearance_panel")
            {
                m_ShowAppearancePanel = (value == "1");
            }
            else if (key == "show_settings_panel")
            {
                m_ShowSettingsPanel = (value == "1");
            }
            else if (key == "font_scale")
            {
                try
                {
                    ApplyFontScale(std::stof(value));
                }
                catch (...)
                {
                    ApplyFontScale(1.0f);
                }
            }
            else if (key == "log_filter")
            {
                try
                {
                    m_LogFilter = std::stoi(value);
                }
                catch (...)
                {
                    m_LogFilter = 0;
                }
            }
            else if (key == "log_auto_scroll")
            {
                m_LogAutoScroll = (value == "1");
            }
            else if (key == "camera_orbit_sensitivity")
            {
                try
                {
                    m_CameraOrbitSensitivity = std::stof(value);
                }
                catch (...)
                {
                    m_CameraOrbitSensitivity = 1.0f;
                }
            }
            else if (key == "camera_pan_sensitivity")
            {
                try
                {
                    m_CameraPanSensitivity = std::stof(value);
                }
                catch (...)
                {
                    m_CameraPanSensitivity = 1.0f;
                }
            }
            else if (key == "camera_zoom_sensitivity")
            {
                try
                {
                    m_CameraZoomSensitivity = std::stof(value);
                }
                catch (...)
                {
                    m_CameraZoomSensitivity = 1.0f;
                }
            }
            else if (key == "camera_target_x")
            {
                try
                {
                    m_CameraTargetPersisted.x = std::stof(value);
                    m_HasPersistedCameraState = true;
                }
                catch (...)
                {
                }
            }
            else if (key == "camera_target_y")
            {
                try
                {
                    m_CameraTargetPersisted.y = std::stof(value);
                    m_HasPersistedCameraState = true;
                }
                catch (...)
                {
                }
            }
            else if (key == "camera_target_z")
            {
                try
                {
                    m_CameraTargetPersisted.z = std::stof(value);
                    m_HasPersistedCameraState = true;
                }
                catch (...)
                {
                }
            }
            else if (key == "camera_distance")
            {
                try
                {
                    m_CameraDistancePersisted = std::stof(value);
                    m_HasPersistedCameraState = true;
                }
                catch (...)
                {
                }
            }
            else if (key == "camera_yaw")
            {
                try
                {
                    m_CameraYawPersisted = std::stof(value);
                    m_HasPersistedCameraState = true;
                }
                catch (...)
                {
                }
            }
            else if (key == "camera_pitch")
            {
                try
                {
                    m_CameraPitchPersisted = std::stof(value);
                    m_HasPersistedCameraState = true;
                }
                catch (...)
                {
                }
            }
            else if (key == "camera_roll")
            {
                try
                {
                    m_CameraRollPersisted = std::stof(value);
                    m_HasPersistedCameraState = true;
                }
                catch (...)
                {
                }
            }
            else if (key == "camera_sensitivity_mode")
            {
                hasSensitivityMode = true;
                relativeSensitivity = (value == "relative_v2");
            }
            else if (key == "viewport_bg_r")
            {
                try
                {
                    m_SceneSettings.clearColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bg_g")
            {
                try
                {
                    m_SceneSettings.clearColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bg_b")
            {
                try
                {
                    m_SceneSettings.clearColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_enabled")
            {
                m_SceneSettings.drawGrid = (value == "1");
            }
            else if (key == "viewport_cell_edges")
            {
                m_ShowCellEdges = (value == "1");
            }
            else if (key == "viewport_grid_extent")
            {
                try
                {
                    m_SceneSettings.gridHalfExtent = std::stoi(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_spacing")
            {
                try
                {
                    m_SceneSettings.gridSpacing = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_line_width")
            {
                try
                {
                    m_SceneSettings.gridLineWidth = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_origin_x")
            {
                try
                {
                    m_SceneSettings.gridOrigin.x = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_origin_y")
            {
                try
                {
                    m_SceneSettings.gridOrigin.y = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_origin_z")
            {
                try
                {
                    m_SceneSettings.gridOrigin.z = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_color_r")
            {
                try
                {
                    m_SceneSettings.gridColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_color_g")
            {
                try
                {
                    m_SceneSettings.gridColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_color_b")
            {
                try
                {
                    m_SceneSettings.gridColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_opacity")
            {
                try
                {
                    m_SceneSettings.gridOpacity = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_cell_edge_color_r")
            {
                try
                {
                    m_CellEdgeColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_cell_edge_color_g")
            {
                try
                {
                    m_CellEdgeColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_cell_edge_color_b")
            {
                try
                {
                    m_CellEdgeColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_cell_edge_line_width")
            {
                try
                {
                    m_CellEdgeLineWidth = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_light_dir_x")
            {
                try
                {
                    m_SceneSettings.lightDirection.x = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_light_dir_y")
            {
                try
                {
                    m_SceneSettings.lightDirection.y = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_light_dir_z")
            {
                try
                {
                    m_SceneSettings.lightDirection.z = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_light_ambient")
            {
                try
                {
                    m_SceneSettings.ambientStrength = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_light_diffuse")
            {
                try
                {
                    m_SceneSettings.diffuseStrength = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_light_color_r")
            {
                try
                {
                    m_SceneSettings.lightColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_light_color_g")
            {
                try
                {
                    m_SceneSettings.lightColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_light_color_b")
            {
                try
                {
                    m_SceneSettings.lightColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_atom_scale")
            {
                try
                {
                    m_SceneSettings.atomScale = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_atom_override")
            {
                m_SceneSettings.overrideAtomColor = (value == "1");
            }
            else if (key == "viewport_atom_override_r")
            {
                try
                {
                    m_SceneSettings.atomOverrideColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_atom_override_g")
            {
                try
                {
                    m_SceneSettings.atomOverrideColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_atom_override_b")
            {
                try
                {
                    m_SceneSettings.atomOverrideColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_atom_element_colors")
            {
                m_ElementColorOverrides = ParseElementColorOverrides(value);
            }
            else if (key == "viewport_atom_brightness")
            {
                try
                {
                    m_SceneSettings.atomBrightness = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_atom_glow")
            {
                try
                {
                    m_SceneSettings.atomGlowStrength = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bonds_auto")
            {
                m_AutoBondGenerationEnabled = (value == "1");
            }
            else if (key == "viewport_bonds_labels")
            {
                m_ShowBondLengthLabels = (value == "1");
            }
            else if (key == "viewport_bonds_render_style")
            {
                try
                {
                    const int mode = std::stoi(value);
                    m_BondRenderStyle = static_cast<BondRenderStyle>(std::clamp(mode, 0, 2));
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bond_label_delete_only")
            {
                m_BondLabelDeleteOnlyMode = (value == "1");
            }
            else if (key == "viewport_bonds_threshold_scale")
            {
                try
                {
                    m_BondThresholdScale = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bonds_pair_override_enabled")
            {
                m_BondUsePairThresholdOverrides = (value == "1");
            }
            else if (key == "viewport_bond_pair_scales")
            {
                m_BondPairThresholdScaleOverrides = ParsePairScaleOverrides(value);
            }
            else if (key == "viewport_bonds_line_width")
            {
                try
                {
                    m_BondLineWidth = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bonds_line_width_min")
            {
                try
                {
                    m_BondLineWidthMin = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bonds_line_width_max")
            {
                try
                {
                    m_BondLineWidthMax = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bonds_color_r")
            {
                try
                {
                    m_BondColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bonds_color_g")
            {
                try
                {
                    m_BondColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bonds_color_b")
            {
                try
                {
                    m_BondColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bonds_selected_color_r")
            {
                try
                {
                    m_BondSelectedColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bonds_selected_color_g")
            {
                try
                {
                    m_BondSelectedColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bonds_selected_color_b")
            {
                try
                {
                    m_BondSelectedColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "selection_color_r")
            {
                try
                {
                    m_SelectionColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "selection_color_g")
            {
                try
                {
                    m_SelectionColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "selection_color_b")
            {
                try
                {
                    m_SelectionColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "selection_outline_thickness")
            {
                try
                {
                    m_SelectionOutlineThickness = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "selection_debug_to_file")
            {
                m_SelectionDebugToFile = (value == "1");
            }
            else if (key == "viewport_render_scale")
            {
                try
                {
                    m_ViewportRenderScale = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_projection_mode")
            {
                try
                {
                    m_ProjectionModeIndex = std::stoi(value);
                }
                catch (...)
                {
                    m_ProjectionModeIndex = 0;
                }
            }
            else if (key == "viewport_cursor_show")
            {
                m_Show3DCursor = (value == "1");
            }
            else if (key == "viewport_cursor_x")
            {
                try
                {
                    m_CursorPosition.x = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_cursor_y")
            {
                try
                {
                    m_CursorPosition.y = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_cursor_z")
            {
                try
                {
                    m_CursorPosition.z = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "scene_origin_x")
            {
                try
                {
                    m_SceneOriginPosition.x = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "scene_origin_y")
            {
                try
                {
                    m_SceneOriginPosition.y = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "scene_origin_z")
            {
                try
                {
                    m_SceneOriginPosition.z = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "light_position_x")
            {
                try
                {
                    m_LightPosition.x = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "light_position_y")
            {
                try
                {
                    m_LightPosition.y = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "light_position_z")
            {
                try
                {
                    m_LightPosition.z = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_cursor_scale")
            {
                try
                {
                    m_CursorVisualScale = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_cursor_snap")
            {
                m_CursorSnapToGrid = (value == "1");
            }
            else if (key == "viewport_touchpad_navigation")
            {
                m_TouchpadNavigationEnabled = (value == "1");
            }
            else if (key == "viewport_clean_view_mode")
            {
                m_CleanViewMode = (value == "1");
            }
            else if (key == "viewport_append_import_collection")
            {
                m_AppendImportToNewCollection = (value == "1");
            }
            else if (key == "viewport_measurement_show")
            {
                m_ShowSelectionMeasurements = (value == "1");
            }
            else if (key == "viewport_measurement_distance")
            {
                m_ShowSelectionDistanceMeasurement = (value == "1");
            }
            else if (key == "viewport_measurement_angle")
            {
                m_ShowSelectionAngleMeasurement = (value == "1");
            }
            else if (key == "viewport_static_angle_labels")
            {
                m_ShowStaticAngleLabels = (value == "1");
            }
            else if (key == "viewport_measurement_precision")
            {
                try
                {
                    m_MeasurementPrecision = std::stoi(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_measurement_text_color_r")
            {
                try
                {
                    m_MeasurementTextColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_measurement_text_color_g")
            {
                try
                {
                    m_MeasurementTextColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_measurement_text_color_b")
            {
                try
                {
                    m_MeasurementTextColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_measurement_bg_color_r")
            {
                try
                {
                    m_MeasurementBackgroundColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_measurement_bg_color_g")
            {
                try
                {
                    m_MeasurementBackgroundColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_measurement_bg_color_b")
            {
                try
                {
                    m_MeasurementBackgroundColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_view_gizmo")
            {
                m_ViewGuizmoEnabled = (value == "1");
            }
            else if (key == "viewport_view_gizmo_drag_mode")
            {
                m_ViewGizmoDragMode = (value == "1");
            }
            else if (key == "viewport_transform_gizmo_size")
            {
                try
                {
                    m_TransformGizmoSize = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_view_gizmo_scale")
            {
                try
                {
                    m_ViewGizmoScale = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_view_gizmo_offset_right")
            {
                try
                {
                    m_ViewGizmoOffsetRight = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_view_gizmo_offset_top")
            {
                try
                {
                    m_ViewGizmoOffsetTop = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_rotate_step_deg")
            {
                try
                {
                    m_ViewportRotateStepDeg = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_fallback_marker_scale")
            {
                try
                {
                    m_FallbackGizmoVisualScale = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_show_transform_empties")
            {
                m_ShowTransformEmpties = (value == "1");
            }
            else if (key == "viewport_transform_empty_visual_scale")
            {
                try
                {
                    m_TransformEmptyVisualScale = std::stof(value);
                }
                catch (...)
                {
                }
            }
            // Keep this tail section flat; MSVC can hit parser nesting limits on very long else-if chains.
            if (key == "viewport_show_global_axes_overlay")
            {
                m_ShowGlobalAxesOverlay = (value == "1");
            }
            if (key == "viewport_show_global_axis_x")
            {
                m_ShowGlobalAxis[0] = (value != "0");
            }
            if (key == "viewport_show_global_axis_y")
            {
                m_ShowGlobalAxis[1] = (value != "0");
            }
            if (key == "viewport_show_global_axis_z")
            {
                m_ShowGlobalAxis[2] = (value != "0");
            }
            if (key == "gizmo_use_temporary_local_axes")
            {
                m_UseTemporaryLocalAxes = (value == "1");
            }
            if (key == "gizmo_temp_axes_source")
            {
                try
                {
                    const int mode = std::stoi(value);
                    m_TemporaryAxesSource = (mode == 1) ? TemporaryAxesSource::ActiveEmpty : TemporaryAxesSource::SelectionAtoms;
                }
                catch (...)
                {
                    m_TemporaryAxesSource = TemporaryAxesSource::SelectionAtoms;
                }
            }
            if (key == "gizmo_temp_axis_atom_a")
            {
                try
                {
                    m_TemporaryAxisAtomA = std::stoi(value);
                }
                catch (...)
                {
                }
            }
            if (key == "gizmo_temp_axis_atom_b")
            {
                try
                {
                    m_TemporaryAxisAtomB = std::stoi(value);
                }
                catch (...)
                {
                }
            }
            if (key == "gizmo_temp_axis_atom_c")
            {
                try
                {
                    m_TemporaryAxisAtomC = std::stoi(value);
                }
                catch (...)
                {
                }
            }
            if (key == "gizmo_axis_color_x_r")
            {
                try
                {
                    m_AxisColors[0].r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "gizmo_axis_color_x_g")
            {
                try
                {
                    m_AxisColors[0].g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "gizmo_axis_color_x_b")
            {
                try
                {
                    m_AxisColors[0].b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "gizmo_axis_color_y_r")
            {
                try
                {
                    m_AxisColors[1].r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "gizmo_axis_color_y_g")
            {
                try
                {
                    m_AxisColors[1].g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "gizmo_axis_color_y_b")
            {
                try
                {
                    m_AxisColors[1].b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "gizmo_axis_color_z_r")
            {
                try
                {
                    m_AxisColors[2].r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "gizmo_axis_color_z_g")
            {
                try
                {
                    m_AxisColors[2].g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "gizmo_axis_color_z_b")
            {
                try
                {
                    m_AxisColors[2].b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "ui_spacing_scale")
            {
                try
                {
                    m_UiSpacingScale = std::stof(value);
                }
                catch (...)
                {
                }
            }
        }

        if (!hasSensitivityMode || !relativeSensitivity)
        {
            // Legacy migration: old settings stored absolute values.
            m_CameraOrbitSensitivity = m_CameraOrbitSensitivity / kBaseOrbitSensitivity;
            m_CameraPanSensitivity = m_CameraPanSensitivity / kBasePanSensitivity;
            m_CameraZoomSensitivity = m_CameraZoomSensitivity / kBaseZoomSensitivity;
        }

        if (m_CameraOrbitSensitivity < 0.05f)
            m_CameraOrbitSensitivity = 0.05f;
        if (m_CameraPanSensitivity < 0.05f)
            m_CameraPanSensitivity = 0.05f;
        if (m_CameraZoomSensitivity < 0.05f)
            m_CameraZoomSensitivity = 0.05f;

        if (m_ProjectionModeIndex < 0)
            m_ProjectionModeIndex = 0;
        if (m_ProjectionModeIndex > 1)
            m_ProjectionModeIndex = 1;

        if (m_SelectionOutlineThickness < 1.0f)
            m_SelectionOutlineThickness = 1.0f;
        if (m_SelectionOutlineThickness > 8.0f)
            m_SelectionOutlineThickness = 8.0f;
        if (m_CursorVisualScale < 0.05f)
            m_CursorVisualScale = 0.05f;
        if (m_CursorVisualScale > 2.0f)
            m_CursorVisualScale = 2.0f;
        if (m_ViewportRenderScale < 0.25f)
            m_ViewportRenderScale = 0.25f;
        if (m_ViewportRenderScale > 1.0f)
            m_ViewportRenderScale = 1.0f;
        if (m_TransformGizmoSize < 0.05f)
            m_TransformGizmoSize = 0.05f;
        if (m_TransformGizmoSize > 0.35f)
            m_TransformGizmoSize = 0.35f;
        if (m_ViewGizmoScale < 0.35f)
            m_ViewGizmoScale = 0.35f;
        if (m_ViewGizmoScale > 2.20f)
            m_ViewGizmoScale = 2.20f;
        if (m_ViewGizmoOffsetRight < 0.0f)
            m_ViewGizmoOffsetRight = 0.0f;
        if (m_ViewGizmoOffsetTop < 0.0f)
            m_ViewGizmoOffsetTop = 0.0f;
        if (m_ViewportRotateStepDeg < 0.1f)
            m_ViewportRotateStepDeg = 0.1f;
        if (m_ViewportRotateStepDeg > 180.0f)
            m_ViewportRotateStepDeg = 180.0f;
        if (m_FallbackGizmoVisualScale < 0.5f)
            m_FallbackGizmoVisualScale = 0.5f;
        if (m_FallbackGizmoVisualScale > 6.0f)
            m_FallbackGizmoVisualScale = 6.0f;
        if (m_TransformEmptyVisualScale < 0.06f)
            m_TransformEmptyVisualScale = 0.06f;
        if (m_TransformEmptyVisualScale > 0.55f)
            m_TransformEmptyVisualScale = 0.55f;
        if (m_SceneSettings.atomGlowStrength < 0.0f)
            m_SceneSettings.atomGlowStrength = 0.0f;
        if (m_SceneSettings.atomGlowStrength > 0.60f)
            m_SceneSettings.atomGlowStrength = 0.60f;
        if (m_BondThresholdScale < 0.80f)
            m_BondThresholdScale = 0.80f;
        if (m_BondThresholdScale > 1.80f)
            m_BondThresholdScale = 1.80f;
        m_BondLineWidthMin = glm::clamp(m_BondLineWidthMin, 0.2f, 20.0f);
        m_BondLineWidthMax = glm::clamp(m_BondLineWidthMax, 0.2f, 20.0f);
        if (m_BondLineWidthMin > m_BondLineWidthMax)
            std::swap(m_BondLineWidthMin, m_BondLineWidthMax);
        m_BondLineWidth = glm::clamp(m_BondLineWidth, m_BondLineWidthMin, m_BondLineWidthMax);
        if (m_CellEdgeLineWidth < 0.5f)
            m_CellEdgeLineWidth = 0.5f;
        if (m_CellEdgeLineWidth > 10.0f)
            m_CellEdgeLineWidth = 10.0f;
        if (m_MeasurementPrecision < 0)
            m_MeasurementPrecision = 0;
        if (m_MeasurementPrecision > 6)
            m_MeasurementPrecision = 6;
        if (m_GizmoModeIndex < 0)
            m_GizmoModeIndex = 0;
        if (m_GizmoModeIndex > 2)
            m_GizmoModeIndex = 2;
        if (m_UiSpacingScale < 0.75f)
            m_UiSpacingScale = 0.75f;
        if (m_UiSpacingScale > 1.80f)
            m_UiSpacingScale = 1.80f;

        m_SceneSettings.drawCellEdges = m_ShowCellEdges;
        m_SceneSettings.cellEdgeColor = glm::clamp(m_CellEdgeColor, glm::vec3(0.0f), glm::vec3(1.0f));
        m_SceneSettings.cellEdgeLineWidth = m_CellEdgeLineWidth;

        // Keep simulation grid on the canonical XY plane.
        m_SceneSettings.gridOrigin.z = 0.0f;
    }

    void EditorLayer::SaveSettings() const
    {
        std::filesystem::create_directories("config");

        std::ofstream out(kSettingsPath, std::ios::trunc);
        if (!out.is_open())
        {
            return;
        }

        out << "theme=" << ThemeName(m_CurrentTheme) << '\n';
        out << "show_demo_window=" << (m_ShowDemoWindow ? "1" : "0") << '\n';
        out << "show_log_panel=" << (m_ShowLogPanel ? "1" : "0") << '\n';
        out << "show_actions_panel=" << (m_ShowActionsPanel ? "1" : "0") << '\n';
        out << "show_appearance_panel=" << (m_ShowAppearancePanel ? "1" : "0") << '\n';
        out << "show_settings_panel=" << (m_ShowSettingsPanel ? "1" : "0") << '\n';
        out << "font_scale=" << m_FontScale << '\n';
        out << "log_filter=" << m_LogFilter << '\n';
        out << "log_auto_scroll=" << (m_LogAutoScroll ? "1" : "0") << '\n';
        out << "camera_orbit_sensitivity=" << m_CameraOrbitSensitivity << '\n';
        out << "camera_pan_sensitivity=" << m_CameraPanSensitivity << '\n';
        out << "camera_zoom_sensitivity=" << m_CameraZoomSensitivity << '\n';
        out << "camera_sensitivity_mode=relative_v2" << '\n';
        if (m_Camera)
        {
            out << "camera_target_x=" << m_Camera->GetTarget().x << '\n';
            out << "camera_target_y=" << m_Camera->GetTarget().y << '\n';
            out << "camera_target_z=" << m_Camera->GetTarget().z << '\n';
            out << "camera_distance=" << m_Camera->GetDistance() << '\n';
            out << "camera_yaw=" << m_Camera->GetYaw() << '\n';
            out << "camera_pitch=" << m_Camera->GetPitch() << '\n';
            out << "camera_roll=" << m_Camera->GetRoll() << '\n';
        }
        else
        {
            out << "camera_target_x=" << m_CameraTargetPersisted.x << '\n';
            out << "camera_target_y=" << m_CameraTargetPersisted.y << '\n';
            out << "camera_target_z=" << m_CameraTargetPersisted.z << '\n';
            out << "camera_distance=" << m_CameraDistancePersisted << '\n';
            out << "camera_yaw=" << m_CameraYawPersisted << '\n';
            out << "camera_pitch=" << m_CameraPitchPersisted << '\n';
            out << "camera_roll=" << m_CameraRollPersisted << '\n';
        }
        out << "viewport_bg_r=" << m_SceneSettings.clearColor.r << '\n';
        out << "viewport_bg_g=" << m_SceneSettings.clearColor.g << '\n';
        out << "viewport_bg_b=" << m_SceneSettings.clearColor.b << '\n';
        out << "viewport_grid_enabled=" << (m_SceneSettings.drawGrid ? "1" : "0") << '\n';
        out << "viewport_cell_edges=" << (m_ShowCellEdges ? "1" : "0") << '\n';
        out << "viewport_grid_extent=" << m_SceneSettings.gridHalfExtent << '\n';
        out << "viewport_grid_spacing=" << m_SceneSettings.gridSpacing << '\n';
        out << "viewport_grid_line_width=" << m_SceneSettings.gridLineWidth << '\n';
        out << "viewport_grid_origin_x=" << m_SceneSettings.gridOrigin.x << '\n';
        out << "viewport_grid_origin_y=" << m_SceneSettings.gridOrigin.y << '\n';
        out << "viewport_grid_origin_z=" << 0.0f << '\n';
        out << "viewport_grid_color_r=" << m_SceneSettings.gridColor.r << '\n';
        out << "viewport_grid_color_g=" << m_SceneSettings.gridColor.g << '\n';
        out << "viewport_grid_color_b=" << m_SceneSettings.gridColor.b << '\n';
        out << "viewport_grid_opacity=" << m_SceneSettings.gridOpacity << '\n';
        out << "viewport_cell_edge_color_r=" << m_CellEdgeColor.r << '\n';
        out << "viewport_cell_edge_color_g=" << m_CellEdgeColor.g << '\n';
        out << "viewport_cell_edge_color_b=" << m_CellEdgeColor.b << '\n';
        out << "viewport_cell_edge_line_width=" << m_CellEdgeLineWidth << '\n';
        out << "viewport_light_dir_x=" << m_SceneSettings.lightDirection.x << '\n';
        out << "viewport_light_dir_y=" << m_SceneSettings.lightDirection.y << '\n';
        out << "viewport_light_dir_z=" << m_SceneSettings.lightDirection.z << '\n';
        out << "viewport_light_ambient=" << m_SceneSettings.ambientStrength << '\n';
        out << "viewport_light_diffuse=" << m_SceneSettings.diffuseStrength << '\n';
        out << "viewport_light_color_r=" << m_SceneSettings.lightColor.r << '\n';
        out << "viewport_light_color_g=" << m_SceneSettings.lightColor.g << '\n';
        out << "viewport_light_color_b=" << m_SceneSettings.lightColor.b << '\n';
        out << "viewport_atom_scale=" << m_SceneSettings.atomScale << '\n';
        out << "viewport_atom_override=" << (m_SceneSettings.overrideAtomColor ? "1" : "0") << '\n';
        out << "viewport_atom_override_r=" << m_SceneSettings.atomOverrideColor.r << '\n';
        out << "viewport_atom_override_g=" << m_SceneSettings.atomOverrideColor.g << '\n';
        out << "viewport_atom_override_b=" << m_SceneSettings.atomOverrideColor.b << '\n';
        out << "viewport_atom_element_colors=" << SerializeElementColorOverrides(m_ElementColorOverrides) << '\n';
        out << "viewport_atom_brightness=" << m_SceneSettings.atomBrightness << '\n';
        out << "viewport_atom_glow=" << m_SceneSettings.atomGlowStrength << '\n';
        out << "viewport_bonds_auto=" << (m_AutoBondGenerationEnabled ? "1" : "0") << '\n';
        out << "viewport_bonds_labels=" << (m_ShowBondLengthLabels ? "1" : "0") << '\n';
        out << "viewport_bonds_render_style=" << static_cast<int>(m_BondRenderStyle) << '\n';
        out << "viewport_bond_label_delete_only=" << (m_BondLabelDeleteOnlyMode ? "1" : "0") << '\n';
        out << "viewport_bonds_threshold_scale=" << m_BondThresholdScale << '\n';
        out << "viewport_bonds_pair_override_enabled=" << (m_BondUsePairThresholdOverrides ? "1" : "0") << '\n';
        out << "viewport_bond_pair_scales=" << SerializePairScaleOverrides(m_BondPairThresholdScaleOverrides) << '\n';
        out << "viewport_bonds_line_width=" << m_BondLineWidth << '\n';
        out << "viewport_bonds_line_width_min=" << m_BondLineWidthMin << '\n';
        out << "viewport_bonds_line_width_max=" << m_BondLineWidthMax << '\n';
        out << "viewport_bonds_color_r=" << m_BondColor.r << '\n';
        out << "viewport_bonds_color_g=" << m_BondColor.g << '\n';
        out << "viewport_bonds_color_b=" << m_BondColor.b << '\n';
        out << "viewport_bonds_selected_color_r=" << m_BondSelectedColor.r << '\n';
        out << "viewport_bonds_selected_color_g=" << m_BondSelectedColor.g << '\n';
        out << "viewport_bonds_selected_color_b=" << m_BondSelectedColor.b << '\n';
        out << "selection_color_r=" << m_SelectionColor.r << '\n';
        out << "selection_color_g=" << m_SelectionColor.g << '\n';
        out << "selection_color_b=" << m_SelectionColor.b << '\n';
        out << "selection_outline_thickness=" << m_SelectionOutlineThickness << '\n';
        out << "selection_debug_to_file=" << (m_SelectionDebugToFile ? "1" : "0") << '\n';
        out << "viewport_render_scale=" << m_ViewportRenderScale << '\n';
        out << "viewport_projection_mode=" << m_ProjectionModeIndex << '\n';
        out << "viewport_cursor_show=" << (m_Show3DCursor ? "1" : "0") << '\n';
        out << "viewport_cursor_x=" << m_CursorPosition.x << '\n';
        out << "viewport_cursor_y=" << m_CursorPosition.y << '\n';
        out << "viewport_cursor_z=" << m_CursorPosition.z << '\n';
        out << "viewport_touchpad_navigation=" << (m_TouchpadNavigationEnabled ? "1" : "0") << '\n';
        out << "viewport_clean_view_mode=" << (m_CleanViewMode ? "1" : "0") << '\n';
        out << "viewport_append_import_collection=" << (m_AppendImportToNewCollection ? "1" : "0") << '\n';
        out << "viewport_measurement_show=" << (m_ShowSelectionMeasurements ? "1" : "0") << '\n';
        out << "viewport_measurement_distance=" << (m_ShowSelectionDistanceMeasurement ? "1" : "0") << '\n';
        out << "viewport_measurement_angle=" << (m_ShowSelectionAngleMeasurement ? "1" : "0") << '\n';
        out << "viewport_static_angle_labels=" << (m_ShowStaticAngleLabels ? "1" : "0") << '\n';
        out << "viewport_measurement_precision=" << m_MeasurementPrecision << '\n';
        out << "viewport_measurement_text_color_r=" << m_MeasurementTextColor.r << '\n';
        out << "viewport_measurement_text_color_g=" << m_MeasurementTextColor.g << '\n';
        out << "viewport_measurement_text_color_b=" << m_MeasurementTextColor.b << '\n';
        out << "viewport_measurement_bg_color_r=" << m_MeasurementBackgroundColor.r << '\n';
        out << "viewport_measurement_bg_color_g=" << m_MeasurementBackgroundColor.g << '\n';
        out << "viewport_measurement_bg_color_b=" << m_MeasurementBackgroundColor.b << '\n';
        out << "scene_origin_x=" << m_SceneOriginPosition.x << '\n';
        out << "scene_origin_y=" << m_SceneOriginPosition.y << '\n';
        out << "scene_origin_z=" << m_SceneOriginPosition.z << '\n';
        out << "light_position_x=" << m_LightPosition.x << '\n';
        out << "light_position_y=" << m_LightPosition.y << '\n';
        out << "light_position_z=" << m_LightPosition.z << '\n';
        out << "viewport_cursor_scale=" << m_CursorVisualScale << '\n';
        out << "viewport_cursor_snap=" << (m_CursorSnapToGrid ? "1" : "0") << '\n';
        out << "viewport_view_gizmo=" << (m_ViewGuizmoEnabled ? "1" : "0") << '\n';
        out << "viewport_view_gizmo_drag_mode=" << (m_ViewGizmoDragMode ? "1" : "0") << '\n';
        out << "viewport_transform_gizmo_size=" << m_TransformGizmoSize << '\n';
        out << "viewport_view_gizmo_scale=" << m_ViewGizmoScale << '\n';
        out << "viewport_view_gizmo_offset_right=" << m_ViewGizmoOffsetRight << '\n';
        out << "viewport_view_gizmo_offset_top=" << m_ViewGizmoOffsetTop << '\n';
        out << "viewport_rotate_step_deg=" << m_ViewportRotateStepDeg << '\n';
        out << "viewport_fallback_marker_scale=" << m_FallbackGizmoVisualScale << '\n';
        out << "viewport_show_transform_empties=" << (m_ShowTransformEmpties ? "1" : "0") << '\n';
        out << "viewport_transform_empty_visual_scale=" << m_TransformEmptyVisualScale << '\n';
        out << "viewport_show_global_axes_overlay=" << (m_ShowGlobalAxesOverlay ? "1" : "0") << '\n';
        out << "viewport_show_global_axis_x=" << (m_ShowGlobalAxis[0] ? "1" : "0") << '\n';
        out << "viewport_show_global_axis_y=" << (m_ShowGlobalAxis[1] ? "1" : "0") << '\n';
        out << "viewport_show_global_axis_z=" << (m_ShowGlobalAxis[2] ? "1" : "0") << '\n';
        out << "gizmo_use_temporary_local_axes=" << (m_UseTemporaryLocalAxes ? "1" : "0") << '\n';
        out << "gizmo_temp_axes_source=" << (m_TemporaryAxesSource == TemporaryAxesSource::ActiveEmpty ? 1 : 0) << '\n';
        out << "gizmo_temp_axis_atom_a=" << m_TemporaryAxisAtomA << '\n';
        out << "gizmo_temp_axis_atom_b=" << m_TemporaryAxisAtomB << '\n';
        out << "gizmo_temp_axis_atom_c=" << m_TemporaryAxisAtomC << '\n';
        out << "gizmo_axis_color_x_r=" << m_AxisColors[0].r << '\n';
        out << "gizmo_axis_color_x_g=" << m_AxisColors[0].g << '\n';
        out << "gizmo_axis_color_x_b=" << m_AxisColors[0].b << '\n';
        out << "gizmo_axis_color_y_r=" << m_AxisColors[1].r << '\n';
        out << "gizmo_axis_color_y_g=" << m_AxisColors[1].g << '\n';
        out << "gizmo_axis_color_y_b=" << m_AxisColors[1].b << '\n';
        out << "gizmo_axis_color_z_r=" << m_AxisColors[2].r << '\n';
        out << "gizmo_axis_color_z_g=" << m_AxisColors[2].g << '\n';
        out << "gizmo_axis_color_z_b=" << m_AxisColors[2].b << '\n';
        out << "ui_spacing_scale=" << m_UiSpacingScale << '\n';

        SaveSceneState();
    }

    void EditorLayer::LoadSceneState()
    {
        std::ifstream in(kSceneStatePath);
        if (!in.is_open())
        {
            LogInfo("LoadSceneState: no scene state file found at config/scene_state.ini");
            return;
        }

        LogInfo("LoadSceneState: parsing config/scene_state.ini");

        std::unordered_map<std::string, std::string> values;
        std::string line;
        while (std::getline(in, line))
        {
            const std::size_t sep = line.find('=');
            if (sep == std::string::npos)
            {
                continue;
            }
            values[line.substr(0, sep)] = line.substr(sep + 1);
        }

        auto getValue = [&](const std::string &key) -> std::string
        {
            const auto it = values.find(key);
            if (it == values.end())
            {
                return std::string();
            }
            return it->second;
        };

        m_Collections.clear();
        m_TransformEmpties.clear();
        m_ObjectGroups.clear();
        m_CameraPresets.clear();
        m_AngleLabelStates.clear();
        m_SelectedCameraPresetIndex = -1;

        int atomCount = 0;
        try
        {
            atomCount = std::stoi(getValue("scene_atom_count"));
        }
        catch (...)
        {
            atomCount = 0;
        }
        if (atomCount > 0)
        {
            m_AtomNodeIds.clear();
            m_AtomNodeIds.reserve(static_cast<std::size_t>(atomCount));
            for (int i = 0; i < atomCount; ++i)
            {
                try
                {
                    m_AtomNodeIds.push_back(static_cast<SceneUUID>(std::stoull(getValue("scene_atom_" + std::to_string(i) + "_id"))));
                }
                catch (...)
                {
                    m_AtomNodeIds.push_back(0);
                }
            }

            m_AtomCollectionIndices.assign(static_cast<std::size_t>(atomCount), 0);
            const std::vector<std::string> collectionTokens = SplitCsv(getValue("scene_atom_collection_indices"));
            for (std::size_t i = 0; i < m_AtomCollectionIndices.size() && i < collectionTokens.size(); ++i)
            {
                try
                {
                    m_AtomCollectionIndices[i] = std::stoi(collectionTokens[i]);
                }
                catch (...)
                {
                    m_AtomCollectionIndices[i] = 0;
                }
            }
        }
        else
        {
            m_AtomCollectionIndices.clear();
        }

        int collectionCount = 0;
        try
        {
            collectionCount = std::stoi(getValue("scene_collection_count"));
        }
        catch (...)
        {
            collectionCount = 0;
        }

        for (int i = 0; i < collectionCount; ++i)
        {
            SceneCollection collection;
            const std::string prefix = "scene_collection_" + std::to_string(i) + "_";
            try
            {
                collection.id = static_cast<SceneUUID>(std::stoull(getValue(prefix + "id")));
            }
            catch (...)
            {
                collection.id = 0;
            }
            collection.name = getValue(prefix + "name");
            if (collection.name.empty())
            {
                collection.name = "Collection " + std::to_string(i + 1);
            }
            collection.visible = (getValue(prefix + "visible") != "0");
            collection.selectable = (getValue(prefix + "selectable") != "0");
            m_Collections.push_back(collection);
        }

        int emptyCount = 0;
        try
        {
            emptyCount = std::stoi(getValue("scene_empty_count"));
        }
        catch (...)
        {
            emptyCount = 0;
        }

        for (int i = 0; i < emptyCount; ++i)
        {
            TransformEmpty empty;
            const std::string prefix = "scene_empty_" + std::to_string(i) + "_";
            try
            {
                empty.id = static_cast<SceneUUID>(std::stoull(getValue(prefix + "id")));
            }
            catch (...)
            {
                empty.id = 0;
            }
            empty.name = getValue(prefix + "name");
            if (empty.name.empty())
            {
                empty.name = "Empty " + std::to_string(i + 1);
            }

            try
            {
                empty.position.x = std::stof(getValue(prefix + "position_x"));
                empty.position.y = std::stof(getValue(prefix + "position_y"));
                empty.position.z = std::stof(getValue(prefix + "position_z"));
            }
            catch (...)
            {
                empty.position = glm::vec3(0.0f);
            }

            for (int axis = 0; axis < 3; ++axis)
            {
                try
                {
                    empty.axes[axis].x = std::stof(getValue(prefix + "axis_" + std::to_string(axis) + "_x"));
                    empty.axes[axis].y = std::stof(getValue(prefix + "axis_" + std::to_string(axis) + "_y"));
                    empty.axes[axis].z = std::stof(getValue(prefix + "axis_" + std::to_string(axis) + "_z"));
                }
                catch (...)
                {
                }
            }

            try
            {
                empty.collectionId = static_cast<SceneUUID>(std::stoull(getValue(prefix + "collection_id")));
            }
            catch (...)
            {
                empty.collectionId = 0;
            }

            try
            {
                empty.parentEmptyId = static_cast<SceneUUID>(std::stoull(getValue(prefix + "parent_empty_id")));
            }
            catch (...)
            {
                empty.parentEmptyId = 0;
            }

            try
            {
                empty.collectionIndex = std::stoi(getValue(prefix + "collection_index"));
            }
            catch (...)
            {
                empty.collectionIndex = 0;
            }

            empty.visible = (getValue(prefix + "visible") != "0");
            empty.selectable = (getValue(prefix + "selectable") != "0");
            m_TransformEmpties.push_back(empty);
        }

        int groupCount = 0;
        try
        {
            groupCount = std::stoi(getValue("scene_group_count"));
        }
        catch (...)
        {
            groupCount = 0;
        }

        for (int i = 0; i < groupCount; ++i)
        {
            SceneGroup group;
            const std::string prefix = "scene_group_" + std::to_string(i) + "_";
            try
            {
                group.id = static_cast<SceneUUID>(std::stoull(getValue(prefix + "id")));
            }
            catch (...)
            {
                group.id = 0;
            }
            group.name = getValue(prefix + "name");
            if (group.name.empty())
            {
                group.name = "Group " + std::to_string(i + 1);
            }

            for (const std::string &token : SplitCsv(getValue(prefix + "atom_indices")))
            {
                try
                {
                    group.atomIndices.push_back(static_cast<std::size_t>(std::stoull(token)));
                }
                catch (...)
                {
                }
            }

            for (const std::string &token : SplitCsv(getValue(prefix + "atom_ids")))
            {
                try
                {
                    group.atomIds.push_back(static_cast<SceneUUID>(std::stoull(token)));
                }
                catch (...)
                {
                }
            }

            for (const std::string &token : SplitCsv(getValue(prefix + "empty_ids")))
            {
                try
                {
                    group.emptyIds.push_back(static_cast<SceneUUID>(std::stoull(token)));
                }
                catch (...)
                {
                }
            }

            for (const std::string &token : SplitCsv(getValue(prefix + "empty_indices")))
            {
                try
                {
                    group.emptyIndices.push_back(std::stoi(token));
                }
                catch (...)
                {
                }
            }

            m_ObjectGroups.push_back(group);
        }

        int cameraPresetCount = 0;
        try
        {
            cameraPresetCount = std::stoi(getValue("scene_camera_preset_count"));
        }
        catch (...)
        {
            cameraPresetCount = 0;
        }

        for (int i = 0; i < cameraPresetCount; ++i)
        {
            CameraPreset preset;
            const std::string prefix = "scene_camera_preset_" + std::to_string(i) + "_";
            preset.name = getValue(prefix + "name");
            if (preset.name.empty())
            {
                preset.name = "Preset " + std::to_string(i + 1);
            }

            try
            {
                preset.target.x = std::stof(getValue(prefix + "target_x"));
                preset.target.y = std::stof(getValue(prefix + "target_y"));
                preset.target.z = std::stof(getValue(prefix + "target_z"));
                preset.distance = std::stof(getValue(prefix + "distance"));
                preset.yaw = std::stof(getValue(prefix + "yaw"));
                preset.pitch = std::stof(getValue(prefix + "pitch"));
                preset.roll = std::stof(getValue(prefix + "roll"));
            }
            catch (...)
            {
                continue;
            }

            if (preset.distance < 0.05f)
            {
                preset.distance = 0.05f;
            }

            m_CameraPresets.push_back(preset);
        }

        int angleLabelCount = 0;
        try
        {
            angleLabelCount = std::stoi(getValue("scene_angle_label_count"));
        }
        catch (...)
        {
            angleLabelCount = 0;
        }
        for (int i = 0; i < angleLabelCount; ++i)
        {
            const std::string prefix = "scene_angle_label_" + std::to_string(i) + "_";
            std::size_t atomA = 0;
            std::size_t atomB = 0;
            std::size_t atomC = 0;
            try
            {
                atomA = static_cast<std::size_t>(std::stoull(getValue(prefix + "a")));
                atomB = static_cast<std::size_t>(std::stoull(getValue(prefix + "b")));
                atomC = static_cast<std::size_t>(std::stoull(getValue(prefix + "c")));
            }
            catch (...)
            {
                continue;
            }

            if (atomA == atomB || atomB == atomC || atomA == atomC)
            {
                continue;
            }

            AngleLabelState label;
            label.atomA = atomA;
            label.atomB = atomB;
            label.atomC = atomC;
            m_AngleLabelStates[MakeAngleTripletKey(atomA, atomB, atomC)] = label;
        }

        try
        {
            m_SelectedCameraPresetIndex = std::stoi(getValue("scene_camera_preset_selected"));
        }
        catch (...)
        {
            m_SelectedCameraPresetIndex = -1;
        }

        if (m_SelectedCameraPresetIndex < 0 ||
            m_SelectedCameraPresetIndex >= static_cast<int>(m_CameraPresets.size()))
        {
            m_SelectedCameraPresetIndex = m_CameraPresets.empty() ? -1 : 0;
        }

        m_CollectionCounter = std::max(1, static_cast<int>(m_Collections.size()) + 1);
        m_GroupCounter = std::max(1, static_cast<int>(m_ObjectGroups.size()) + 1);
        m_TransformEmptyCounter = std::max(1, static_cast<int>(m_TransformEmpties.size()) + 1);

        LogInfo("LoadSceneState: loaded collections=" + std::to_string(m_Collections.size()) +
                ", empties=" + std::to_string(m_TransformEmpties.size()) +
                ", groups=" + std::to_string(m_ObjectGroups.size()) +
                ", atomNodeIds=" + std::to_string(m_AtomNodeIds.size()));
    }

    void EditorLayer::SaveSceneState() const
    {
        std::filesystem::create_directories("config");

        std::ofstream out(kSceneStatePath, std::ios::trunc);
        if (!out.is_open())
        {
            return;
        }

        out << "scene_version=1\n";
        out << "scene_atom_count=" << m_AtomNodeIds.size() << '\n';
        for (std::size_t i = 0; i < m_AtomNodeIds.size(); ++i)
        {
            out << "scene_atom_" << i << "_id=" << m_AtomNodeIds[i] << '\n';
        }
        out << "scene_atom_collection_indices=" << JoinCsv(m_AtomCollectionIndices) << '\n';
        out << "scene_collection_count=" << m_Collections.size() << '\n';
        for (std::size_t i = 0; i < m_Collections.size(); ++i)
        {
            const SceneCollection &collection = m_Collections[i];
            const std::string prefix = "scene_collection_" + std::to_string(i) + "_";
            out << prefix << "id=" << collection.id << '\n';
            out << prefix << "name=" << collection.name << '\n';
            out << prefix << "visible=" << (collection.visible ? 1 : 0) << '\n';
            out << prefix << "selectable=" << (collection.selectable ? 1 : 0) << '\n';
        }

        out << "scene_empty_count=" << m_TransformEmpties.size() << '\n';
        for (std::size_t i = 0; i < m_TransformEmpties.size(); ++i)
        {
            const TransformEmpty &empty = m_TransformEmpties[i];
            const std::string prefix = "scene_empty_" + std::to_string(i) + "_";
            out << prefix << "id=" << empty.id << '\n';
            out << prefix << "name=" << empty.name << '\n';
            out << prefix << "position_x=" << empty.position.x << '\n';
            out << prefix << "position_y=" << empty.position.y << '\n';
            out << prefix << "position_z=" << empty.position.z << '\n';
            out << prefix << "collection_id=" << empty.collectionId << '\n';
            out << prefix << "parent_empty_id=" << empty.parentEmptyId << '\n';
            out << prefix << "collection_index=" << empty.collectionIndex << '\n';
            out << prefix << "visible=" << (empty.visible ? 1 : 0) << '\n';
            out << prefix << "selectable=" << (empty.selectable ? 1 : 0) << '\n';
            for (int axis = 0; axis < 3; ++axis)
            {
                out << prefix << "axis_" << axis << "_x=" << empty.axes[axis].x << '\n';
                out << prefix << "axis_" << axis << "_y=" << empty.axes[axis].y << '\n';
                out << prefix << "axis_" << axis << "_z=" << empty.axes[axis].z << '\n';
            }
        }

        out << "scene_group_count=" << m_ObjectGroups.size() << '\n';
        for (std::size_t i = 0; i < m_ObjectGroups.size(); ++i)
        {
            const SceneGroup &group = m_ObjectGroups[i];
            const std::string prefix = "scene_group_" + std::to_string(i) + "_";
            out << prefix << "id=" << group.id << '\n';
            out << prefix << "name=" << group.name << '\n';
            out << prefix << "atom_ids=" << JoinCsv(group.atomIds) << '\n';
            out << prefix << "atom_indices=" << JoinCsv(group.atomIndices) << '\n';
            out << prefix << "empty_ids=" << JoinCsv(group.emptyIds) << '\n';
            out << prefix << "empty_indices=" << JoinCsv(group.emptyIndices) << '\n';
        }

        out << "scene_camera_preset_count=" << m_CameraPresets.size() << '\n';
        out << "scene_camera_preset_selected=" << m_SelectedCameraPresetIndex << '\n';
        for (std::size_t i = 0; i < m_CameraPresets.size(); ++i)
        {
            const CameraPreset &preset = m_CameraPresets[i];
            const std::string prefix = "scene_camera_preset_" + std::to_string(i) + "_";
            out << prefix << "name=" << preset.name << '\n';
            out << prefix << "target_x=" << preset.target.x << '\n';
            out << prefix << "target_y=" << preset.target.y << '\n';
            out << prefix << "target_z=" << preset.target.z << '\n';
            out << prefix << "distance=" << preset.distance << '\n';
            out << prefix << "yaw=" << preset.yaw << '\n';
            out << prefix << "pitch=" << preset.pitch << '\n';
            out << prefix << "roll=" << preset.roll << '\n';
        }

        out << "scene_angle_label_count=" << m_AngleLabelStates.size() << '\n';
        std::size_t angleLabelIndex = 0;
        for (const auto &[key, label] : m_AngleLabelStates)
        {
            (void)key;
            const std::string prefix = "scene_angle_label_" + std::to_string(angleLabelIndex++) + "_";
            out << prefix << "a=" << label.atomA << '\n';
            out << prefix << "b=" << label.atomB << '\n';
            out << prefix << "c=" << label.atomC << '\n';
        }
    }


} // namespace ds
