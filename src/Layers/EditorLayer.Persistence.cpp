#include "Layers/EditorLayerPrivate.h"

#include <yaml-cpp/yaml.h>

namespace ds
{
    namespace
    {
        template <typename T>
        void TryLoadYamlScalar(const YAML::Node &node, const char *key, T &value)
        {
            if (!node || !node[key])
            {
                return;
            }

            try
            {
                value = node[key].as<T>();
            }
            catch (const YAML::Exception &)
            {
            }
        }

        void TryLoadYamlVec3(const YAML::Node &node, glm::vec3 &value)
        {
            if (!node)
            {
                return;
            }

            try
            {
                if (node.IsMap())
                {
                    if (node["r"] && node["g"] && node["b"])
                    {
                        value.r = node["r"].as<float>();
                        value.g = node["g"].as<float>();
                        value.b = node["b"].as<float>();
                        return;
                    }

                    if (node["x"] && node["y"] && node["z"])
                    {
                        value.x = node["x"].as<float>();
                        value.y = node["y"].as<float>();
                        value.z = node["z"].as<float>();
                        return;
                    }
                }
                else if (node.IsSequence() && node.size() >= 3)
                {
                    value.x = node[0].as<float>();
                    value.y = node[1].as<float>();
                    value.z = node[2].as<float>();
                }
            }
            catch (const YAML::Exception &)
            {
            }
        }

        void TryLoadYamlArray4(const YAML::Node &node, std::array<float, 4> &value)
        {
            if (!node || !node.IsSequence() || node.size() < 4)
            {
                return;
            }

            try
            {
                for (std::size_t index = 0; index < 4; ++index)
                {
                    value[index] = node[index].as<float>();
                }
            }
            catch (const YAML::Exception &)
            {
            }
        }

        YAML::Node MakeColorNode(const glm::vec3 &value)
        {
            YAML::Node node;
            node["r"] = value.r;
            node["g"] = value.g;
            node["b"] = value.b;
            return node;
        }

        YAML::Node MakeVec3Node(const glm::vec3 &value)
        {
            YAML::Node node;
            node["x"] = value.x;
            node["y"] = value.y;
            node["z"] = value.z;
            return node;
        }

        YAML::Node MakeArray4Node(const std::array<float, 4> &value)
        {
            YAML::Node node(YAML::NodeType::Sequence);
            for (float component : value)
            {
                node.push_back(component);
            }
            return node;
        }

    } // namespace

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
        ClearSceneHistory();
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
        PushUndoSnapshot("Append structure as collection");
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
        PushUndoSnapshot("Add atom");

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
        bool hasChange = false;
        for (std::size_t atomIndex : m_SelectedAtomIndices)
        {
            if (atomIndex < m_WorkingStructure.atoms.size() && m_WorkingStructure.atoms[atomIndex].element != symbol)
            {
                hasChange = true;
                break;
            }
        }

        if (!hasChange)
        {
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Change atom type: selection already uses " + symbol + ".";
            if (outChangedCount)
            {
                *outChangedCount = 0;
            }
            return true;
        }

        PushUndoSnapshot("Change atom type");
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

    void EditorLayer::ApplyAtomDefaultsToSceneSettings()
    {
        m_AtomDefaults.defaultOverrideColor = glm::clamp(m_AtomDefaults.defaultOverrideColor, glm::vec3(0.0f), glm::vec3(1.0f));
        m_AtomDefaults.defaultSize = std::clamp(m_AtomDefaults.defaultSize, 0.05f, 1.25f);

        std::unordered_map<std::string, glm::vec3> sanitizedElementColors;
        sanitizedElementColors.reserve(m_AtomDefaults.elementColors.size());
        for (const auto &[element, color] : m_AtomDefaults.elementColors)
        {
            const std::string normalizedElement = NormalizeElementSymbol(element);
            if (normalizedElement.empty())
            {
                continue;
            }

            sanitizedElementColors[normalizedElement] = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
        }

        m_AtomDefaults.elementColors = std::move(sanitizedElementColors);
        m_SceneSettings.atomScale = m_AtomDefaults.defaultSize;
        m_SceneSettings.atomOverrideColor = m_AtomDefaults.defaultOverrideColor;
        m_ElementColorOverrides = m_AtomDefaults.elementColors;
    }

    void EditorLayer::LoadDefaultConfigYaml()
    {
        std::snprintf(m_ImportPathBuffer.data(), m_ImportPathBuffer.size(), "%s", kFallbackStartupImportPath);

        if (!std::filesystem::exists(kDefaultConfigPath))
        {
            return;
        }

        try
        {
            const YAML::Node root = YAML::LoadFile(kDefaultConfigPath);
            const YAML::Node app = root["app"];
            if (app)
            {
                const YAML::Node startup = app["startup"];
                if (startup && startup["defaultStructurePath"])
                {
                    const std::string defaultStructurePath = startup["defaultStructurePath"].as<std::string>();
                    if (!defaultStructurePath.empty())
                    {
                        std::snprintf(m_ImportPathBuffer.data(), m_ImportPathBuffer.size(), "%s", defaultStructurePath.c_str());
                    }
                }
            }

            const YAML::Node camera = root["app"] ? root["app"]["camera"] : YAML::Node();
            TryLoadYamlScalar(camera, "orbitSensitivity", m_CameraOrbitSensitivity);
            TryLoadYamlScalar(camera, "panSensitivity", m_CameraPanSensitivity);
            TryLoadYamlScalar(camera, "zoomSensitivity", m_CameraZoomSensitivity);
        }
        catch (const YAML::Exception &exception)
        {
            LogWarn(std::string("LoadDefaultConfigYaml failed: ") + exception.what());
        }
    }

    void EditorLayer::MigrateLegacyAtomIniIfNeeded()
    {
        if (std::filesystem::exists(kAtomSettingsPath) || !std::filesystem::exists(kLegacyAtomSettingsPath))
        {
            return;
        }

        LoadLegacyAtomSettingsIni(kLegacyAtomSettingsPath);
        SaveAtomSettingsYaml();
        LogInfo("MigrateLegacyAtomIniIfNeeded: migrated config/atom_settings.ini to config/atom_settings.yaml");
    }

