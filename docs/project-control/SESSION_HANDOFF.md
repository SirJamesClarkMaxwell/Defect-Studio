# Session Handoff

## Current state
- Integration target: `main`
- Recently completed task: `T10 - Editing workflow polish`
- Recommended next branch: `task/11-volumetrics-mvp`
- Build status in this session:
  - `scripts/Verify-Build.bat` passes in `Debug|x64` and `Release|x64`
  - Remaining warning is the known `stb_image_write.h` `C4996`

## What changed in the finished T10 pass
- Editing workflow polish:
  - circle-select subtract on `Shift`
  - undo/redo coverage for core transform workflows
  - internal copy / paste / duplicate for atoms, empties, and collections
  - richer viewport and outliner context menus
  - collection duplicate/delete shortcuts from `Scene Outliner`
- Scene Outliner improvements:
  - multi-select collections
  - range selection with `Shift`
  - `Select all` on collection atom trees
  - drag-drop atoms between collections
  - more stable context menus and item IDs
- Camera and transform UX:
  - focus workflow around current selection via `.`
  - focus tuning in `Settings`
  - scene-aware clip diagnostics
  - align empty axes to camera view
  - typed modal translate and temporary local axes workflow
- Project workflow:
  - explicit project root concept
  - `Create Project`, `Open Project`, and `Open Recent Project`
  - recent-project popup and keyboard shortcuts
  - safer project-root normalization to avoid nested `config/project/config/project`
- Config ownership cleanup:
  - per-project element appearance overrides moved out of `scene_state.ini`
  - dedicated `config/project/project_appearance.yaml`
  - import / export / reset flow for project appearance overrides
  - project-local editor and viewport settings now serialize to `PROJECT_ROOT/config/ui_settings.yaml`
  - loading order is now effectively `global app UI defaults -> project UI overrides`
- POSCAR export polish:
  - more stable `Direct` export when the whole cell or structure was translated
  - collection export to POSCAR

## Current config model
- Global app defaults:
  - `config/default.yaml`
  - `config/atom_settings.yaml`
  - app-level `config/ui_settings.yaml`
- Project-local state:
  - `PROJECT_ROOT/project.yaml`
  - `PROJECT_ROOT/config/ui_settings.yaml`
  - `PROJECT_ROOT/config/project/project_appearance.yaml`
  - `PROJECT_ROOT/config/scene_state.ini`

## Recommended next task: T11
Goal: start volumetric data support without blocking the later refactor and documentation pass.

Recommended first steps:
1. Define supported input scope for `CHG`, `CHGCAR`, and `PARCHG`.
2. Decide what "multi-block support" means in the UI and data model.
3. Add a minimal scalar-field data container independent from the atom renderer.
4. Implement parser loading and basic diagnostics first, before isosurface rendering.
5. Add a tiny debug UI for block selection and scalar range preview.

## Relevant files touched in late T10
- `src/Layers/EditorLayer.cpp`
- `src/Layers/EditorLayer.ImGui.cpp`
- `src/Layers/EditorLayer.Persistence.cpp`
- `src/Layers/EditorLayer.Scene.cpp`
- `src/Layers/EditorLayer.Selection.cpp`
- `src/Layers/EditorLayer.Update.cpp`
- `src/Layers/EditorLayer.Render.cpp`
- `src/Layers/EditorLayer.h`
- `src/Layers/EditorLayerPrivate.h`
- `src/UI/SettingsPanel.cpp`
- `src/UI/PropertiesPanel.cpp`
- `src/IO/PoscarSerializer.cpp`
- `src/IO/PoscarSerializer.h`
- `docs/project-control/TODO.md`
- `README.md`

## Ready-to-paste starter prompt for next chat
I am continuing after the completed T10 editing workflow pass.
Please:
1. read `docs/project-control/TODO.md`, `docs/project-control/SESSION_HANDOFF.md`, and `README.md`,
2. confirm the current branch and git status,
3. start `T11` on `task/11-volumetrics-mvp`,
4. inspect existing VASP I/O code and propose the minimal scalar-field data model for `CHGCAR` / `PARCHG`,
5. implement parser-first volumetric MVP before any advanced rendering,
6. summarize what was finished and what design decisions still need owner input.
