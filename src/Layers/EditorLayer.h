#pragma once

#include "Core/Layer.h"
#include "DataModel/Structure.h"
#include "IO/PoscarParser.h"
#include "IO/PoscarSerializer.h"
#include "Renderer/IRenderBackend.h"

#include <array>
#include <cstddef>
#include <cstdint>
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

        enum class BondRenderStyle
        {
            UnicolorLine = 0,
            BicolorLine = 1,
            ColorGradient = 2
        };

        enum class PeriodicTableTarget
        {
            AddAtomEntry = 0,
            ChangeSelectedAtoms = 1
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

        enum class SpecialNodeSelection
        {
            None = 0,
            Light
        };

        static constexpr const char *kSettingsPath = "config/editor_ui_settings.ini";
        static constexpr const char *kSceneStatePath = "config/scene_state.ini";

        void ApplyTheme(ThemePreset preset);
        void ApplyFontScale(float scale);
        void ApplyCameraSensitivity();
        void SaveSettings() const;
        void LoadSettings();
        void SaveSceneState() const;
        void LoadSceneState();
        void SyncRenderAppearanceFromViewport();
        const char *ThemeName(ThemePreset preset) const;
        bool LoadStructureFromPath(const std::string &path);
        bool AppendStructureFromPathAsCollection(const std::string &path);
        bool ExportStructureToPath(const std::string &path, CoordinateMode mode, int precision);
        bool AddAtomToStructure(const std::string &elementSymbol, const glm::vec3 &position, CoordinateMode inputMode);
        bool ApplyElementToSelectedAtoms(const std::string &elementInput, std::size_t *outChangedCount = nullptr);
        bool IsAtomSelected(std::size_t index) const;
        bool IsAtomHidden(std::size_t index) const;
        void ToggleInteractionMode();
        void HandleViewportSelection();
        void SelectAtomsInScreenRect(const glm::vec2 &screenStart, const glm::vec2 &screenEnd, bool additiveSelection);
        void SelectBondsInScreenRect(const glm::vec2 &screenStart, const glm::vec2 &screenEnd, bool additiveSelection);
        void SelectAtomsInScreenCircle(const glm::vec2 &screenCenter, float screenRadius, bool additiveSelection);
        void SelectBondsInScreenCircle(const glm::vec2 &screenCenter, float screenRadius, bool additiveSelection);
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
        std::array<glm::vec3, 3> ResolveTransformAxes(const glm::vec3 &pivot) const;
        bool BuildAxesFromPoints(const std::vector<glm::vec3> &points, const glm::vec3 &pivot, std::array<glm::vec3, 3> &outAxes) const;
        bool ResolveTemporaryLocalAxes(std::array<glm::vec3, 3> &outAxes) const;
        bool Set3DCursorToSelectionCenterOfMass();
        bool Set3DCursorToSelectedAtom(bool useLastSelected);
        void EnsureAtomNodeIds();
        bool PickWorldPositionOnGrid(const glm::vec2 &mousePos, glm::vec3 &outWorldPosition) const;
        void Set3DCursorFromScreenPoint(const glm::vec2 &mousePos);
        void DrawPeriodicTableWindow();
        void DrawChangeAtomTypeConfirmDialog();
        void DrawRenderImageDialog(bool &settingsChanged);
        void DrawRenderPreviewWindow(bool &settingsChanged);
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

        bool m_ShowDemoWindow = false;
        bool m_ShowLogPanel = true;
        ThemePreset m_CurrentTheme = ThemePreset::Dark;
        float m_FontScale = 1.0f;
        int m_LogFilter = 0;
        bool m_LogAutoScroll = true;

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
        std::optional<Structure> m_OriginalStructure;
        Structure m_WorkingStructure;
        bool m_HasStructureLoaded = false;

        std::array<char, 512> m_ImportPathBuffer = {};
        std::array<char, 512> m_ExportPathBuffer = {};
        std::array<char, 512> m_RenderImagePathBuffer = {};
        int m_ExportPrecision = 8;
        int m_ExportCoordinateModeIndex = 0;
        int m_RenderImageWidth = 1920;
        int m_RenderImageHeight = 1080;
        RenderImageFormat m_RenderImageFormat = RenderImageFormat::Png;
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
        bool m_PeriodicTableOpen = false;
        PeriodicTableTarget m_PeriodicTableTarget = PeriodicTableTarget::AddAtomEntry;
        std::array<char, 16> m_PendingChangeAtomElementBuffer = {};
        bool m_ChangeAtomTypeConfirmOpen = false;
        bool m_PeriodicTableOpenedFromContextMenu = false;
        bool m_ReopenViewportSelectionContextMenu = false;
        bool m_ShowAddAtomDialog = false;
        bool m_AutoBondGenerationEnabled = true;
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
        std::unordered_map<SceneUUID, glm::vec3> m_AtomColorOverrides;
        std::size_t m_LastLoggedBondCount = std::numeric_limits<std::size_t>::max();
        bool m_LastStructureOperationFailed = false;
        std::string m_LastStructureMessage;
        SpecialNodeSelection m_SelectedSpecialNode = SpecialNodeSelection::None;
        glm::vec3 m_SceneOriginPosition = glm::vec3(0.0f);
        glm::vec3 m_LightPosition = glm::vec3(3.0f, -2.0f, 2.5f);
        std::vector<SceneUUID> m_AtomNodeIds;
        std::vector<int> m_AtomCollectionIndices;
        std::vector<std::size_t> m_SelectedAtomIndices;
        std::optional<std::size_t> m_OutlinerAtomSelectionAnchor;
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
        bool m_CursorSnapToGrid = true;
        bool m_TouchpadNavigationEnabled = true;
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
        int m_ProjectionModeIndex = 0;
        bool m_ViewportSettingsOpen = true;

        std::unique_ptr<IRenderBackend> m_RenderBackend;
        std::unique_ptr<IRenderBackend> m_RenderPreviewBackend;
        std::unique_ptr<OrbitCamera> m_Camera;
    };

} // namespace ds
