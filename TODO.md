# DefectsStudio - TODO / Progress

## Project workflow rules
- One feature branch per task: `task/NN-short-name`
- Merge to `main` only after build and basic verification pass
- Keep `scripts/Setup.bat` as the canonical local setup entrypoint

## Milestones

### [x] T01 - Bootstrap repository and build system (`task/01-bootstrap`)
- [x] Premake5 workspace for Visual Studio 2022
- [x] C++23 setup, Debug/Release configs
- [x] Vendor layout and base include/lib wiring
- [x] `scripts/Setup.bat` generating `.sln`
- [x] MVP app starts with DockSpace + viewport scaffolding (code-level bootstrap)
- [ ] Full CLI compile verification pending environment with VS2022 C++ Build Tools (`msbuild` not available in current PATH)

### [x] T02 - Core app skeleton and layer system (`task/02-core-layering`)
- [x] App lifecycle, window, events
- [x] LayerStack, CoreLayer, ImGuiLayer, EditorLayer
- [x] Logging + error panel basics
- [x] VS Code workspace integration (`.vscode` tasks/launch/settings)
- [x] UI style presets + persistent user UI settings file

### [x] T03 - OpenGL renderer MVP (`task/03-renderer-opengl`)
- [x] GL context + renderer backend abstraction
- [x] Shader system with per-file shaders in `assets/shaders`
- [x] 3D viewport and camera controls (Blender-like baseline)

### [x] T04 - Data model + POSCAR/CONTCAR import/export (`task/04-vasp-io-structure`)
- [x] Structure/Atom/Bond data model (Structure/Atom skeleton started)
- [x] POSCAR/CONTCAR parser (VASP5/6 symbols line, selective dynamics) (POSCAR parser skeleton started)
- [x] Export with precision and Direct/Cartesian options
- [x] Original-state restore support

### [ ] T05 - Atom rendering, selection, gizmo (`task/05-editor-selection-gizmo`)
- [ ] Instanced atom rendering
- [ ] Click/multi-select
- [ ] Box select (`B`) and RMB context menu actions
- [ ] ImGuizmo transform for selected atoms

### [ ] T06 - Bonds and measurements (`task/06-bonds-tools`)
- [ ] Auto bond generation and dynamic threshold updates
- [ ] Global cutoff + per-element-pair mode
- [ ] Distance/angle tools and labels
- [ ] Clean view + cell edge toggle

### [ ] T07 - Volumetrics MVP (`task/07-volumetrics-mvp`)
- [ ] CHG/CHGCAR/PARCHG parser (FFT ordering)
- [ ] Multi-block support
- [ ] Iso-surface controls incl. dual iso mode

### [ ] T08 - UI panels and UX polishing (`task/08-ui-panels-ux`)
- [ ] Dockspace + tool panels
- [ ] File dialogs with fallback path
- [ ] Toggles, profiler integration, config defaults

### [ ] T09 - Offscreen render and F12 pipeline (`task/09-offscreen-render`)
- [ ] Render dialog and camera frame preview
- [ ] Offscreen render to PNG/JPG at chosen resolution

### [ ] T10 - Tests, samples, docs (`task/10-tests-docs`)
- [ ] Parser unit tests + POSCAR round-trip
- [ ] `assets/samples` small input files
- [ ] README (build/run/controls)

## Current focus
- Active task: **T05 - Atom rendering, selection, gizmo**
- Active branch: **task/05-editor-selection-gizmo**
