# Session Handoff

## Current state
- Integration target: `main`
- Recently completed task: `T12 - Windows Python tooling migration`
- Recommended next branch: `task/13-bug-fixes`
- Build status in this session:
  - Full Windows setup wrapper now passes end-to-end: uv, `.venv`, submodules, premake, MSVC detection, build, and app launch
  - Windows Python tooling workflow passes in `Debug|x64` and `Release|x64`
  - Remaining warning is the known `stb_image_write.h` `C4996`

## What changed in the finished T12 pass
- Tooling workflow:
  - added `pyproject.toml` + `uv.lock` for Python tooling dependencies
  - added `scripts/setup.py` and `scripts/verify_build.py` as the underlying setup/build entrypoints
  - added `scripts/Tooling.bat` / `scripts/Tooling.ps1` as the single Windows bootstrapper for Python tooling
  - bootstrapper now installs `uv` when needed and installs Python 3.11+ as a uv-managed interpreter before dispatching Python scripts
  - `scripts/Run.bat` / `scripts/Run.ps1` remain the separate application launcher
- Compatibility and docs:
  - old `Setup.*` / `Verify-Build.*` wrappers now delegate to the new tooling bootstrapper
  - VS Code tasks now use the Windows tooling wrapper
  - `README.md`, `TODO.md`, and `COPILOT_GUIDELINES.md` now point to the Windows tooling workflow

## Important context carried over from the finished T11 pass
- Volumetric dataset foundation:
  - scalar-field data model decoupled from atom / bond scene data
  - parser support for `CHG`, `CHGCAR`, and `PARCHG`-style headers plus scalar grids
  - multi-block dataset support with per-block metadata and statistics
- Volumetric project workflow:
  - project manifest integration for volumetric datasets
  - volumetric datasets now load only from project manifests or manual import, not from a startup hardcoded profiling sample
  - shared file-dialog starting directory between structure import and volumetric import
  - optional application of crystal structure from volumetric file headers
- Volumetric performance pass:
  - lazy loading of block samples
  - background dataset, block, and surface-preview jobs via `thread-pool`
  - faster ASCII parsing path for block sample loading
  - retry/error-state cleanup for failed block loads
  - Tracy CPU timing zones and memory instrumentation for volumetric work
  - surface-mesh GPU upload path no longer thrashes every frame
- Volumetric rendering/UI:
  - single-surface and dual-surface preview
  - `Surface A` / `Surface B` side-by-side controls
  - project-persisted surface state: visibility, iso, color, opacity, material
  - VESTA-like field interpretation for `PARCHG`:
    - `Selected block`
    - `Total density`
    - `Magnetization`
    - derived `Spin up`
    - derived `Spin down`
  - VESTA-like positive / negative surface modes
  - simpler `Volumetrics` panel with less diagnostic noise in the main working area
- Scene/structure validation:
  - stronger comparison between dataset header structure and current scene
  - checks now cover more than atom count: lattice/species/order/positions
- View/toolbar improvements:
  - VESTA-inspired `Viewport Controls`
  - camera-relative orbit/pan/zoom step controls
  - icon-button based toolbar for arrow/roll/zoom controls
  - wider numeric entry fields
  - proportional dock layout scaling when resizing the main application window

## Current config / data model notes
- Global app-level config:
  - `config/default.yaml`
  - `config/atom_settings.yaml`
  - app-level `config/ui_settings.yaml`
- Project-local state:
  - `PROJECT_ROOT/project.yaml`
  - `PROJECT_ROOT/config/ui_settings.yaml`
  - `PROJECT_ROOT/config/project/project_appearance.yaml`
  - `PROJECT_ROOT/config/scene_state.ini`
- Volumetric semantics currently used for `PARCHG`:
  - block 1 = total density (`up + down`)
  - block 2 = magnetization (`up - down`)
  - derived channels:
    - `spin_up = 0.5 * (block1 + block2)`
    - `spin_down = 0.5 * (block1 - block2)`

## Open follow-ups after T11
- Manual validation against VESTA still matters most for visual parity:
  - verify interpretation and default iso settings on the real `PARCHG.0778-0782.ALLK` set
  - tighten shading/material defaults so surfaces look closer to the VESTA reference
  - decide whether more VESTA-like controls should stay in T11 follow-up or move to `T18`
- Toolbar polish:
  - user-provided directional icons are wired in
  - remaining exact VESTA icon mapping can still be refined if needed

## Recommended next task: T13
Goal: return to product bugs/features after finishing the Windows-only Python tooling migration.

Recommended first steps:
1. read `TODO.md` and prioritize the newly added bug-fix items
2. keep `T12a` explicitly deferred unless Linux/WSL parity becomes urgent
3. preserve the Windows tooling entrypoints:
   - `scripts/Tooling.bat setup`
   - `scripts/Tooling.bat verify-build`
4. use `scripts/Run.bat` only for launching an already built app
5. keep docs aligned with the Windows-only tooling scope

## Relevant files touched in late T11
- `src/DataModel/VolumetricDataset.cpp`
- `src/DataModel/VolumetricDataset.h`
- `src/IO/VaspVolumetricParser.cpp`
- `src/Layers/EditorLayer.cpp`
- `src/Layers/EditorLayer.ImGui.cpp`
- `src/Layers/EditorLayer.Persistence.cpp`
- `src/Layers/EditorLayer.Scene.cpp`
- `src/Layers/EditorLayer.Update.cpp`
- `src/Layers/EditorLayer.h`
- `src/Renderer/IRenderBackend.h`
- `src/Renderer/OpenGLRendererBackend.cpp`
- `src/Renderer/OpenGLRendererBackend.h`
- `src/Renderer/OrbitCamera.cpp`
- `src/Renderer/OrbitCamera.h`
- `assets/shaders/surface_mesh.vert`
- `assets/shaders/surface_mesh.frag`
- `docs/project-control/TODO.md`

## Ready-to-paste starter prompt for next chat
I am continuing after the completed T12 Windows Python tooling migration pass.
Please:
1. read `docs/project-control/TODO.md` and `docs/project-control/SESSION_HANDOFF.md`,
2. confirm the current branch and git status,
3. continue from the Windows-only tooling baseline,
4. leave Linux/WSL follow-up in `T12a` unless explicitly requested,
5. start the next prioritized product task from `TODO.md`,
6. summarize what was finished and what still needs validation.
