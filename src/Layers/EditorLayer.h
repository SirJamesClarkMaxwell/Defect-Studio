#pragma once

#include "Core/Layer.h"
#include "DataModel/Structure.h"
#include "DataModel/VolumetricDataset.h"
#include "IO/PoscarParser.h"
#include "IO/PoscarSerializer.h"
#include "IO/VaspVolumetricParser.h"
#include "Volumetrics/IsosurfaceExtractor.h"
#include "Renderer/IRenderBackend.h"

#include <BS_thread_pool.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace ds
{
    using SceneUUID = std::uint64_t;

    class IRenderBackend;
    class OrbitCamera;
    class PropertiesPanel;
    class SettingsPanel;
    class SceneGroupingBackend;

    class EditorLayer : public Layer
    {
    public:
        EditorLayer();
        ~EditorLayer() override = default;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTime) override;
        void OnImGuiRender() override;

    private:
        friend class PropertiesPanel;
        friend class SettingsPanel;
        friend class SceneGroupingBackend;

        enum class ThemePreset
        {
            Dark = 0,
            Light = 1,
            Classic = 2,
            PhotoshopStyle = 3,
            WarmSlate = 4
        };

        enum class InteractionMode
        {
            Navigate = 0,
            Select = 1,
            ViewSet = 2,
            Translate = 3,
            Rotate = 4
        };

        enum class TemporaryAxesSource
        {
            SelectionAtoms = 0,
            ActiveEmpty = 1
        };

        enum class SelectionFilter
        {
            AtomsOnly = 0,
            AtomsAndBonds = 1,
            BondsOnly = 2,
            BondLabelsOnly = 3
        };

        enum class SelectionStrokeMode
        {
            Replace = 0,
            Add = 1,
            Subtract = 2
        };

        enum class BondRenderStyle
        {
            UnicolorLine = 0,
            BicolorLine = 1,
            ColorGradient = 2
        };

        enum class PeriodicTableTarget
        {
            AddAtomEntry = 0,
            ChangeSelectedAtoms = 1,
            ElementAppearanceEditor = 2
        };

        enum class RenderImageFormat
        {
            Png = 0,
            Jpg = 1
        };

        struct RenderImageRequest
        {
            std::string outputPath;
            std::uint32_t width = 1920;
            std::uint32_t height = 1080;
            RenderImageFormat format = RenderImageFormat::Png;
            SceneRenderSettings sceneSettings;
            bool showBondLengthLabels = true;
            float bondLabelScaleMultiplier = 1.0f;
            glm::vec3 bondLabelTextColor = glm::vec3(0.84f, 0.88f, 0.93f);
            glm::vec3 bondLabelBackgroundColor = glm::vec3(0.08f, 0.09f, 0.12f);
            glm::vec3 bondLabelBorderColor = glm::vec3(0.72f, 0.76f, 0.88f);
            int bondLabelPrecision = 3;
            bool useCrop = false;
            std::array<float, 4> cropRectNormalized = {0.0f, 0.0f, 1.0f, 1.0f};
        };

        struct VolumetricSurfaceState;

        struct TransformEmpty
        {
            SceneUUID id = 0;
            std::string name;
            glm::vec3 position = glm::vec3(0.0f);
            std::array<glm::vec3, 3> axes = {
                glm::vec3(1.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec3(0.0f, 0.0f, 1.0f)};
            SceneUUID parentEmptyId = 0;
            SceneUUID collectionId = 0;
            int collectionIndex = 0;
            int groupIndex = -1;
            bool visible = true;
            bool selectable = true;
        };

        struct SceneCollection
        {
            SceneUUID id = 0;
            std::string name;
            bool visible = true;
            bool selectable = true;
        };

        struct SceneGroup
        {
            SceneUUID id = 0;
            std::string name;
            std::vector<SceneUUID> atomIds;
            std::vector<std::size_t> atomIndices;
            std::vector<SceneUUID> emptyIds;
            std::vector<int> emptyIndices;
        };

        struct BondSegment
        {
            std::size_t atomA = 0;
            std::size_t atomB = 0;
            glm::vec3 start = glm::vec3(0.0f);
            glm::vec3 end = glm::vec3(0.0f);
            glm::vec3 midpoint = glm::vec3(0.0f);
            float length = 0.0f;
        };

        struct BondLabelState
        {
            std::size_t atomA = 0;
            std::size_t atomB = 0;
            glm::vec3 worldOffset = glm::vec3(0.0f);
            float scale = 1.0f;
            bool hidden = false;
            bool deleted = false;
        };

        struct AngleLabelState
        {
            std::size_t atomA = 0;
            std::size_t atomB = 0;
            std::size_t atomC = 0;
        };

        enum class SpecialNodeSelection
        {
            None = 0,
            Light
        };

        struct EditorSceneSnapshot
        {
            bool hasStructureLoaded = false;
            std::optional<Structure> originalStructure;
            Structure workingStructure;
            std::vector<SceneUUID> atomNodeIds;
            std::vector<int> atomCollectionIndices;
            std::unordered_set<std::size_t> hiddenAtomIndices;
            std::unordered_set<std::uint64_t> hiddenBondKeys;
            std::unordered_set<std::uint64_t> manualBondKeys;
            std::unordered_set<std::uint64_t> deletedBondKeys;
            std::unordered_map<std::uint64_t, BondLabelState> bondLabelStates;
            std::unordered_set<std::uint64_t> selectedBondKeys;
            std::uint64_t selectedBondLabelKey = 0;
            std::unordered_map<std::string, AngleLabelState> angleLabelStates;
            std::unordered_map<std::string, glm::vec3> elementColorOverrides;
            std::unordered_map<std::string, float> elementScaleOverrides;
            std::unordered_map<SceneUUID, glm::vec3> atomColorOverrides;
            std::vector<std::size_t> selectedAtomIndices;
            std::optional<std::size_t> outlinerAtomSelectionAnchor;
            std::vector<TransformEmpty> transformEmpties;
            int activeTransformEmptyIndex = -1;
            int selectedTransformEmptyIndex = -1;
            int transformEmptyCounter = 1;
            std::vector<SceneCollection> collections;
            std::vector<SceneGroup> objectGroups;
            int activeCollectionIndex = 0;
            int activeGroupIndex = -1;
            int collectionCounter = 1;
            int groupCounter = 1;
            SpecialNodeSelection selectedSpecialNode = SpecialNodeSelection::None;
            glm::vec3 lightPosition = glm::vec3(3.0f, -2.0f, 2.5f);
            glm::vec3 cursorPosition = glm::vec3(0.0f);
        };

        struct EditorSceneHistoryEntry
        {
            std::string label;
            EditorSceneSnapshot snapshot;
        };

        struct BondLabelLayoutItem
        {
            std::uint64_t key = 0;
            std::string text;
            glm::vec2 textPos = glm::vec2(0.0f);
            glm::vec2 textSize = glm::vec2(0.0f);
            glm::vec2 boxMin = glm::vec2(0.0f);
            glm::vec2 boxMax = glm::vec2(0.0f);
            float fontSize = 0.0f;
            float depthToCamera = 0.0f;
            bool selected = false;
        };

        struct CameraPreset
        {
            std::string name;
            glm::vec3 target = glm::vec3(0.0f);
            float distance = 6.0f;
            float yaw = 0.6f;
            float pitch = 0.5f;
            float roll = 0.0f;
        };

        struct AtomDefaultsConfig
        {
            glm::vec3 defaultOverrideColor = glm::vec3(0.90f, 0.65f, 0.35f);
            float defaultSize = 0.30f;
            std::unordered_map<std::string, glm::vec3> elementColors;
            std::unordered_map<std::string, float> elementScales;
        };

        struct ClipboardAtom
        {
            SceneUUID sourceAtomId = 0;
            std::string element;
            glm::vec3 positionCartesian = glm::vec3(0.0f);
            int collectionIndex = 0;
            bool hasCustomColorOverride = false;
            glm::vec3 customColorOverride = glm::vec3(0.0f);
        };

        struct ClipboardEmpty
        {
            SceneUUID sourceId = 0;
            TransformEmpty empty;
        };

        enum class ClipboardPayloadKind
        {
            None = 0,
            Selection = 1,
            Collection = 2
        };

        struct EditorClipboard
        {
            ClipboardPayloadKind kind = ClipboardPayloadKind::None;
            std::string label;
            int sourceCollectionIndex = 0;
            glm::vec3 sourcePivot = glm::vec3(0.0f);
            std::vector<ClipboardAtom> atoms;
            std::vector<ClipboardEmpty> empties;
        };

        static constexpr const char *kDefaultConfigPath = "config/default.yaml";
        static constexpr const char *kUiSettingsPath = "config/ui_settings.yaml";
        static constexpr const char *kLegacyUiSettingsPath = "config/ui_settings.ini";
        static constexpr const char *kLegacyEditorSettingsPath = "config/editor_ui_settings.ini";
        static constexpr const char *kSceneStatePath = "config/scene_state.ini";
        static constexpr const char *kAtomSettingsPath = "config/atom_settings.yaml";
        static constexpr const char *kLegacyAtomSettingsPath = "config/atom_catalog.yaml";
        static constexpr const char *kLegacyAtomSettingsIniPath = "config/atom_settings.ini";
        static constexpr const char *kProjectConfigDirectory = "config/project";
        static constexpr const char *kProjectAppearancePath = "config/project/project_appearance.yaml";
        static constexpr const char *kProjectManifestPath = "project.yaml";
        static constexpr const char *kFallbackStartupImportPath = "assets/samples/reduced_diamond_bulk.vasp";
        static constexpr std::size_t kMaxRecentProjects = 8;

        void ApplyTheme(ThemePreset preset);
        void ApplyFontScale(float scale);
        void ApplyCameraSensitivity();
        void ApplyAtomDefaultsToSceneSettings();
        void LoadDefaultConfigYaml();
        void MigrateLegacyAtomIniIfNeeded();
        void MigrateLegacyUiIniIfNeeded();
        void LoadLegacyAtomSettingsIni(const std::string &path);
        void LoadLegacyUiSettingsIni(const std::string &path);
        void LoadAtomSettingsYaml();
        void SaveAtomSettingsYaml() const;
        void LoadUiSettingsYaml();
        void SaveUiSettingsYaml() const;
        void LoadUiSettingsYamlFromPath(const std::filesystem::path &settingsPath, bool includeProjectState);
        void SaveUiSettingsYamlToPath(const std::filesystem::path &settingsPath, bool includeProjectState) const;
        void LoadProjectAppearanceYaml();
        void SaveProjectAppearanceYaml() const;
        void MigrateLegacyProjectAppearanceFromSceneStateIfNeeded();
        void SanitizeLoadedUiState();
        void LoadAtomSettings();
        void SaveAtomSettings() const;
        void SaveSettings() const;
        void LoadSettings();
        void SaveSceneState() const;
        void LoadSceneState();
        void SyncRenderAppearanceFromViewport();
        glm::vec3 ResolveElementColor(const std::string &element) const;
        float ResolveElementVisualScale(const std::string &element) const;
        SelectionStrokeMode ResolveSelectionStrokeMode(bool additiveSelection) const;
        std::filesystem::path GetAppRootPath() const;
        std::filesystem::path GetAppUiSettingsFilePath() const;
        std::filesystem::path GetProjectRootPath() const;
        std::filesystem::path GetProjectUiSettingsFilePath() const;
        std::filesystem::path ResolveProjectPath(const std::filesystem::path &relativePath) const;
        std::filesystem::path GetProjectConfigDirectoryPath() const;
        std::filesystem::path GetProjectAppearanceFilePath() const;
        std::filesystem::path GetProjectSceneStateFilePath() const;
        std::filesystem::path GetProjectManifestFilePath() const;
        std::filesystem::path ResolveProjectStructurePath() const;
        void AddRecentProjectPath(const std::filesystem::path &path);
        void SaveProjectManifest() const;
        void LoadProjectManifest();
        void LoadProjectVolumetricDatasets();
        bool CreateProjectAt(const std::filesystem::path &folderPath);
        bool OpenProjectAt(const std::filesystem::path &folderPath);
        void ResetProjectSceneState();
        void EnsureElementAppearanceSelection();
        bool ApplyParsedStructureToScene(const Structure &parsed, const std::string &sourcePath, bool updateProjectStructurePath);
        std::filesystem::path GetPreferredStructureDialogDirectory() const;
        std::filesystem::path GetPreferredVolumetricDialogDirectory() const;
        const char *ThemeName(ThemePreset preset) const;
        bool LoadStructureFromPath(const std::string &path);
        bool AppendStructureFromPathAsCollection(const std::string &path);
        bool ExportStructureToPath(const std::string &path, CoordinateMode mode, int precision);
        bool BuildCollectionExportStructure(int collectionIndex, Structure &outStructure) const;
        bool ExportCollectionToPath(int collectionIndex, const std::string &path, CoordinateMode mode, int precision);
        bool LoadVolumetricDatasetFromPath(const std::string &path);
        bool QueueVolumetricDatasetLoad(const std::string &path, bool autoApplyStructureIfNeeded = true, bool persistToProject = true);
        bool QueueVolumetricBlockLoad(int datasetIndex, int blockIndex, bool forceRetry = false);
        void PumpVolumetricLoadingJobs();
        void PumpVolumetricSurfaceBuildJobs();
        bool QueueVolumetricSurfaceBuild(int surfaceSlot);
        bool HasPendingVolumetricDatasetLoad(const std::string &normalizedPath) const;
        bool HasPendingVolumetricBlockLoad(const std::string &datasetPath, int blockIndex) const;
        bool RemoveVolumetricDatasetAtIndex(int datasetIndex);
        void ClearVolumetricDatasets();
        void EnsureVolumetricSelection();
        bool AddAtomToStructure(const std::string &elementSymbol, const glm::vec3 &position, CoordinateMode inputMode);
        bool ApplyElementToSelectedAtoms(const std::string &elementInput, std::size_t *outChangedCount = nullptr);
        bool IsAtomSelected(std::size_t index) const;
        bool IsAtomHidden(std::size_t index) const;
        void ToggleInteractionMode();
        void HandleViewportSelection();
        void SelectAtomsInScreenRect(const glm::vec2 &screenStart, const glm::vec2 &screenEnd, SelectionStrokeMode strokeMode);
        void SelectBondsInScreenRect(const glm::vec2 &screenStart, const glm::vec2 &screenEnd, SelectionStrokeMode strokeMode);
        void SelectAtomsInScreenCircle(const glm::vec2 &screenCenter, float screenRadius, SelectionStrokeMode strokeMode);
        void SelectBondsInScreenCircle(const glm::vec2 &screenCenter, float screenRadius, SelectionStrokeMode strokeMode);
        void AppendSelectionDebugLog(const std::string &message) const;
        bool PickAtomAtScreenPoint(const glm::vec2 &mousePos, std::size_t &outAtomIndex) const;
        bool PickBondAtScreenPoint(const glm::vec2 &mousePos, std::uint64_t &outBondKey) const;
        bool PickTransformEmptyAtScreenPoint(const glm::vec2 &mousePos, std::size_t &outEmptyIndex) const;
        glm::vec3 GetAtomCartesianPosition(std::size_t atomIndex) const;
        void SetAtomCartesianPosition(std::size_t atomIndex, const glm::vec3 &position);
        glm::vec3 ComputeSelectionCenter() const;
        bool ComputeSelectionAxesAround(const glm::vec3 &pivot, std::array<glm::vec3, 3> &outAxes) const;
        bool HasActiveTransformEmpty() const;
        bool IsCollectionVisible(int collectionIndex) const;
        bool IsCollectionSelectable(int collectionIndex) const;
        bool IsAtomCollectionVisible(std::size_t atomIndex) const;
        bool IsAtomCollectionSelectable(std::size_t atomIndex) const;
        int ResolveAtomCollectionIndex(std::size_t atomIndex) const;
        void EnsureAtomCollectionAssignments();
        void EnsureSceneDefaults();
        void DeleteTransformEmptyAtIndex(int emptyIndex);
        bool AlignEmptyZAxisFromSelectedAtoms(int emptyIndex);
        bool AlignEmptyAxesToCameraView(int emptyIndex);
        std::array<glm::vec3, 3> ResolveTransformAxes(const glm::vec3 &pivot) const;
        bool BuildAxesFromPoints(const std::vector<glm::vec3> &points, const glm::vec3 &pivot, std::array<glm::vec3, 3> &outAxes) const;
        bool ResolveTemporaryLocalAxes(std::array<glm::vec3, 3> &outAxes) const;
        bool Set3DCursorToSelectionCenterOfMass();
        bool Set3DCursorToSelectedAtom(bool useLastSelected);
        void EnsureAtomNodeIds();
        bool PickWorldPositionOnGrid(const glm::vec2 &mousePos, glm::vec3 &outWorldPosition) const;
        void Set3DCursorFromScreenPoint(const glm::vec2 &mousePos);
        void OpenPeriodicTable(PeriodicTableTarget target, bool openedFromContextMenu = false);
        std::string ResolvePeriodicTableActiveElement(PeriodicTableTarget target) const;
        void ApplyPeriodicTableSelection(const char *symbol, PeriodicTableTarget target);
        void DrawPeriodicTableSelector(const char *instanceId, PeriodicTableTarget target, bool closeAfterSelection);
        void DrawPeriodicTableWindow();
        void DrawChangeAtomTypeConfirmDialog();
        void DrawRenderImageDialog(bool &settingsChanged);
        void DrawRenderPreviewWindow(bool &settingsChanged);
        void DrawElementAppearanceWindow(bool &settingsChanged);
        void DrawVolumetricsWindow(bool &settingsChanged);
        bool RotateCameraRelative(float yawDeltaRadians, float pitchDeltaRadians, float rollDeltaRadians = 0.0f);
        bool PanCameraRelativePixels(float deltaXPixels, float deltaYPixels);
        bool ZoomCameraRelativePercent(float zoomPercentDelta);
        bool SetCameraViewToCrystalAxis(int axisIndex, bool reciprocalAxis, bool invertDirection);
        std::string DescribeVolumetricStructureMatch(const Structure &datasetStructure, bool &outMatches) const;
        void MarkVolumetricMeshesDirty();
        void SyncVolumetricSurfaceDefaults();
        bool RebuildVolumetricSurfaceMesh(struct VolumetricSurfaceState &surfaceState);
        void EnsureVolumetricSurfaceMeshes();
        void RenderVolumetricSurfaces(IRenderBackend &backend, const OrbitCamera &camera, const SceneRenderSettings &settings);
        void ApplyDefaultDockLayout(unsigned int dockspaceId);
        bool HasClipboardPayload() const;
        bool CopyCurrentSelectionToClipboard();
        bool CopyCollectionToClipboard(int collectionIndex);
        bool PasteClipboard(bool applyPlacementOffset = true);
        bool DuplicateCurrentSelection();
        bool DuplicateCollection(int collectionIndex);
        bool ExtractSelectionToNewCollection();
        bool DeleteCollectionAtIndex(int collectionIndex, std::string *outStatusMessage = nullptr);
        void BeginCollectionRename(int collectionIndex);
        void ResetProjectAppearanceOverrides();
        bool ImportProjectAppearanceOverrides(const std::string &path);
        bool ExportProjectAppearanceOverrides(const std::string &path) const;
        EditorSceneSnapshot CaptureSceneSnapshot() const;
        void RestoreSceneSnapshot(const EditorSceneSnapshot &snapshot);
        void PushUndoSnapshot(std::string label);
        void PushUndoSnapshot(std::string label, const EditorSceneSnapshot &snapshot);
        bool UndoSceneEdit();
        bool RedoSceneEdit();
        void ClearSceneHistory();
        bool SaveCurrentFrameAsImage(
            const std::string &outputPath,
            std::uint32_t width,
            std::uint32_t height,
            RenderImageFormat format,
            bool useCrop,
            const std::array<float, 4> &cropRectNormalized) const;
        std::vector<BondLabelLayoutItem> BuildBondLabelLayout(
            const OrbitCamera &camera,
            std::uint32_t sourceWidth,
            std::uint32_t sourceHeight,
            const glm::vec2 &targetRectMin,
            const glm::vec2 &targetRectMax,
            bool showLabels,
            float labelScaleMultiplier,
            int labelPrecision,
            bool useCrop,
            const std::array<float, 4> &cropRectNormalized) const;
        void RebuildAutoBonds(const std::vector<glm::vec3> &atomCartesianPositions);
        float ResolveBondThresholdScaleForPair(const std::string &elementA, const std::string &elementB) const;
        void StartCameraOrbitTransition(const glm::vec3 &target, float distance, float yaw, float pitch, std::optional<float> roll = std::nullopt);
        void UpdateCameraOrbitTransition(float deltaTime);
        bool SelectAllVisibleByCurrentFilter();
        bool FocusCameraOnCursor(float distanceFactorMultiplier = 1.0f, bool persistAdjustment = false);

        bool m_ShowDemoWindow = false;
        bool m_ShowLogPanel = true;
        bool m_ShowStatsPanel = true;
        bool m_ShowViewportInfoPanel = true;
        bool m_ShowShortcutReferencePanel = false;
        bool m_ShowElementCatalogPanel = false;
        bool m_ShowVolumetricsPanel = false;
        ThemePreset m_CurrentTheme = ThemePreset::Dark;
        float m_FontScale = 1.0f;
        int m_LogFilter = 0;
        bool m_LogAutoScroll = true;
        bool m_ApplyDefaultDockLayoutOnNextFrame = false;
        bool m_RequestDockLayoutReset = false;

        bool m_ViewportHovered = false;
        bool m_ViewportFocused = false;
        glm::vec2 m_ViewportSize = glm::vec2(1.0f, 1.0f);
        glm::vec2 m_ViewportRectMin = glm::vec2(0.0f, 0.0f);
        glm::vec2 m_ViewportRectMax = glm::vec2(0.0f, 0.0f);

        float m_CameraOrbitSensitivity = 1.0f;
        float m_CameraPanSensitivity = 1.0f;
        float m_CameraZoomSensitivity = 1.0f;

        PoscarParser m_PoscarParser;
        PoscarSerializer m_PoscarSerializer;
        VaspVolumetricParser m_VaspVolumetricParser;
        std::optional<Structure> m_OriginalStructure;
        Structure m_WorkingStructure;
        bool m_HasStructureLoaded = false;
        std::vector<VolumetricDataset> m_VolumetricDatasets;
        std::string m_AppRootPath;
        std::string m_ProjectRootPath;
        std::string m_ProjectName = "Default Project";
        std::string m_ProjectStructurePath;
        std::vector<std::string> m_ProjectVolumetricPaths;
        std::vector<std::string> m_RecentProjectPaths;

        std::array<char, 512> m_ImportPathBuffer = {};
        std::array<char, 512> m_ExportPathBuffer = {};
        std::array<char, 512> m_VolumetricImportPathBuffer = {};
        std::array<char, 512> m_RenderImagePathBuffer = {};
        int m_ExportPrecision = 8;
        int m_ExportCoordinateModeIndex = 0;
        int m_RenderImageWidth = 1920;
        int m_RenderImageHeight = 1080;
        RenderImageFormat m_RenderImageFormat = RenderImageFormat::Png;
        bool m_RenderDialogAspectLocked = false;
        bool m_ShowRenderImageDialog = false;
        bool m_ShowRenderPreviewWindow = true;
        int m_RenderJpegQuality = 92;
        SceneRenderSettings m_RenderSceneSettings;
        bool m_RenderCropEnabled = false;
        std::array<float, 4> m_RenderCropRectNormalized = {0.0f, 0.0f, 1.0f, 1.0f};
        bool m_RenderShowBondLengthLabels = true;
        float m_RenderBondLabelScaleMultiplier = 1.0f;
        int m_RenderBondLabelPrecision = 3;
        glm::vec3 m_RenderBondLabelTextColor = glm::vec3(0.84f, 0.88f, 0.93f);
        glm::vec3 m_RenderBondLabelBackgroundColor = glm::vec3(0.08f, 0.09f, 0.12f);
        glm::vec3 m_RenderBondLabelBorderColor = glm::vec3(0.72f, 0.76f, 0.88f);
        glm::vec2 m_RenderPreviewContentSize = glm::vec2(640.0f, 360.0f);
        std::uint32_t m_RenderPreviewTargetWidth = 1280;
        std::uint32_t m_RenderPreviewTargetHeight = 720;
        float m_RenderPreviewLongSideCap = 1600.0f;
        RenderImageRequest m_RenderImageRequest;
        std::array<char, 16> m_AddAtomElementBuffer = {'S', 'i', '\0'};
        std::array<char, 16> m_ChangeAtomElementBuffer = {'G', 'e', '\0'};
        glm::vec3 m_AddAtomPosition = glm::vec3(0.0f);
        float m_AddAtomUniformPositionValue = 0.0f;
        int m_AddAtomCoordinateModeIndex = 1;
        bool m_ShowPeriodicTablePanel = false;
        bool m_PeriodicTableOpen = false;
        bool m_RequestPeriodicTableFocus = false;
        PeriodicTableTarget m_PeriodicTableTarget = PeriodicTableTarget::AddAtomEntry;
        std::array<char, 16> m_PendingChangeAtomElementBuffer = {};
        bool m_ChangeAtomTypeConfirmOpen = false;
        bool m_PeriodicTableOpenedFromContextMenu = false;
        bool m_ReopenViewportSelectionContextMenu = false;
        bool m_ShowAddAtomDialog = false;
        bool m_AutoBondGenerationEnabled = true;
        bool m_AutoRecalculateBondsOnEdit = true;
        bool m_AutoBondsDirty = true;
        bool m_ShowBondLengthLabels = true;
        float m_BondThresholdScale = 1.12f;
        float m_BondLineWidth = 2.0f;
        glm::vec3 m_BondColor = glm::vec3(0.72f, 0.78f, 0.90f);
        glm::vec3 m_BondSelectedColor = glm::vec3(0.98f, 0.88f, 0.42f);
        BondRenderStyle m_BondRenderStyle = BondRenderStyle::UnicolorLine;
        std::vector<BondSegment> m_GeneratedBonds;
        std::unordered_map<std::uint64_t, BondLabelState> m_BondLabelStates;
        std::unordered_set<std::uint64_t> m_SelectedBondKeys;
        std::unordered_set<std::uint64_t> m_DeletedBondKeys;
        std::uint64_t m_SelectedBondLabelKey = 0;
        SelectionFilter m_SelectionFilter = SelectionFilter::AtomsAndBonds;
        bool m_BondLabelDeleteOnlyMode = false;
        bool m_BondLabelGizmoEnabled = true;
        int m_BondLabelGizmoOperation = 0;
        float m_BondLabelMultiScaleValue = 1.0f;
        bool m_BondUsePairThresholdOverrides = true;
        std::unordered_map<std::string, float> m_BondPairThresholdScaleOverrides;
        glm::vec3 m_BondLabelTextColor = glm::vec3(0.84f, 0.88f, 0.93f);
        glm::vec3 m_BondLabelBackgroundColor = glm::vec3(0.08f, 0.09f, 0.12f);
        glm::vec3 m_BondLabelBorderColor = glm::vec3(0.72f, 0.76f, 0.88f);
        int m_BondLabelPrecision = 3;
        bool m_ShowSelectionMeasurements = true;
        bool m_ShowSelectionDistanceMeasurement = true;
        bool m_ShowSelectionAngleMeasurement = true;
        glm::vec3 m_MeasurementTextColor = glm::vec3(0.95f, 0.95f, 0.80f);
        glm::vec3 m_MeasurementBackgroundColor = glm::vec3(0.08f, 0.10f, 0.14f);
        int m_MeasurementPrecision = 3;
        std::unordered_set<std::size_t> m_HiddenAtomIndices;
        std::unordered_set<std::uint64_t> m_HiddenBondKeys;
        std::unordered_set<std::uint64_t> m_ManualBondKeys;
        std::unordered_map<std::string, AngleLabelState> m_AngleLabelStates;
        bool m_ShowStaticAngleLabels = true;
        glm::vec3 m_SelectedAtomCustomColor = glm::vec3(0.95f, 0.42f, 0.42f);
        std::unordered_map<std::string, glm::vec3> m_ElementColorOverrides;
        std::unordered_map<std::string, float> m_ElementScaleOverrides;
        std::unordered_map<SceneUUID, glm::vec3> m_AtomColorOverrides;
        std::string m_ElementCatalogSelectedSymbol = "C";
        bool m_ElementCatalogFollowViewportSelection = false;
        int m_ActiveVolumetricDatasetIndex = -1;
        int m_ActiveVolumetricBlockIndex = 0;
        int m_VolumetricPreviewMaxDimension = 96;
        float m_ViewportPanStepPixels = 10.0f;
        float m_ViewportZoomStepPercent = 10.0f;
        bool m_LastVolumetricOperationFailed = false;
        std::string m_LastVolumetricMessage;
        struct VolumetricSurfaceState
        {
            bool enabled = true;
            int blockIndex = 0;
            VolumetricFieldMode fieldMode = VolumetricFieldMode::SelectedBlock;
            VolumetricIsosurfaceMode isosurfaceMode = VolumetricIsosurfaceMode::PositiveOnly;
            float isoValue = 0.0f;
            glm::vec3 color = glm::vec3(1.0f, 0.92f, 0.14f);
            float opacity = 0.90f;
            glm::vec3 negativeColor = glm::vec3(0.10f, 0.22f, 0.96f);
            float negativeOpacity = 0.90f;
            bool dirty = true;
            bool buildQueued = false;
            bool hasMesh = false;
            bool hasNegativeMesh = false;
            glm::ivec3 sampledDimensions = glm::ivec3(0);
            int decimationStep = 1;
            double lastBuildMilliseconds = 0.0;
            std::string lastStatus;
            SurfaceTriangleMesh mesh;
            SurfaceTriangleMesh negativeMesh;
            std::uint64_t meshRevision = 1;
            std::uint64_t negativeMeshRevision = 1;
            std::uint64_t pendingBuildRequestId = 0;
        };
        struct VolumetricDatasetLoadResult
        {
            std::uint64_t generation = 0;
            std::string path;
            VolumetricDataset dataset;
            std::string error;
            double wallMilliseconds = 0.0;
            bool success = false;
        };
        struct PendingVolumetricDatasetLoad
        {
            std::string normalizedPath;
            std::future<VolumetricDatasetLoadResult> future;
        };
        struct VolumetricBlockLoadResult
        {
            std::uint64_t generation = 0;
            std::string datasetPath;
            int blockIndex = -1;
            ScalarFieldBlock block;
            std::string error;
            double wallMilliseconds = 0.0;
            bool success = false;
        };
        struct PendingVolumetricBlockLoad
        {
            std::string datasetPath;
            int blockIndex = -1;
            std::future<VolumetricBlockLoadResult> future;
        };
        struct VolumetricSurfaceBuildResult
        {
            std::uint64_t generation = 0;
            int surfaceSlot = -1;
            std::uint64_t requestId = 0;
            std::string datasetPath;
            int blockIndex = -1;
            SurfaceTriangleMesh mesh;
            SurfaceTriangleMesh negativeMesh;
            glm::ivec3 sampledDimensions = glm::ivec3(0);
            int decimationStep = 1;
            std::string error;
            double wallMilliseconds = 0.0;
            bool success = false;
        };
        struct PendingVolumetricSurfaceBuild
        {
            int surfaceSlot = -1;
            std::uint64_t requestId = 0;
            std::future<VolumetricSurfaceBuildResult> future;
        };
        IsosurfaceExtractor m_IsosurfaceExtractor;
        BS::thread_pool<> m_BackgroundThreadPool;
        VolumetricSurfaceState m_PrimaryVolumetricSurface;
        VolumetricSurfaceState m_SecondaryVolumetricSurface = []()
        {
            VolumetricSurfaceState state;
            state.enabled = false;
            state.blockIndex = 1;
            state.fieldMode = VolumetricFieldMode::SelectedBlock;
            state.isosurfaceMode = VolumetricIsosurfaceMode::NegativeOnly;
            state.color = glm::vec3(0.10f, 0.22f, 0.96f);
            state.negativeColor = glm::vec3(0.10f, 0.22f, 0.96f);
            state.opacity = 0.90f;
            state.negativeOpacity = 0.90f;
            return state;
        }();
        glm::vec3 m_VolumetricSpecularColor = glm::vec3(0.0f);
        float m_VolumetricShininess = 100.0f;
        std::string m_VolumetricSurfaceDatasetKey;
        std::vector<PendingVolumetricDatasetLoad> m_PendingVolumetricDatasetLoads;
        std::vector<PendingVolumetricBlockLoad> m_PendingVolumetricBlockLoads;
        std::vector<PendingVolumetricSurfaceBuild> m_PendingVolumetricSurfaceBuilds;
        std::uint64_t m_VolumetricLoadGeneration = 1;
        std::uint64_t m_VolumetricSurfaceBuildRequestCounter = 1;
        std::size_t m_LastLoggedBondCount = std::numeric_limits<std::size_t>::max();
        bool m_LastStructureOperationFailed = false;
        std::string m_LastStructureMessage;
        SpecialNodeSelection m_SelectedSpecialNode = SpecialNodeSelection::None;
        glm::vec3 m_SceneOriginPosition = glm::vec3(0.0f);
        glm::vec3 m_LightPosition = glm::vec3(3.0f, -2.0f, 2.5f);
        std::vector<SceneUUID> m_AtomNodeIds;
        std::vector<int> m_AtomCollectionIndices;
        std::vector<std::size_t> m_SelectedAtomIndices;
        std::unordered_set<int> m_SelectedCollectionIndices;
        std::unordered_map<std::string, bool> m_OutlinerTreeOpenStates;
        std::optional<std::size_t> m_OutlinerAtomSelectionAnchor;
        std::optional<std::size_t> m_OutlinerCollectionSelectionAnchor;
        InteractionMode m_InteractionMode = InteractionMode::Navigate;
        glm::vec3 m_SelectionColor = glm::vec3(0.95f, 0.85f, 0.25f);
        float m_SelectionOutlineThickness = 2.0f;
        bool m_GizmoEnabled = true;
        bool m_ViewGuizmoEnabled = true;
        int m_GizmoOperationIndex = 0;
        int m_GizmoModeIndex = 1;
        bool m_UseTemporaryLocalAxes = false;
        TemporaryAxesSource m_TemporaryAxesSource = TemporaryAxesSource::SelectionAtoms;
        int m_TemporaryAxisAtomA = -1;
        int m_TemporaryAxisAtomB = -1;
        int m_TemporaryAxisAtomC = -1;
        std::vector<TransformEmpty> m_TransformEmpties;
        int m_ActiveTransformEmptyIndex = -1;
        int m_SelectedTransformEmptyIndex = -1;
        int m_TransformEmptyCounter = 1;
        std::vector<SceneCollection> m_Collections;
        std::vector<SceneGroup> m_ObjectGroups;
        int m_ActiveCollectionIndex = 0;
        int m_ActiveGroupIndex = -1;
        int m_CollectionCounter = 1;
        int m_GroupCounter = 1;
        bool m_ShowSceneOutlinerPanel = true;
        bool m_ShowObjectPropertiesPanel = true;
        bool m_ShowSettingsPanel = true;
        bool m_ShowTransformEmpties = true;
        float m_TransformEmptyVisualScale = 0.20f;
        std::array<glm::vec3, 3> m_AxisColors = {
            glm::vec3(0.90f, 0.27f, 0.27f),
            glm::vec3(0.27f, 0.90f, 0.27f),
            glm::vec3(0.27f, 0.47f, 0.96f)};
        bool m_ShowGlobalAxesOverlay = true;
        std::array<bool, 3> m_ShowGlobalAxis = {true, true, true};
        bool m_GizmoSnapEnabled = false;
        float m_GizmoTranslateSnap = 0.1f;
        float m_GizmoRotateSnapDeg = 15.0f;
        float m_GizmoScaleSnap = 0.1f;
        float m_TransformGizmoSize = 0.24f;
        float m_ViewGizmoScale = 0.72f;
        float m_ViewGizmoOffsetRight = 16.0f;
        float m_ViewGizmoOffsetTop = 72.0f;
        float m_ViewportRotateStepDeg = 15.0f;
        bool m_SelectionDebugToFile = true;
        bool m_BoxSelectArmed = false;
        bool m_BoxSelecting = false;
        bool m_CircleSelectArmed = false;
        bool m_CircleSelecting = false;
        float m_CircleSelectRadius = 52.0f;
        bool m_BlockSelectionThisFrame = false;
        bool m_ShowActionsPanel = true;
        bool m_ShowAppearancePanel = true;
        bool m_RenameCollectionDialogOpen = false;
        std::array<char, 128> m_RenameCollectionBuffer = {};
        int m_RenameCollectionTargetIndex = -1;
        bool m_GizmoConsumedMouseThisFrame = false;
        bool m_FallbackGizmoDragging = false;
        float m_FallbackGizmoVisualScale = 2.0f;
        bool m_TranslateModeActive = false;
        int m_TranslateConstraintAxis = -1;
        int m_TranslatePlaneLockAxis = -1;
        int m_TranslateSpecialNode = 0;
        glm::vec2 m_TranslateLastMousePos = glm::vec2(0.0f, 0.0f);
        std::vector<std::size_t> m_TranslateIndices;
        std::vector<glm::vec3> m_TranslateInitialCartesian;
        int m_TranslateEmptyIndex = -1;
        glm::vec3 m_TranslateEmptyInitialPosition = glm::vec3(0.0f);
        glm::vec3 m_TranslateCurrentOffset = glm::vec3(0.0f);
        std::string m_TranslateTypedDistanceBuffer;
        bool m_TranslateTypedDistanceActive = false;
        bool m_RotateModeActive = false;
        int m_RotateConstraintAxis = -1;
        glm::vec2 m_RotateLastMousePos = glm::vec2(0.0f, 0.0f);
        std::vector<std::size_t> m_RotateIndices;
        std::vector<glm::vec3> m_RotateInitialCartesian;
        glm::vec3 m_RotatePivot = glm::vec3(0.0f);
        float m_RotateCurrentAngle = 0.0f;
        glm::vec2 m_ModePiePopupPos = glm::vec2(0.0f, 0.0f);
        bool m_ModePieActive = false;
        int m_ModePieHoveredSlice = -1;
        glm::vec2 m_AddMenuPopupPos = glm::vec2(0.0f, 0.0f);
        bool m_ViewGizmoDragMode = false;
        bool m_ViewGizmoDragging = false;
        glm::vec2 m_ViewGizmoDragAnchor = glm::vec2(0.0f, 0.0f);
        float m_ViewGizmoStartOffsetRight = 0.0f;
        float m_ViewGizmoStartOffsetTop = 0.0f;
        glm::vec2 m_FallbackLastMousePos = glm::vec2(0.0f, 0.0f);
        glm::vec2 m_FallbackPivotScreen = glm::vec2(0.0f, 0.0f);
        int m_FallbackGizmoOperation = -1;
        int m_FallbackGizmoAxis = -1;
        glm::vec2 m_FallbackDragAxisScreenDir = glm::vec2(1.0f, 0.0f);
        glm::vec3 m_FallbackDragAxisWorldDir = glm::vec3(1.0f, 0.0f, 0.0f);
        float m_FallbackDragPixelsPerWorld = 1.0f;
        float m_FallbackDragAccumulated = 0.0f;
        float m_FallbackDragApplied = 0.0f;
        float m_FallbackRotateLastAngle = 0.0f;
        glm::vec2 m_BoxSelectStart = glm::vec2(0.0f, 0.0f);
        glm::vec2 m_BoxSelectEnd = glm::vec2(0.0f, 0.0f);
        bool m_Show3DCursor = true;
        glm::vec3 m_CursorPosition = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 m_CursorColor = glm::vec3(0.22f, 0.95f, 0.95f);
        float m_CursorVisualScale = 0.20f;
        float m_CursorFocusDistanceFactor = 1.00f;
        float m_CursorFocusMinDistance = 1.5f;
        float m_CursorFocusSelectionPadding = 2.2f;
        bool m_CursorSnapToGrid = true;
        bool m_TouchpadNavigationEnabled = true;
        bool m_InvertViewportZoom = false;
        bool m_InvertCircleSelectWheel = false;
        float m_CircleSelectWheelStep = 4.0f;
        bool m_DuplicateAppliesOffset = false;
        bool m_AppendImportToNewCollection = true;

        bool m_HasPersistedCameraState = false;
        glm::vec3 m_CameraTargetPersisted = glm::vec3(0.0f, 0.0f, 0.0f);
        float m_CameraDistancePersisted = 6.0f;
        float m_CameraYawPersisted = 0.6f;
        float m_CameraPitchPersisted = 0.5f;
        float m_CameraRollPersisted = 0.0f;

        bool m_CameraTransitionActive = false;
        float m_CameraTransitionElapsed = 0.0f;
        float m_CameraTransitionDuration = 0.25f;
        glm::vec3 m_CameraTransitionStartTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 m_CameraTransitionEndTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        float m_CameraTransitionStartDistance = 6.0f;
        float m_CameraTransitionEndDistance = 6.0f;
        float m_CameraTransitionStartYaw = 0.0f;
        float m_CameraTransitionEndYaw = 0.0f;
        float m_CameraTransitionStartPitch = 0.0f;
        float m_CameraTransitionEndPitch = 0.0f;
        float m_CameraTransitionStartRoll = 0.0f;
        float m_CameraTransitionEndRoll = 0.0f;

        AtomDefaultsConfig m_AtomDefaults;
        std::vector<CameraPreset> m_CameraPresets;
        int m_SelectedCameraPresetIndex = -1;
        std::array<char, 64> m_CameraPresetNameBuffer = {'P', 'r', 'e', 's', 'e', 't', ' ', '1', '\0'};

        SceneRenderSettings m_SceneSettings;
        int m_GridHalfExtentMin = 1;
        int m_GridHalfExtentMax = 64;
        float m_GridSpacingMin = 0.1f;
        float m_GridSpacingMax = 5.0f;
        float m_GridLineWidthMin = 1.0f;
        float m_GridLineWidthMax = 4.0f;
        float m_GridOpacityMin = 0.05f;
        float m_GridOpacityMax = 1.0f;
        bool m_CleanViewMode = false;
        bool m_ShowCellEdges = false;
        glm::vec3 m_CellEdgeColor = glm::vec3(0.78f, 0.86f, 0.94f);
        float m_CellEdgeLineWidth = 1.8f;
        float m_AmbientMin = 0.0f;
        float m_AmbientMax = 1.5f;
        float m_DiffuseMin = 0.0f;
        float m_DiffuseMax = 2.0f;
        float m_RenderScaleMin = 0.25f;
        float m_RenderScaleMax = 1.0f;
        float m_AtomSizeMin = 0.05f;
        float m_AtomSizeMax = 1.25f;
        float m_AtomBrightnessMin = 0.3f;
        float m_AtomBrightnessMax = 2.2f;
        float m_AtomGlowMin = 0.0f;
        float m_AtomGlowMax = 0.60f;
        float m_BondLineWidthMin = 1.0f;
        float m_BondLineWidthMax = 6.0f;
        float m_SelectionOutlineMin = 1.0f;
        float m_SelectionOutlineMax = 8.0f;
        float m_ViewportRenderScale = 1.0f;
        float m_UiSpacingScale = 1.0f;
        float m_CameraClipNearPadding = 2.4f;
        float m_CameraClipFarPadding = 5.0f;
        int m_ProjectionModeIndex = 0;
        bool m_ViewportSettingsOpen = true;
        std::string m_LastProjectDialogPath;
        std::uint32_t m_HotkeyAddMenu = 0;
        std::uint32_t m_HotkeyOpenRender = 0;
        std::uint32_t m_HotkeyToggleSidePanels = 0;
        std::uint32_t m_HotkeyDeleteSelection = 0;
        std::uint32_t m_HotkeyHideSelection = 0;
        std::uint32_t m_HotkeyBoxSelect = 0;
        std::uint32_t m_HotkeyCircleSelect = 0;
        std::uint32_t m_HotkeyTranslateModal = 0;
        std::uint32_t m_HotkeyTranslateGizmo = 0;
        std::uint32_t m_HotkeyRotateGizmo = 0;
        std::uint32_t m_HotkeyScaleGizmo = 0;

        std::unique_ptr<IRenderBackend> m_RenderBackend;
        std::unique_ptr<IRenderBackend> m_RenderPreviewBackend;
        std::unique_ptr<OrbitCamera> m_Camera;
        EditorClipboard m_EditorClipboard;
        static constexpr std::size_t kMaxSceneHistoryEntries = 64;
        std::vector<EditorSceneHistoryEntry> m_UndoStack;
        std::vector<EditorSceneHistoryEntry> m_RedoStack;
        bool m_SuspendUndoCapture = false;
        bool m_PendingTransformUndoValid = false;
        bool m_PendingTransformUndoDirty = false;
        std::string m_PendingTransformUndoLabel;
        EditorSceneSnapshot m_PendingTransformUndoSnapshot;
    };

} // namespace ds
