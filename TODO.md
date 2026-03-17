# DefectsStudio - TODO / Progress

## Project workflow rules
- One feature branch per task: `task/NN-short-name`
- Merge to `main` only after build and basic verification pass
- Keep `scripts/Setup.bat` as the canonical local setup entrypoint
- Track only significant tasks/features in this TODO; do not add tiny fixes

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
- [x] Instanced atom rendering
- [x] Click/multi-select (**next priority #1**)
- [x] Box select (`B`) and RMB context menu actions (**next priority #3**)
- [x] ImGuizmo transform for selected atoms (**next priority #4**)
- [x] Blender-like XYZ axes (R/G/B) with configurable orientation and colors
- [x] Relative axes mode (based on surrounding atoms)
- [x] Fix Blender-like global axis overlay so axes are always readable and visually consistent in viewport

#### [ ] T05a - Scene object system (Blender-like, in-progress under T05)
- [x] Fix crash on deleting Empty and stabilize Empty lifecycle
- [x] Fix Empty re-selection after lifecycle operations (add/delete/clear)
- [x] Add Scene Outliner panel (tree view, Blender-like workflow)
- [x] Add Object Properties panel for selected object
- [x] Introduce scene Collections model and visibility/selectability controls
- [x] Introduce Grouping model (create/select/manage groups)
- [x] Add "Align Empty Z axis to atoms" workflow

#### [ ] T05b - Transform UX polish (under T05)
- [ ] Iterate UX polish for Empty transform interactions
- [x] Fix keyboard transform flow: `G` then `X/Y/Z` should start constrained move and show active axis
- [x] Implement atom-defined pivot workflow (create/manage local pivot axes from selected atoms)
- [x] Add Circle Menu for fast mode/action switching (selection, transform, view)
- [x] Add Blender-like `Shift+A` add menu for scene items (Atoms, Empty)
- [x] Add `Delete` hotkey for selected scene items (atoms/empty/group)
- [x] Add atom add popup with configurable insert options before creation


### [ ] T06 - Bonds and measurements (`task/06-bonds-tools`)
- [x] Add atom tool (insert atom into structure) 
- [ ] change atom type 
- [ ] Auto bond generation and dynamic threshold updates
- [ ] Global cutoff + per-element-pair mode
- [ ] Distance/angle tools and labels
- [ ] Collections system (Blender-like outliner groups, visibility/lock/select toggles)
- [ ] Clean view + cell edge toggle

### [ ] T07 - UI panels and UX polishing (`task/08-ui-panels-ux`)
- [x] Dockspace + tool panels
- [x] Viewport settings panel: background, grid, lighting, projection mode, atom color override
- [x] File dialogs with fallback path
- [ ] Persist axis settings (colors + orientation) together with renderer settings
- [x] Persist ImGui dock/panel layout across runs
- [ ] Toggles, profiler integration, config defaults
- [ ] nice logging window with icons (error, warning, info, debug, trace) ❌,⚠️(dodaj inne emotki)

### [ ] T08 - Offscreen render and F12 pipeline (`task/09-offscreen-render`)
- [ ] Render dialog and camera frame preview
- [ ] Offscreen render to PNG/JPG at chosen resolution

### [ ] T09 - Volumetrics MVP (`task/07-volumetrics-mvp`)
- [ ] CHG/CHGCAR/PARCHG parser (FFT ordering)
- [ ] Multi-block support
- [ ] Iso-surface controls incl. dual iso mode


### [ ] T10 - Tests, samples, docs (`task/10-tests-docs`)
- [ ] Parser unit tests + POSCAR round-trip
- [ ] `assets/samples` small input files
- [ ] README (build/run/controls)

## Current focus
- Active task: **T05 / T05a - Atom rendering, selection, gizmo + scene object system**
- Active branch: **task/05-editor-selection-gizmo**