    void EditorLayer::LoadLegacyAtomSettingsIni(const std::string &path)
    {
        std::ifstream in(path);
        if (!in.is_open())
        {
            return;
        }

        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#' || line[0] == ';')
            {
                continue;
            }

            const std::size_t sep = line.find('=');
            if (sep == std::string::npos)
            {
                continue;
            }

            const std::string key = line.substr(0, sep);
            const std::string value = line.substr(sep + 1);

            if (key == "default_atom_size")
            {
                try
                {
                    m_AtomDefaults.defaultSize = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "default_atom_override_color_r" || key == "default_atom_color_r")
            {
                try
                {
                    m_AtomDefaults.defaultOverrideColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "default_atom_override_color_g" || key == "default_atom_color_g")
            {
                try
                {
                    m_AtomDefaults.defaultOverrideColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "default_atom_override_color_b" || key == "default_atom_color_b")
            {
                try
                {
                    m_AtomDefaults.defaultOverrideColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "element_colors")
            {
                m_AtomDefaults.elementColors = ParseElementColorOverrides(value);
            }
        }

        ApplyAtomDefaultsToSceneSettings();
    }

    void EditorLayer::LoadAtomSettingsYaml()
    {
        if (!std::filesystem::exists(kAtomSettingsPath))
        {
            ApplyAtomDefaultsToSceneSettings();
            SaveAtomSettingsYaml();
            return;
        }

        try
        {
            const YAML::Node root = YAML::LoadFile(kAtomSettingsPath);
            const YAML::Node defaults = root["defaults"];
            const YAML::Node elements = root["elements"];

            if (defaults)
            {
                TryLoadYamlVec3(defaults["overrideColor"], m_AtomDefaults.defaultOverrideColor);
                TryLoadYamlScalar(defaults, "atomScale", m_AtomDefaults.defaultSize);
            }

            std::unordered_map<std::string, glm::vec3> elementColors;
            if (elements && elements.IsMap())
            {
                for (const auto &entry : elements)
                {
                    const std::string element = NormalizeElementSymbol(entry.first.as<std::string>());
                    if (element.empty())
                    {
                        continue;
                    }

                    glm::vec3 color = ColorFromElement(element);
                    const YAML::Node visual = entry.second["visual"];
                    if (visual)
                    {
                        TryLoadYamlVec3(visual["color"], color);
                    }

                    elementColors[element] = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
                }
            }

            m_AtomDefaults.elementColors = std::move(elementColors);
            ApplyAtomDefaultsToSceneSettings();
        }
        catch (const YAML::Exception &exception)
        {
            LogWarn(std::string("LoadAtomSettingsYaml failed: ") + exception.what());
            ApplyAtomDefaultsToSceneSettings();
        }
    }

    void EditorLayer::LoadAtomSettings()
    {
        LoadAtomSettingsYaml();
    }

    void EditorLayer::SaveAtomSettingsYaml() const
    {
        std::filesystem::create_directories("config");

        std::ofstream out(kAtomSettingsPath, std::ios::trunc);
        if (!out.is_open())
        {
            return;
        }

        const glm::vec3 defaultOverrideColor = glm::clamp(m_SceneSettings.atomOverrideColor, glm::vec3(0.0f), glm::vec3(1.0f));
        const float defaultSize = std::clamp(m_SceneSettings.atomScale, 0.05f, 1.25f);
        YAML::Node root;
        root["version"] = 1;
        root["defaults"]["overrideColor"] = MakeColorNode(defaultOverrideColor);
        root["defaults"]["atomScale"] = defaultSize;

        std::vector<std::string> elementKeys;
        elementKeys.reserve(m_ElementColorOverrides.size());
        for (const auto &[element, color] : m_ElementColorOverrides)
        {
            (void)color;
            elementKeys.push_back(element);
        }
        std::sort(elementKeys.begin(), elementKeys.end());

        for (const std::string &element : elementKeys)
        {
            const auto it = m_ElementColorOverrides.find(element);
            if (it == m_ElementColorOverrides.end())
            {
                continue;
            }

            root["elements"][element]["visual"]["color"] = MakeColorNode(glm::clamp(it->second, glm::vec3(0.0f), glm::vec3(1.0f)));
        }

        YAML::Emitter emitter;
        emitter.SetIndent(2);
        emitter << root;
        out << emitter.c_str();
    }

    void EditorLayer::SaveAtomSettings() const
    {
        SaveAtomSettingsYaml();
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

    EditorLayer::EditorSceneSnapshot EditorLayer::CaptureSceneSnapshot() const
    {
        EditorSceneSnapshot snapshot;
        snapshot.hasStructureLoaded = m_HasStructureLoaded;
        snapshot.originalStructure = m_OriginalStructure;
        snapshot.workingStructure = m_WorkingStructure;
        snapshot.atomNodeIds = m_AtomNodeIds;
        snapshot.atomCollectionIndices = m_AtomCollectionIndices;
        snapshot.hiddenAtomIndices = m_HiddenAtomIndices;
        snapshot.hiddenBondKeys = m_HiddenBondKeys;
        snapshot.manualBondKeys = m_ManualBondKeys;
        snapshot.deletedBondKeys = m_DeletedBondKeys;
        snapshot.bondLabelStates = m_BondLabelStates;
        snapshot.selectedBondKeys = m_SelectedBondKeys;
        snapshot.selectedBondLabelKey = m_SelectedBondLabelKey;
        snapshot.angleLabelStates = m_AngleLabelStates;
        snapshot.elementColorOverrides = m_ElementColorOverrides;
        snapshot.atomColorOverrides = m_AtomColorOverrides;
        snapshot.selectedAtomIndices = m_SelectedAtomIndices;
        snapshot.outlinerAtomSelectionAnchor = m_OutlinerAtomSelectionAnchor;
        snapshot.transformEmpties = m_TransformEmpties;
        snapshot.activeTransformEmptyIndex = m_ActiveTransformEmptyIndex;
        snapshot.selectedTransformEmptyIndex = m_SelectedTransformEmptyIndex;
        snapshot.transformEmptyCounter = m_TransformEmptyCounter;
        snapshot.collections = m_Collections;
        snapshot.objectGroups = m_ObjectGroups;
        snapshot.activeCollectionIndex = m_ActiveCollectionIndex;
        snapshot.activeGroupIndex = m_ActiveGroupIndex;
        snapshot.collectionCounter = m_CollectionCounter;
        snapshot.groupCounter = m_GroupCounter;
        snapshot.selectedSpecialNode = m_SelectedSpecialNode;
        snapshot.lightPosition = m_LightPosition;
        snapshot.cursorPosition = m_CursorPosition;
        return snapshot;
    }

    void EditorLayer::RestoreSceneSnapshot(const EditorSceneSnapshot &snapshot)
    {
        m_HasStructureLoaded = snapshot.hasStructureLoaded;
        m_OriginalStructure = snapshot.originalStructure;
        m_WorkingStructure = snapshot.workingStructure;
        m_AtomNodeIds = snapshot.atomNodeIds;
        m_AtomCollectionIndices = snapshot.atomCollectionIndices;
        m_HiddenAtomIndices = snapshot.hiddenAtomIndices;
        m_HiddenBondKeys = snapshot.hiddenBondKeys;
        m_ManualBondKeys = snapshot.manualBondKeys;
        m_DeletedBondKeys = snapshot.deletedBondKeys;
        m_BondLabelStates = snapshot.bondLabelStates;
        m_SelectedBondKeys = snapshot.selectedBondKeys;
        m_SelectedBondLabelKey = snapshot.selectedBondLabelKey;
        m_AngleLabelStates = snapshot.angleLabelStates;
        m_ElementColorOverrides = snapshot.elementColorOverrides;
        m_AtomColorOverrides = snapshot.atomColorOverrides;
        m_SelectedAtomIndices = snapshot.selectedAtomIndices;
        m_OutlinerAtomSelectionAnchor = snapshot.outlinerAtomSelectionAnchor;
        m_TransformEmpties = snapshot.transformEmpties;
        m_ActiveTransformEmptyIndex = snapshot.activeTransformEmptyIndex;
        m_SelectedTransformEmptyIndex = snapshot.selectedTransformEmptyIndex;
        m_TransformEmptyCounter = snapshot.transformEmptyCounter;
        m_Collections = snapshot.collections;
        m_ObjectGroups = snapshot.objectGroups;
        m_ActiveCollectionIndex = snapshot.activeCollectionIndex;
        m_ActiveGroupIndex = snapshot.activeGroupIndex;
        m_CollectionCounter = snapshot.collectionCounter;
        m_GroupCounter = snapshot.groupCounter;
        m_SelectedSpecialNode = snapshot.selectedSpecialNode;
        m_LightPosition = snapshot.lightPosition;
        m_CursorPosition = snapshot.cursorPosition;

        EnsureAtomNodeIds();
        EnsureAtomCollectionAssignments();
        for (int groupIndex = 0; groupIndex < static_cast<int>(m_ObjectGroups.size()); ++groupIndex)
        {
            SceneGroupingBackend::SanitizeGroup(*this, groupIndex);
        }

        m_GeneratedBonds.clear();
        m_AutoBondsDirty = true;
        m_LastStructureOperationFailed = false;
    }

    void EditorLayer::PushUndoSnapshot(std::string label)
    {
        if (m_SuspendUndoCapture)
        {
            return;
        }

        m_UndoStack.push_back(EditorSceneHistoryEntry{std::move(label), CaptureSceneSnapshot()});
        if (m_UndoStack.size() > kMaxSceneHistoryEntries)
        {
            m_UndoStack.erase(m_UndoStack.begin(), m_UndoStack.begin() + static_cast<std::ptrdiff_t>(m_UndoStack.size() - kMaxSceneHistoryEntries));
        }
        m_RedoStack.clear();
    }

    bool EditorLayer::UndoSceneEdit()
    {
        if (m_UndoStack.empty())
        {
            return false;
        }

        EditorSceneHistoryEntry entry = std::move(m_UndoStack.back());
        m_UndoStack.pop_back();
        m_RedoStack.push_back(EditorSceneHistoryEntry{entry.label, CaptureSceneSnapshot()});
        if (m_RedoStack.size() > kMaxSceneHistoryEntries)
        {
            m_RedoStack.erase(m_RedoStack.begin(), m_RedoStack.begin() + static_cast<std::ptrdiff_t>(m_RedoStack.size() - kMaxSceneHistoryEntries));
        }

        RestoreSceneSnapshot(entry.snapshot);
        m_LastStructureMessage = "Undo: " + entry.label;
        LogInfo(m_LastStructureMessage);
        return true;
    }

    bool EditorLayer::RedoSceneEdit()
    {
        if (m_RedoStack.empty())
        {
            return false;
        }

        EditorSceneHistoryEntry entry = std::move(m_RedoStack.back());
        m_RedoStack.pop_back();
        m_UndoStack.push_back(EditorSceneHistoryEntry{entry.label, CaptureSceneSnapshot()});
        if (m_UndoStack.size() > kMaxSceneHistoryEntries)
        {
            m_UndoStack.erase(m_UndoStack.begin(), m_UndoStack.begin() + static_cast<std::ptrdiff_t>(m_UndoStack.size() - kMaxSceneHistoryEntries));
        }

        RestoreSceneSnapshot(entry.snapshot);
        m_LastStructureMessage = "Redo: " + entry.label;
        LogInfo(m_LastStructureMessage);
        return true;
    }

    void EditorLayer::ClearSceneHistory()
    {
        m_UndoStack.clear();
        m_RedoStack.clear();
    }

    void EditorLayer::ApplyTheme(ThemePreset preset)
    {
        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.ChildRounding = 0.0f;
        style.PopupRounding = 0.0f;
        style.FrameRounding = 2.0f;
        style.GrabRounding = 2.0f;
        style.ScrollbarRounding = 2.0f;
        style.TabRounding = 0.0f;
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(6.0f, 4.0f);
        style.CellPadding = ImVec2(6.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.IndentSpacing = 20.0f;

        switch (preset)
        {
        case ThemePreset::Dark:
        {
            ImGui::StyleColorsDark();
            ImVec4 *colors = style.Colors;
            colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.65f, 0.68f, 0.71f, 1.00f);
            colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.11f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.18f, 0.19f, 0.20f, 1.00f);
            colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.21f, 0.22f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.31f, 0.31f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.11f, 0.11f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.11f, 0.11f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.11f, 0.11f, 1.00f);
            colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.11f, 0.11f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.21f, 0.22f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.31f, 0.31f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.34f, 0.35f, 0.35f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 0.50f, 0.00f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.90f, 0.46f, 0.08f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.50f, 0.00f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.20f, 0.21f, 0.22f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.31f, 0.31f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.20f, 0.21f, 0.22f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.31f, 0.31f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
            colors[ImGuiCol_Separator] = ImVec4(0.19f, 0.20f, 0.21f, 1.00f);
            colors[ImGuiCol_SeparatorHovered] = ImVec4(0.88f, 0.47f, 0.08f, 1.00f);
            colors[ImGuiCol_SeparatorActive] = ImVec4(1.00f, 0.55f, 0.00f, 1.00f);
            colors[ImGuiCol_ResizeGrip] = ImVec4(0.88f, 0.47f, 0.08f, 0.24f);
            colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.88f, 0.47f, 0.08f, 0.72f);
            colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 0.55f, 0.00f, 0.92f);
            colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.31f, 0.31f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.21f, 0.22f, 1.00f);
            colors[ImGuiCol_TabUnfocused] = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
            colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
            colors[ImGuiCol_DockingPreview] = ImVec4(1.00f, 0.50f, 0.00f, 0.72f);
            colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
            colors[ImGuiCol_PlotLines] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
            colors[ImGuiCol_PlotHistogram] = ImVec4(1.00f, 0.50f, 0.00f, 1.00f);
            colors[ImGuiCol_TableHeaderBg] = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
            colors[ImGuiCol_TableBorderStrong] = ImVec4(0.18f, 0.19f, 0.20f, 1.00f);
            colors[ImGuiCol_TableBorderLight] = ImVec4(0.15f, 0.16f, 0.17f, 1.00f);
            colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);
            colors[ImGuiCol_TextSelectedBg] = ImVec4(1.00f, 0.50f, 0.00f, 0.28f);
            colors[ImGuiCol_NavHighlight] = ImVec4(1.00f, 0.50f, 0.00f, 0.80f);
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

    void EditorLayer::LoadLegacyUiSettingsIni(const std::string &settingsPath)
    {
        std::ifstream in(settingsPath);
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
            else if (key == "show_stats_panel")
            {
                m_ShowStatsPanel = (value == "1");
            }
            else if (key == "show_viewport_info_panel")
            {
                m_ShowViewportInfoPanel = (value == "1");
            }
            if (key == "show_shortcut_reference_panel")
            {
                m_ShowShortcutReferencePanel = (value == "1");
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
            else if (key == "show_scene_outliner_panel")
            {
                m_ShowSceneOutlinerPanel = (value == "1");
            }
            else if (key == "show_object_properties_panel")
            {
                m_ShowObjectPropertiesPanel = (value == "1");
            }
            else if (key == "show_render_preview_window")
            {
                m_ShowRenderPreviewWindow = (value == "1");
            }
            else if (key == "viewport_settings_open")
            {
                m_ViewportSettingsOpen = (value == "1");
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
            else if (key == "viewport_selection_filter")
            {
                try
                {
                    const int mode = std::stoi(value);
                    m_SelectionFilter = static_cast<SelectionFilter>(std::clamp(mode, 0, 3));
                }
                catch (...)
                {
                    m_SelectionFilter = SelectionFilter::AtomsAndBonds;
                }
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
            if (key == "viewport_bond_label_text_color_r")
            {
                try
                {
                    m_BondLabelTextColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bond_label_text_color_g")
            {
                try
                {
                    m_BondLabelTextColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bond_label_text_color_b")
            {
                try
                {
                    m_BondLabelTextColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bond_label_bg_color_r")
            {
                try
                {
                    m_BondLabelBackgroundColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bond_label_bg_color_g")
            {
                try
                {
                    m_BondLabelBackgroundColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bond_label_bg_color_b")
            {
                try
                {
                    m_BondLabelBackgroundColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bond_label_border_color_r")
            {
                try
                {
                    m_BondLabelBorderColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bond_label_border_color_g")
            {
                try
                {
                    m_BondLabelBorderColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bond_label_border_color_b")
            {
                try
                {
                    m_BondLabelBorderColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bond_label_precision")
            {
                try
                {
                    m_BondLabelPrecision = std::stoi(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_selected_atom_custom_color_r")
            {
                try
                {
                    m_SelectedAtomCustomColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_selected_atom_custom_color_g")
            {
                try
                {
                    m_SelectedAtomCustomColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_selected_atom_custom_color_b")
            {
                try
                {
                    m_SelectedAtomCustomColor.b = std::stof(value);
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
            if (key == "viewport_projection_mode")
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
            if (key == "viewport_measurement_show")
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
            if (key == "viewport_show_transform_empties")
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
            if (key == "render_image_width")
            {
                try
                {
                    m_RenderImageWidth = std::stoi(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_image_height")
            {
                try
                {
                    m_RenderImageHeight = std::stoi(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_image_format")
            {
                try
                {
                    const int format = std::stoi(value);
                    m_RenderImageFormat = (format == 1) ? RenderImageFormat::Jpg : RenderImageFormat::Png;
                }
                catch (...)
                {
                    m_RenderImageFormat = RenderImageFormat::Png;
                }
            }
            if (key == "render_jpeg_quality")
            {
                try
                {
                    m_RenderJpegQuality = std::stoi(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_crop_enabled")
            {
                m_RenderCropEnabled = (value == "1");
            }
            if (key == "render_show_bond_length_labels")
            {
                m_RenderShowBondLengthLabels = (value == "1");
            }
            if (key == "render_bond_label_scale_multiplier")
            {
                try
                {
                    m_RenderBondLabelScaleMultiplier = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_bond_label_precision")
            {
                try
                {
                    m_RenderBondLabelPrecision = std::stoi(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_bond_label_text_color_r")
            {
                try
                {
                    m_RenderBondLabelTextColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_bond_label_text_color_g")
            {
                try
                {
                    m_RenderBondLabelTextColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_bond_label_text_color_b")
            {
                try
                {
                    m_RenderBondLabelTextColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_bond_label_bg_color_r")
            {
                try
                {
                    m_RenderBondLabelBackgroundColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_bond_label_bg_color_g")
            {
                try
                {
                    m_RenderBondLabelBackgroundColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_bond_label_bg_color_b")
            {
                try
                {
                    m_RenderBondLabelBackgroundColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_bond_label_border_color_r")
            {
                try
                {
                    m_RenderBondLabelBorderColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_bond_label_border_color_g")
            {
                try
                {
                    m_RenderBondLabelBorderColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_bond_label_border_color_b")
            {
                try
                {
                    m_RenderBondLabelBorderColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_preview_long_side_cap")
            {
                try
                {
                    m_RenderPreviewLongSideCap = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "render_dialog_aspect_locked")
            {
                m_RenderDialogAspectLocked = (value == "1");
            }
            if (key == "input_invert_viewport_zoom")
            {
                m_InvertViewportZoom = (value == "1");
            }
            if (key == "input_invert_circle_select_wheel")
            {
                m_InvertCircleSelectWheel = (value == "1");
            }
            if (key == "input_circle_select_wheel_step")
            {
                try
                {
                    m_CircleSelectWheelStep = std::stof(value);
                }
                catch (...)
                {
                }
            }
            if (key == "hotkey_add_menu")
            {
                try
                {
                    m_HotkeyAddMenu = static_cast<std::uint32_t>(std::stoul(value));
                }
                catch (...)
                {
                }
            }
            if (key == "hotkey_open_render")
            {
                try
                {
                    m_HotkeyOpenRender = static_cast<std::uint32_t>(std::stoul(value));
                }
                catch (...)
                {
                }
            }
            if (key == "hotkey_toggle_side_panels")
            {
                try
                {
                    m_HotkeyToggleSidePanels = static_cast<std::uint32_t>(std::stoul(value));
                }
                catch (...)
                {
                }
            }
            if (key == "hotkey_delete_selection")
            {
                try
                {
                    m_HotkeyDeleteSelection = static_cast<std::uint32_t>(std::stoul(value));
                }
                catch (...)
                {
                }
            }
            if (key == "hotkey_hide_selection")
            {
                try
                {
                    m_HotkeyHideSelection = static_cast<std::uint32_t>(std::stoul(value));
                }
                catch (...)
                {
                }
            }
            if (key == "hotkey_box_select")
            {
                try
                {
                    m_HotkeyBoxSelect = static_cast<std::uint32_t>(std::stoul(value));
                }
                catch (...)
                {
                }
            }
            if (key == "hotkey_circle_select")
            {
                try
                {
                    m_HotkeyCircleSelect = static_cast<std::uint32_t>(std::stoul(value));
                }
                catch (...)
                {
                }
            }
            if (key == "hotkey_translate_modal")
            {
                try
                {
                    m_HotkeyTranslateModal = static_cast<std::uint32_t>(std::stoul(value));
                }
                catch (...)
                {
                }
            }
            if (key == "hotkey_translate_gizmo")
            {
                try
                {
                    m_HotkeyTranslateGizmo = static_cast<std::uint32_t>(std::stoul(value));
                }
                catch (...)
                {
                }
            }
            if (key == "hotkey_rotate_gizmo")
            {
                try
                {
                    m_HotkeyRotateGizmo = static_cast<std::uint32_t>(std::stoul(value));
                }
                catch (...)
                {
                }
            }
            if (key == "hotkey_scale_gizmo")
            {
                try
                {
                    m_HotkeyScaleGizmo = static_cast<std::uint32_t>(std::stoul(value));
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

        if (settingsPath == kLegacyUiSettingsPath || settingsPath == kLegacyEditorSettingsPath)
        {
            LogInfo("LoadLegacyUiSettingsIni: loaded legacy UI settings from " + settingsPath);
        }

        SanitizeLoadedUiState();
    }

    void EditorLayer::SanitizeLoadedUiState()
    {
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
        if (m_CircleSelectWheelStep < 1.0f)
            m_CircleSelectWheelStep = 1.0f;
        if (m_CircleSelectWheelStep > 32.0f)
            m_CircleSelectWheelStep = 32.0f;
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
        if (m_RenderPreviewLongSideCap < 320.0f)
            m_RenderPreviewLongSideCap = 320.0f;
        if (m_RenderPreviewLongSideCap > 8192.0f)
            m_RenderPreviewLongSideCap = 8192.0f;

        m_SceneSettings.drawCellEdges = m_ShowCellEdges;
        m_SceneSettings.cellEdgeColor = glm::clamp(m_CellEdgeColor, glm::vec3(0.0f), glm::vec3(1.0f));
        m_SceneSettings.cellEdgeLineWidth = m_CellEdgeLineWidth;
        m_RenderCropRectNormalized[0] = glm::clamp(m_RenderCropRectNormalized[0], 0.0f, 1.0f);
        m_RenderCropRectNormalized[1] = glm::clamp(m_RenderCropRectNormalized[1], 0.0f, 1.0f);
        m_RenderCropRectNormalized[2] = glm::clamp(m_RenderCropRectNormalized[2], 0.01f, 1.0f);
        m_RenderCropRectNormalized[3] = glm::clamp(m_RenderCropRectNormalized[3], 0.01f, 1.0f);

        m_SceneSettings.gridOrigin.z = 0.0f;
    }

    void EditorLayer::MigrateLegacyUiIniIfNeeded()
    {
        if (std::filesystem::exists(kUiSettingsPath))
        {
            return;
        }

        std::string legacyPath;
        if (std::filesystem::exists(kLegacyUiSettingsPath))
        {
            legacyPath = kLegacyUiSettingsPath;
        }
        else if (std::filesystem::exists(kLegacyEditorSettingsPath))
        {
            legacyPath = kLegacyEditorSettingsPath;
        }

        if (legacyPath.empty())
        {
            return;
        }

        LoadLegacyUiSettingsIni(legacyPath);
        LoadAtomSettingsYaml();
        SaveUiSettingsYaml();
        LogInfo("MigrateLegacyUiIniIfNeeded: migrated " + legacyPath + " to config/ui_settings.yaml");
    }

    void EditorLayer::LoadSettings()
    {
        LoadUiSettingsYaml();
    }

    void EditorLayer::LoadUiSettingsYaml()
    {
        if (!std::filesystem::exists(kUiSettingsPath))
        {
            SaveUiSettingsYaml();
            return;
        }

        try
        {
            const YAML::Node root = YAML::LoadFile(kUiSettingsPath);

            const YAML::Node ui = root["ui"];
            const YAML::Node panels = root["panels"];
            const YAML::Node logs = root["logs"];
            const YAML::Node camera = root["camera"];
            const YAML::Node viewport = root["viewport"];
            const YAML::Node render = root["renderImage"];
            const YAML::Node hotkeys = root["hotkeys"];

            if (ui)
            {
                std::string themeName;
                TryLoadYamlScalar(ui, "theme", themeName);
                if (themeName == "Dark")
                    m_CurrentTheme = ThemePreset::Dark;
                else if (themeName == "Light")
                    m_CurrentTheme = ThemePreset::Light;
                else if (themeName == "Classic")
                    m_CurrentTheme = ThemePreset::Classic;
                else if (themeName == "PhotoshopStyle")
                    m_CurrentTheme = ThemePreset::PhotoshopStyle;
                else if (themeName == "WarmSlate")
                    m_CurrentTheme = ThemePreset::WarmSlate;

                TryLoadYamlScalar(ui, "fontScale", m_FontScale);
                TryLoadYamlScalar(ui, "spacingScale", m_UiSpacingScale);
            }

            if (panels)
            {
                TryLoadYamlScalar(panels, "showDemoWindow", m_ShowDemoWindow);
                TryLoadYamlScalar(panels, "showLogPanel", m_ShowLogPanel);
                TryLoadYamlScalar(panels, "showStatsPanel", m_ShowStatsPanel);
                TryLoadYamlScalar(panels, "showViewportInfoPanel", m_ShowViewportInfoPanel);
                TryLoadYamlScalar(panels, "showShortcutReferencePanel", m_ShowShortcutReferencePanel);
                TryLoadYamlScalar(panels, "showActionsPanel", m_ShowActionsPanel);
                TryLoadYamlScalar(panels, "showAppearancePanel", m_ShowAppearancePanel);
                TryLoadYamlScalar(panels, "showSettingsPanel", m_ShowSettingsPanel);
                TryLoadYamlScalar(panels, "showSceneOutlinerPanel", m_ShowSceneOutlinerPanel);
                TryLoadYamlScalar(panels, "showObjectPropertiesPanel", m_ShowObjectPropertiesPanel);
                TryLoadYamlScalar(panels, "showRenderPreviewWindow", m_ShowRenderPreviewWindow);
                TryLoadYamlScalar(panels, "viewportSettingsOpen", m_ViewportSettingsOpen);
            }

            if (logs)
            {
                TryLoadYamlScalar(logs, "filter", m_LogFilter);
                TryLoadYamlScalar(logs, "autoScroll", m_LogAutoScroll);
            }

            if (camera)
            {
                TryLoadYamlScalar(camera, "orbitSensitivity", m_CameraOrbitSensitivity);
                TryLoadYamlScalar(camera, "panSensitivity", m_CameraPanSensitivity);
                TryLoadYamlScalar(camera, "zoomSensitivity", m_CameraZoomSensitivity);
                TryLoadYamlVec3(camera["target"], m_CameraTargetPersisted);
                TryLoadYamlScalar(camera, "distance", m_CameraDistancePersisted);
                TryLoadYamlScalar(camera, "yaw", m_CameraYawPersisted);
                TryLoadYamlScalar(camera, "pitch", m_CameraPitchPersisted);
                TryLoadYamlScalar(camera, "roll", m_CameraRollPersisted);
                if (camera["target"] || camera["distance"] || camera["yaw"] || camera["pitch"] || camera["roll"])
                {
                    m_HasPersistedCameraState = true;
                }
            }

            if (viewport)
            {
                TryLoadYamlVec3(viewport["clearColor"], m_SceneSettings.clearColor);
                TryLoadYamlScalar(viewport, "projectionMode", m_ProjectionModeIndex);
                TryLoadYamlScalar(viewport, "renderScale", m_ViewportRenderScale);
                TryLoadYamlScalar(viewport, "touchpadNavigation", m_TouchpadNavigationEnabled);
                TryLoadYamlScalar(viewport, "cleanViewMode", m_CleanViewMode);
                TryLoadYamlScalar(viewport, "appendImportCollection", m_AppendImportToNewCollection);

                const YAML::Node grid = viewport["grid"];
                TryLoadYamlScalar(grid, "enabled", m_SceneSettings.drawGrid);
                TryLoadYamlScalar(grid, "showCellEdges", m_ShowCellEdges);
                TryLoadYamlScalar(grid, "halfExtent", m_SceneSettings.gridHalfExtent);
                TryLoadYamlScalar(grid, "spacing", m_SceneSettings.gridSpacing);
                TryLoadYamlScalar(grid, "lineWidth", m_SceneSettings.gridLineWidth);
                TryLoadYamlVec3(grid["origin"], m_SceneSettings.gridOrigin);
                TryLoadYamlVec3(grid["color"], m_SceneSettings.gridColor);
                TryLoadYamlScalar(grid, "opacity", m_SceneSettings.gridOpacity);
                TryLoadYamlVec3(grid["cellEdgeColor"], m_CellEdgeColor);
                TryLoadYamlScalar(grid, "cellEdgeLineWidth", m_CellEdgeLineWidth);

                const YAML::Node lighting = viewport["lighting"];
                TryLoadYamlVec3(lighting["direction"], m_SceneSettings.lightDirection);
                TryLoadYamlScalar(lighting, "ambientStrength", m_SceneSettings.ambientStrength);
                TryLoadYamlScalar(lighting, "diffuseStrength", m_SceneSettings.diffuseStrength);
                TryLoadYamlVec3(lighting["color"], m_SceneSettings.lightColor);
                TryLoadYamlVec3(lighting["position"], m_LightPosition);

                const YAML::Node atoms = viewport["atoms"];
                TryLoadYamlScalar(atoms, "brightness", m_SceneSettings.atomBrightness);
                TryLoadYamlScalar(atoms, "glowStrength", m_SceneSettings.atomGlowStrength);
                TryLoadYamlVec3(atoms["selectedCustomColor"], m_SelectedAtomCustomColor);

                const YAML::Node bonds = viewport["bonds"];
                TryLoadYamlScalar(bonds, "autoGenerate", m_AutoBondGenerationEnabled);
                TryLoadYamlScalar(bonds, "showLengthLabels", m_ShowBondLengthLabels);
                TryLoadYamlScalar(bonds, "deleteLabelOnlyMode", m_BondLabelDeleteOnlyMode);
                TryLoadYamlScalar(bonds, "thresholdScale", m_BondThresholdScale);
                TryLoadYamlScalar(bonds, "usePairThresholdOverrides", m_BondUsePairThresholdOverrides);
                std::string pairThresholdOverrides;
                TryLoadYamlScalar(bonds, "pairThresholdOverrides", pairThresholdOverrides);
                if (!pairThresholdOverrides.empty())
                {
                    m_BondPairThresholdScaleOverrides = ParsePairScaleOverrides(pairThresholdOverrides);
                }
                int bondRenderStyle = static_cast<int>(m_BondRenderStyle);
                TryLoadYamlScalar(bonds, "renderStyle", bondRenderStyle);
                m_BondRenderStyle = static_cast<BondRenderStyle>(std::clamp(bondRenderStyle, 0, 2));
                TryLoadYamlScalar(bonds, "lineWidth", m_BondLineWidth);
                TryLoadYamlScalar(bonds, "lineWidthMin", m_BondLineWidthMin);
                TryLoadYamlScalar(bonds, "lineWidthMax", m_BondLineWidthMax);
                TryLoadYamlVec3(bonds["color"], m_BondColor);
                TryLoadYamlVec3(bonds["selectedColor"], m_BondSelectedColor);

                const YAML::Node bondLabels = bonds["labels"];
                TryLoadYamlVec3(bondLabels["textColor"], m_BondLabelTextColor);
                TryLoadYamlVec3(bondLabels["backgroundColor"], m_BondLabelBackgroundColor);
                TryLoadYamlVec3(bondLabels["borderColor"], m_BondLabelBorderColor);
                TryLoadYamlScalar(bondLabels, "precision", m_BondLabelPrecision);

                const YAML::Node selection = viewport["selection"];
                int selectionFilter = static_cast<int>(m_SelectionFilter);
                TryLoadYamlScalar(selection, "filter", selectionFilter);
                m_SelectionFilter = static_cast<SelectionFilter>(std::clamp(selectionFilter, 0, 3));
                TryLoadYamlVec3(selection["color"], m_SelectionColor);
                TryLoadYamlScalar(selection, "outlineThickness", m_SelectionOutlineThickness);
                TryLoadYamlScalar(selection, "debugToFile", m_SelectionDebugToFile);

                const YAML::Node input = viewport["input"];
                TryLoadYamlScalar(input, "invertViewportZoom", m_InvertViewportZoom);
                TryLoadYamlScalar(input, "invertCircleSelectWheel", m_InvertCircleSelectWheel);
                TryLoadYamlScalar(input, "circleSelectWheelStep", m_CircleSelectWheelStep);

                const YAML::Node cursor = viewport["cursor"];
                TryLoadYamlScalar(cursor, "show", m_Show3DCursor);
                TryLoadYamlVec3(cursor["position"], m_CursorPosition);
                TryLoadYamlScalar(cursor, "visualScale", m_CursorVisualScale);
                TryLoadYamlScalar(cursor, "snapToGrid", m_CursorSnapToGrid);

                const YAML::Node measurements = viewport["measurements"];
                TryLoadYamlScalar(measurements, "show", m_ShowSelectionMeasurements);
                TryLoadYamlScalar(measurements, "showDistance", m_ShowSelectionDistanceMeasurement);
                TryLoadYamlScalar(measurements, "showAngle", m_ShowSelectionAngleMeasurement);
                TryLoadYamlScalar(measurements, "showStaticAngleLabels", m_ShowStaticAngleLabels);
                TryLoadYamlScalar(measurements, "precision", m_MeasurementPrecision);
                TryLoadYamlVec3(measurements["textColor"], m_MeasurementTextColor);
                TryLoadYamlVec3(measurements["backgroundColor"], m_MeasurementBackgroundColor);

                const YAML::Node scene = viewport["scene"];
                TryLoadYamlVec3(scene["origin"], m_SceneOriginPosition);

                const YAML::Node gizmos = viewport["gizmos"];
                TryLoadYamlScalar(gizmos, "viewEnabled", m_ViewGuizmoEnabled);
                TryLoadYamlScalar(gizmos, "viewDragMode", m_ViewGizmoDragMode);
                TryLoadYamlScalar(gizmos, "transformSize", m_TransformGizmoSize);
                TryLoadYamlScalar(gizmos, "viewScale", m_ViewGizmoScale);
                TryLoadYamlScalar(gizmos, "offsetRight", m_ViewGizmoOffsetRight);
                TryLoadYamlScalar(gizmos, "offsetTop", m_ViewGizmoOffsetTop);
                TryLoadYamlScalar(gizmos, "rotateStepDegrees", m_ViewportRotateStepDeg);
                TryLoadYamlScalar(gizmos, "fallbackMarkerScale", m_FallbackGizmoVisualScale);
                TryLoadYamlScalar(gizmos, "showTransformEmpties", m_ShowTransformEmpties);
                TryLoadYamlScalar(gizmos, "transformEmptyVisualScale", m_TransformEmptyVisualScale);
                TryLoadYamlScalar(gizmos, "showGlobalAxesOverlay", m_ShowGlobalAxesOverlay);
                const YAML::Node globalAxes = gizmos["showGlobalAxis"];
                TryLoadYamlScalar(globalAxes, "x", m_ShowGlobalAxis[0]);
                TryLoadYamlScalar(globalAxes, "y", m_ShowGlobalAxis[1]);
                TryLoadYamlScalar(globalAxes, "z", m_ShowGlobalAxis[2]);
                TryLoadYamlScalar(gizmos, "useTemporaryLocalAxes", m_UseTemporaryLocalAxes);
                int temporaryAxesSource = (m_TemporaryAxesSource == TemporaryAxesSource::ActiveEmpty) ? 1 : 0;
                TryLoadYamlScalar(gizmos, "temporaryAxesSource", temporaryAxesSource);
                m_TemporaryAxesSource = (temporaryAxesSource == 1) ? TemporaryAxesSource::ActiveEmpty : TemporaryAxesSource::SelectionAtoms;
                TryLoadYamlScalar(gizmos, "temporaryAxisAtomA", m_TemporaryAxisAtomA);
                TryLoadYamlScalar(gizmos, "temporaryAxisAtomB", m_TemporaryAxisAtomB);
                TryLoadYamlScalar(gizmos, "temporaryAxisAtomC", m_TemporaryAxisAtomC);
                TryLoadYamlVec3(gizmos["axisColorX"], m_AxisColors[0]);
                TryLoadYamlVec3(gizmos["axisColorY"], m_AxisColors[1]);
                TryLoadYamlVec3(gizmos["axisColorZ"], m_AxisColors[2]);
            }

            if (render)
            {
                TryLoadYamlScalar(render, "width", m_RenderImageWidth);
                TryLoadYamlScalar(render, "height", m_RenderImageHeight);
                int renderFormat = static_cast<int>(m_RenderImageFormat);
                TryLoadYamlScalar(render, "format", renderFormat);
                m_RenderImageFormat = static_cast<RenderImageFormat>(std::clamp(renderFormat, 0, 1));
                TryLoadYamlScalar(render, "jpegQuality", m_RenderJpegQuality);
                TryLoadYamlScalar(render, "cropEnabled", m_RenderCropEnabled);
                TryLoadYamlArray4(render["cropRectNormalized"], m_RenderCropRectNormalized);
                TryLoadYamlScalar(render, "showBondLengthLabels", m_RenderShowBondLengthLabels);
                TryLoadYamlScalar(render, "bondLabelScaleMultiplier", m_RenderBondLabelScaleMultiplier);
                TryLoadYamlScalar(render, "bondLabelPrecision", m_RenderBondLabelPrecision);
                TryLoadYamlVec3(render["bondLabelTextColor"], m_RenderBondLabelTextColor);
                TryLoadYamlVec3(render["bondLabelBackgroundColor"], m_RenderBondLabelBackgroundColor);
                TryLoadYamlVec3(render["bondLabelBorderColor"], m_RenderBondLabelBorderColor);
                TryLoadYamlScalar(render, "previewLongSideCap", m_RenderPreviewLongSideCap);
                TryLoadYamlScalar(render, "dialogAspectLocked", m_RenderDialogAspectLocked);
            }

            if (hotkeys)
            {
                TryLoadYamlScalar(hotkeys, "addMenu", m_HotkeyAddMenu);
                TryLoadYamlScalar(hotkeys, "openRender", m_HotkeyOpenRender);
                TryLoadYamlScalar(hotkeys, "toggleSidePanels", m_HotkeyToggleSidePanels);
                TryLoadYamlScalar(hotkeys, "deleteSelection", m_HotkeyDeleteSelection);
                TryLoadYamlScalar(hotkeys, "hideSelection", m_HotkeyHideSelection);
                TryLoadYamlScalar(hotkeys, "boxSelect", m_HotkeyBoxSelect);
                TryLoadYamlScalar(hotkeys, "circleSelect", m_HotkeyCircleSelect);
                TryLoadYamlScalar(hotkeys, "translateModal", m_HotkeyTranslateModal);
                TryLoadYamlScalar(hotkeys, "translateGizmo", m_HotkeyTranslateGizmo);
                TryLoadYamlScalar(hotkeys, "rotateGizmo", m_HotkeyRotateGizmo);
                TryLoadYamlScalar(hotkeys, "scaleGizmo", m_HotkeyScaleGizmo);
            }
        }
        catch (const YAML::Exception &exception)
        {
            LogWarn(std::string("LoadUiSettingsYaml failed: ") + exception.what());
        }

        SanitizeLoadedUiState();
    }

    void EditorLayer::SaveUiSettingsYaml() const
    {
        std::filesystem::create_directories("config");

        std::ofstream out(kUiSettingsPath, std::ios::trunc);
        if (!out.is_open())
        {
            return;
        }

        YAML::Node root;
        root["version"] = 1;
        root["ui"]["theme"] = ThemeName(m_CurrentTheme);
        root["ui"]["fontScale"] = m_FontScale;
        root["ui"]["spacingScale"] = m_UiSpacingScale;

        root["panels"]["showDemoWindow"] = m_ShowDemoWindow;
        root["panels"]["showLogPanel"] = m_ShowLogPanel;
        root["panels"]["showStatsPanel"] = m_ShowStatsPanel;
        root["panels"]["showViewportInfoPanel"] = m_ShowViewportInfoPanel;
        root["panels"]["showShortcutReferencePanel"] = m_ShowShortcutReferencePanel;
        root["panels"]["showActionsPanel"] = m_ShowActionsPanel;
        root["panels"]["showAppearancePanel"] = m_ShowAppearancePanel;
        root["panels"]["showSettingsPanel"] = m_ShowSettingsPanel;
        root["panels"]["showSceneOutlinerPanel"] = m_ShowSceneOutlinerPanel;
        root["panels"]["showObjectPropertiesPanel"] = m_ShowObjectPropertiesPanel;
        root["panels"]["showRenderPreviewWindow"] = m_ShowRenderPreviewWindow;
        root["panels"]["viewportSettingsOpen"] = m_ViewportSettingsOpen;

        root["logs"]["filter"] = m_LogFilter;
        root["logs"]["autoScroll"] = m_LogAutoScroll;

        const glm::vec3 cameraTarget = m_Camera ? m_Camera->GetTarget() : m_CameraTargetPersisted;
        root["camera"]["orbitSensitivity"] = m_CameraOrbitSensitivity;
        root["camera"]["panSensitivity"] = m_CameraPanSensitivity;
        root["camera"]["zoomSensitivity"] = m_CameraZoomSensitivity;
        root["camera"]["target"] = MakeVec3Node(cameraTarget);
        root["camera"]["distance"] = m_Camera ? m_Camera->GetDistance() : m_CameraDistancePersisted;
        root["camera"]["yaw"] = m_Camera ? m_Camera->GetYaw() : m_CameraYawPersisted;
        root["camera"]["pitch"] = m_Camera ? m_Camera->GetPitch() : m_CameraPitchPersisted;
        root["camera"]["roll"] = m_Camera ? m_Camera->GetRoll() : m_CameraRollPersisted;
        root["camera"]["sensitivityMode"] = "relative_v2";

        root["viewport"]["clearColor"] = MakeColorNode(m_SceneSettings.clearColor);
        root["viewport"]["projectionMode"] = m_ProjectionModeIndex;
        root["viewport"]["renderScale"] = m_ViewportRenderScale;
        root["viewport"]["touchpadNavigation"] = m_TouchpadNavigationEnabled;
        root["viewport"]["cleanViewMode"] = m_CleanViewMode;
        root["viewport"]["appendImportCollection"] = m_AppendImportToNewCollection;

        root["viewport"]["grid"]["enabled"] = m_SceneSettings.drawGrid;
        root["viewport"]["grid"]["showCellEdges"] = m_ShowCellEdges;
        root["viewport"]["grid"]["halfExtent"] = m_SceneSettings.gridHalfExtent;
        root["viewport"]["grid"]["spacing"] = m_SceneSettings.gridSpacing;
        root["viewport"]["grid"]["lineWidth"] = m_SceneSettings.gridLineWidth;
        root["viewport"]["grid"]["origin"] = MakeVec3Node(glm::vec3(m_SceneSettings.gridOrigin.x, m_SceneSettings.gridOrigin.y, 0.0f));
        root["viewport"]["grid"]["color"] = MakeColorNode(m_SceneSettings.gridColor);
        root["viewport"]["grid"]["opacity"] = m_SceneSettings.gridOpacity;
        root["viewport"]["grid"]["cellEdgeColor"] = MakeColorNode(m_CellEdgeColor);
        root["viewport"]["grid"]["cellEdgeLineWidth"] = m_CellEdgeLineWidth;

        root["viewport"]["lighting"]["direction"] = MakeVec3Node(m_SceneSettings.lightDirection);
        root["viewport"]["lighting"]["ambientStrength"] = m_SceneSettings.ambientStrength;
        root["viewport"]["lighting"]["diffuseStrength"] = m_SceneSettings.diffuseStrength;
        root["viewport"]["lighting"]["color"] = MakeColorNode(m_SceneSettings.lightColor);
        root["viewport"]["lighting"]["position"] = MakeVec3Node(m_LightPosition);

        root["viewport"]["atoms"]["brightness"] = m_SceneSettings.atomBrightness;
        root["viewport"]["atoms"]["glowStrength"] = m_SceneSettings.atomGlowStrength;
        root["viewport"]["atoms"]["selectedCustomColor"] = MakeColorNode(m_SelectedAtomCustomColor);

        root["viewport"]["bonds"]["autoGenerate"] = m_AutoBondGenerationEnabled;
        root["viewport"]["bonds"]["showLengthLabels"] = m_ShowBondLengthLabels;
        root["viewport"]["bonds"]["renderStyle"] = static_cast<int>(m_BondRenderStyle);
        root["viewport"]["bonds"]["deleteLabelOnlyMode"] = m_BondLabelDeleteOnlyMode;
        root["viewport"]["bonds"]["thresholdScale"] = m_BondThresholdScale;
        root["viewport"]["bonds"]["usePairThresholdOverrides"] = m_BondUsePairThresholdOverrides;
        root["viewport"]["bonds"]["pairThresholdOverrides"] = SerializePairScaleOverrides(m_BondPairThresholdScaleOverrides);
        root["viewport"]["bonds"]["lineWidth"] = m_BondLineWidth;
        root["viewport"]["bonds"]["lineWidthMin"] = m_BondLineWidthMin;
        root["viewport"]["bonds"]["lineWidthMax"] = m_BondLineWidthMax;
        root["viewport"]["bonds"]["color"] = MakeColorNode(m_BondColor);
        root["viewport"]["bonds"]["selectedColor"] = MakeColorNode(m_BondSelectedColor);
        root["viewport"]["bonds"]["labels"]["textColor"] = MakeColorNode(m_BondLabelTextColor);
        root["viewport"]["bonds"]["labels"]["backgroundColor"] = MakeColorNode(m_BondLabelBackgroundColor);
        root["viewport"]["bonds"]["labels"]["borderColor"] = MakeColorNode(m_BondLabelBorderColor);
        root["viewport"]["bonds"]["labels"]["precision"] = m_BondLabelPrecision;

        root["viewport"]["selection"]["filter"] = static_cast<int>(m_SelectionFilter);
        root["viewport"]["selection"]["color"] = MakeColorNode(m_SelectionColor);
        root["viewport"]["selection"]["outlineThickness"] = m_SelectionOutlineThickness;
        root["viewport"]["selection"]["debugToFile"] = m_SelectionDebugToFile;

        root["viewport"]["input"]["invertViewportZoom"] = m_InvertViewportZoom;
        root["viewport"]["input"]["invertCircleSelectWheel"] = m_InvertCircleSelectWheel;
        root["viewport"]["input"]["circleSelectWheelStep"] = m_CircleSelectWheelStep;

        root["viewport"]["cursor"]["show"] = m_Show3DCursor;
        root["viewport"]["cursor"]["position"] = MakeVec3Node(m_CursorPosition);
        root["viewport"]["cursor"]["visualScale"] = m_CursorVisualScale;
        root["viewport"]["cursor"]["snapToGrid"] = m_CursorSnapToGrid;

        root["viewport"]["measurements"]["show"] = m_ShowSelectionMeasurements;
        root["viewport"]["measurements"]["showDistance"] = m_ShowSelectionDistanceMeasurement;
        root["viewport"]["measurements"]["showAngle"] = m_ShowSelectionAngleMeasurement;
        root["viewport"]["measurements"]["showStaticAngleLabels"] = m_ShowStaticAngleLabels;
        root["viewport"]["measurements"]["precision"] = m_MeasurementPrecision;
        root["viewport"]["measurements"]["textColor"] = MakeColorNode(m_MeasurementTextColor);
        root["viewport"]["measurements"]["backgroundColor"] = MakeColorNode(m_MeasurementBackgroundColor);

        root["viewport"]["scene"]["origin"] = MakeVec3Node(m_SceneOriginPosition);

        root["viewport"]["gizmos"]["viewEnabled"] = m_ViewGuizmoEnabled;
        root["viewport"]["gizmos"]["viewDragMode"] = m_ViewGizmoDragMode;
        root["viewport"]["gizmos"]["transformSize"] = m_TransformGizmoSize;
        root["viewport"]["gizmos"]["viewScale"] = m_ViewGizmoScale;
        root["viewport"]["gizmos"]["offsetRight"] = m_ViewGizmoOffsetRight;
        root["viewport"]["gizmos"]["offsetTop"] = m_ViewGizmoOffsetTop;
        root["viewport"]["gizmos"]["rotateStepDegrees"] = m_ViewportRotateStepDeg;
        root["viewport"]["gizmos"]["fallbackMarkerScale"] = m_FallbackGizmoVisualScale;
        root["viewport"]["gizmos"]["showTransformEmpties"] = m_ShowTransformEmpties;
        root["viewport"]["gizmos"]["transformEmptyVisualScale"] = m_TransformEmptyVisualScale;
        root["viewport"]["gizmos"]["showGlobalAxesOverlay"] = m_ShowGlobalAxesOverlay;
        root["viewport"]["gizmos"]["showGlobalAxis"]["x"] = m_ShowGlobalAxis[0];
        root["viewport"]["gizmos"]["showGlobalAxis"]["y"] = m_ShowGlobalAxis[1];
        root["viewport"]["gizmos"]["showGlobalAxis"]["z"] = m_ShowGlobalAxis[2];
        root["viewport"]["gizmos"]["useTemporaryLocalAxes"] = m_UseTemporaryLocalAxes;
        root["viewport"]["gizmos"]["temporaryAxesSource"] = (m_TemporaryAxesSource == TemporaryAxesSource::ActiveEmpty) ? 1 : 0;
        root["viewport"]["gizmos"]["temporaryAxisAtomA"] = m_TemporaryAxisAtomA;
        root["viewport"]["gizmos"]["temporaryAxisAtomB"] = m_TemporaryAxisAtomB;
        root["viewport"]["gizmos"]["temporaryAxisAtomC"] = m_TemporaryAxisAtomC;
        root["viewport"]["gizmos"]["axisColorX"] = MakeColorNode(m_AxisColors[0]);
        root["viewport"]["gizmos"]["axisColorY"] = MakeColorNode(m_AxisColors[1]);
        root["viewport"]["gizmos"]["axisColorZ"] = MakeColorNode(m_AxisColors[2]);

        root["renderImage"]["width"] = m_RenderImageWidth;
        root["renderImage"]["height"] = m_RenderImageHeight;
        root["renderImage"]["format"] = static_cast<int>(m_RenderImageFormat);
        root["renderImage"]["jpegQuality"] = m_RenderJpegQuality;
        root["renderImage"]["cropEnabled"] = m_RenderCropEnabled;
        root["renderImage"]["cropRectNormalized"] = MakeArray4Node(m_RenderCropRectNormalized);
        root["renderImage"]["showBondLengthLabels"] = m_RenderShowBondLengthLabels;
        root["renderImage"]["bondLabelScaleMultiplier"] = m_RenderBondLabelScaleMultiplier;
        root["renderImage"]["bondLabelPrecision"] = m_RenderBondLabelPrecision;
        root["renderImage"]["bondLabelTextColor"] = MakeColorNode(m_RenderBondLabelTextColor);
        root["renderImage"]["bondLabelBackgroundColor"] = MakeColorNode(m_RenderBondLabelBackgroundColor);
        root["renderImage"]["bondLabelBorderColor"] = MakeColorNode(m_RenderBondLabelBorderColor);
        root["renderImage"]["previewLongSideCap"] = m_RenderPreviewLongSideCap;
        root["renderImage"]["dialogAspectLocked"] = m_RenderDialogAspectLocked;

        root["hotkeys"]["addMenu"] = m_HotkeyAddMenu;
        root["hotkeys"]["openRender"] = m_HotkeyOpenRender;
        root["hotkeys"]["toggleSidePanels"] = m_HotkeyToggleSidePanels;
        root["hotkeys"]["deleteSelection"] = m_HotkeyDeleteSelection;
        root["hotkeys"]["hideSelection"] = m_HotkeyHideSelection;
        root["hotkeys"]["boxSelect"] = m_HotkeyBoxSelect;
        root["hotkeys"]["circleSelect"] = m_HotkeyCircleSelect;
        root["hotkeys"]["translateModal"] = m_HotkeyTranslateModal;
        root["hotkeys"]["translateGizmo"] = m_HotkeyTranslateGizmo;
        root["hotkeys"]["rotateGizmo"] = m_HotkeyRotateGizmo;
        root["hotkeys"]["scaleGizmo"] = m_HotkeyScaleGizmo;

        YAML::Emitter emitter;
        emitter.SetIndent(2);
        emitter << root;
        out << emitter.c_str();
    }

    void EditorLayer::SaveSettings() const
    {
        SaveUiSettingsYaml();
        SaveAtomSettings();
        ImGuiLayer::SaveCurrentStyle();
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
