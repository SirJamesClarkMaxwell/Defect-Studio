# DefectsStudio - TODO / Progress

## Project workflow rules
- One feature branch per task: `task/NN-short-name`
- Merge to `main` only after build and basic verification pass
- Keep `scripts/Setup.bat` as the canonical local setup entrypoint
- Track only significant tasks/features in this TODO; do not add tiny fixes

## Current priority queue (ordered)
- [ ] P1: Start T08 UI cleanup with Hazel-style panel hierarchy and visual polish
- [ ] P1: Unify ownership of controls across `Tools`, `Settings`, and `Viewport Settings`
- [ ] P1: Rebuild logging window UX with per-level icons, filtering, and clearer status affordances
- [ ] P2: Persist remaining UI and renderer-facing preferences that still reset between runs
- [ ] P3: After T11a/T11b, start deferred advanced render architecture task (MSDF / SVG / multi-viewport / mesh-only follow-up)

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

### [x] T05 - Atom rendering, selection, gizmo (`task/05-editor-selection-gizmo`)
- [x] Instanced atom rendering
- [x] Click/multi-select (**next priority #1**)
- [x] Box select (`B`) and RMB context menu actions (**next priority #3**)
- [x] ImGuizmo transform for selected atoms (**next priority #4**)
- [x] Blender-like XYZ axes (R/G/B) with configurable orientation and colors
- [x] Relative axes mode (based on surrounding atoms)
- [x] Fix Blender-like global axis overlay so axes are always readable and visually consistent in viewport

#### [x] T05a - Scene object system (Blender-like, in-progress under T05)
- [x] Fix crash on deleting Empty and stabilize Empty lifecycle
- [x] Fix Empty re-selection after lifecycle operations (add/delete/clear)
- [x] Add Scene Outliner panel (tree view, Blender-like workflow)
- [x] Add Object Properties panel for selected object
- [x] Introduce scene Collections model and visibility/selectability controls
- [x] Introduce Grouping model (create/select/manage groups)
- [x] Add "Align Empty Z axis to atoms" workflow

#### [x] T05b - Transform UX polish (under T05)
- [x] Iterate UX polish for Empty transform interactions
- [x] Fix keyboard transform flow: `G` then `X/Y/Z` should start constrained move and show active axis
- [x] Improve constrained `G`+axis movement to follow mouse direction more predictably
- [x] Keep gizmo active together with modal `G` workflow (single consistent transform mode)
- [x] Implement atom-defined pivot workflow (create/manage local pivot axes from selected atoms)
- [x] Add Circle Menu for fast mode/action switching (selection, transform, view)
- [x] Add Blender-like `Shift+A` add menu for scene items (Atoms, Empty)
- [x] Add `Delete` hotkey for selected scene items (atoms/empty/group)
- [x] Add atom add popup with configurable insert options before creation
- [x] Add movable special helpers: `Origin` and `Light` (select + transform + properties)
- [x] Add view gizmo reposition workflow (offset + drag mode)
- [x] Add Blender-like `N` side-panel behavior (toggle + collapsed side strip)

### [x] T06 - Bonds and measurements (`task/06-bonds-tools`)
- [x] Add atom tool (insert atom into structure)
- [x] add rotation button on the top of the view window with specification how much the view has to be rotated
- [x] change atom type
- [x] Make POSCAR/CONTCAR export formatting exactly match sample style (e.g., `assets/samples/diamond_bulk`) to avoid noisy `diff`
- [x] Auto bond generation and dynamic threshold updates with generated text as bond length along the bonds and easy read in camera view
- [x] Global cutoff + per-element-pair mode
- [x] hiding atoms bonds instead of deleting it
- [x] Hide selected atoms/bonds/labels with `H` and unhide all with `Alt+H`
- [x] Distance/angle tools and labels
- [x] Collections system (Blender-like outliner groups, visibility/lock/select toggles)
- [x] multi POSCAR import system (via multiple render targets or collections)
- [x] Clean view + cell edge toggle
- [x] add touchpad support in 3D viewport

### [x] T07 - Offscreen render and F12 pipeline (`task/08-offscreen-render`)
- [x] Render dialog and camera frame preview
- [x] Offscreen render to PNG/JPG at chosen resolution
- [x] Queue-driven export path in `OnUpdate`
- [x] Crop rectangle support in export pipeline
- [x] Render look overrides (white background and atom color override)
- [x] Bond label scale consistency across resolutions
- [x] Label rendering style overhaul (shared font/layout and export alignment)
- [x] Separate live preview window behavior
- [x] Atoms/bonds high-resolution quality pass
- [x] Render settings decoupled from main viewport appearance
- [x] Split `EditorLayer.cpp` into multiple `.cpp` files for faster incremental builds

### [ ] T08 - UI panels and UX polishing (`task/08-ui-panels-ux`)
- [x] Dockspace + tool panels
- [x] Extract dedicated `Settings` window to separate UI class (`SettingsPanel`)
- [x] Viewport settings panel: background, grid, lighting, projection mode, atom color override
- [x] File dialogs with fallback path
- [x] Configurable UI spacing scale (saved in editor settings)
- [ ] Hazel-like visual pass guided by `docs/project-control/hazel-ui-style-guide.md`
- [ ] Define and apply a clear panel taxonomy for `Tools`, `Settings`, `Viewport Settings`, and render popups
- [ ] Establish Hazel-like default docking layout: hierarchy left, properties right, viewport center, logs/stats bottom
- [ ] Refine toolbar, panel headers, tab states, and section framing for a consistent editor feel
- [ ] Persist axis settings (colors + orientation) together with renderer settings
- [x] Persist ImGui dock/panel layout across runs
- [ ] Toggles, profiler integration, config defaults
- [ ] Nice logging window with per-level icons (error, warning, info, debug, trace)
- [ ] Improve collapsible hierarchy readability and consistent section indentation across panels
- [ ] Add editor undo/redo stack (`Ctrl+Z` / `Ctrl+Y`) for delete/hide and core scene edit actions
- [ ] Add keyboard shortcut reference panel (overlay or separate window)
- [ ] Save remaining UI settings (colors, checkboxes, atom colors, bond appearance, panel toggles, layout defaults)

### [ ] T09 - Volumetrics MVP (`task/09-volumetrics-mvp`)
- [ ] CHG/CHGCAR/PARCHG parser (FFT ordering)
- [ ] Multi-block support
- [ ] Iso-surface controls incl. dual iso mode

### [ ] T10 Scripts - Migrate build scripts to Python (`task/scripts-python-migration`)
- [ ] Replace `scripts/Setup.bat` with `scripts/setup.py`
- [ ] Replace `scripts/Verify-Build.bat` with `scripts/verify_build.py`
- [ ] Ensure Python scripts produce identical output/exit codes to original bat equivalents
- [ ] Verify scripts work on Windows (native Python) and WSL (same scripts, no changes)
- [ ] Update `COPILOT_GUIDELINES.md` and `SESSION_HANDOFF.md` - replace bat references with py
- [ ] Update `README.md` with new canonical commands

### [ ] T11a - General code refactor (`task/11a-general-refactor`)
- [ ] Define refactor scope and module boundaries (no feature changes)
- [ ] Stronger Core <-> App separation (dependency direction and responsibilities)
- [ ] Modularize large source files into focused modules/components
- [ ] Extract reusable parts into separate libraries (where it reduces coupling)
- [ ] Replace complex inline lambdas with named functions/methods where readability improves
- [ ] Reduce nesting depth by guard clauses and early returns
- [ ] Split large classes into smaller cohesive classes (single-responsibility focus)
- [ ] Standardize naming and file layout for new modules/classes
- [ ] Verify no regressions via Debug/Release build and smoke run
- [ ] Audit all magic numbers - extract to named constexpr constants
- [ ] Replace raw arrays with std::array where size is compile-time known
- [ ] Audit all catch-by-value and catch(...) blocks - replace with typed catches or remove
- [ ] Replace error-code returns in IO layer (parser, exporter) with exceptions or std::expected<T, E>
- [ ] Add noexcept where function provably cannot throw (getters, math utilities, pure transforms)
- [ ] Ensure destructors are noexcept (compiler default, but verify explicitly in RAII wrappers)
- [ ] Wrap all file I/O entry points (POSCAR/CIF/CHGCAR load/save) in top-level try/catch with user-facing error message
- [ ] Do NOT propagate exceptions through OpenGL/render hot path - document this boundary explicitly
- [ ] Add static_assert or comment at renderer boundary: "exception-free zone below this point"

### [ ] T11b - Local code documentation site (mdBook) (`task/11b-local-docs-mdbook`)
- [ ] Start after T11 completion (minimum fallback: after T09)
- [ ] Initialize local mdBook project in `docs/`
- [ ] Define documentation structure (`SUMMARY.md`) for core modules
- [ ] Add architecture pages for Core, DataModel, IO, Renderer, Layers, UI
- [ ] Add developer workflows: build, run, debug, branch/task conventions
- [ ] Add API-oriented pages for key classes and data flow
- [ ] Add local scripts to serve/build docs (`scripts/Docs-Serve.bat`, `scripts/Docs-Build.bat`)
- [ ] Add quick link/instructions in main `README.md`
- [ ] Verify fully offline local usage (`mdbook serve` / `mdbook build`)

### [ ] T11c - Advanced render architecture follow-up (`task/11c-advanced-render-architecture`)
- [ ] Start only after T11a and T11b are in good shape
- [ ] Evaluate msdfgen / 3D label strategy and decide implementation path
- [ ] Investigate a fuller mesh-based atoms/bonds rendering direction (replace remaining line-based paths, scene/ECS implications, `entt?`)
- [ ] Add SVG export
- [ ] Multi-viewport support (different defects in different viewports)

### [ ] T12 - Python ecosystem integration (`task/14-python-integration`)
- [ ] Python integration layer
- [ ] Embedded Python interpreter lifecycle management
- [ ] Script runner with project-context bindings
- [ ] Interoperability with scientific Python libraries
- [ ] ASE integration
- [ ] Import/export structures through ASE
- [ ] Structure manipulation using ASE workflows
- [ ] Optional ASE calculator bridge (phase 2)

### [ ] T13 - Advanced materials science tools (`task/10-materials-tools`)
- [ ] Crystal generator from Bravais lattices
- [ ] Define lattice system presets (sc, bcc, fcc, hcp, diamond, custom)
- [ ] Define and edit basis atoms (element + fractional coordinates)
- [ ] Interactive editing of lattice vectors and lattice constants
- [ ] Supercell generation options at creation time (Nx, Ny, Nz)
- [ ] Quick generation wizard for common structures
- [ ] CIF file support
- [ ] Import CIF structures
- [ ] Convert CIF model to internal structure model
- [ ] Export imported CIF structures to POSCAR/CONTCAR
- [ ] CIF validation report (unsupported symmetry/occupancy fallback)
- [ ] KPOINTS convergance generator
- [ ] EnergyCutoff convergance generator
- [ ] supercell convergence test
- [ ] INCAR editor
- [ ] atoms info panel (when the atom in periodic table is selected the second popup is opened and there some informationa about the atom gonna be displayed, maybe interactive preview of the structure)
- [ ] defining a user set of materials

### [ ] T14 - Local CI equivalents (`task/16-local-ci`)
- [ ] Add `scripts/ci_check.py` - master check script that runs full verification sequence locally
- [ ] ci_check.py step 1: run `verify_build.py` (Debug + Release)
- [ ] ci_check.py step 2: run build on WSL (g++/clang) via subprocess
- [ ] ci_check.py step 3: report summary - pass/fail per platform with exit code
- [ ] Integrate `ci_check.py` into Copilot workflow: run before every merge to main
- [ ] Document usage in README.md

### [ ] T15 - VASP ecosystem integration (`task/11-vasp-integration`)
- [ ] Full OUTCAR parser
- [ ] Parse electronic structure metadata
- [ ] Extract energies per ionic step
- [ ] Extract forces and stress tensor
- [ ] Extract relaxation and convergence information
- [ ] Parse available band structure and DOS metadata
- [ ] WAVECAR reader (MVP)
- [ ] Read KS wavefunction headers and k-point metadata
- [ ] Enable orbital projection data pipeline
- [ ] Prepare wavefunction-derived data for visualization

### [ ] T16 - Volumetric visualization extensions (`task/12-volumetric-extensions`)
- [ ] Isosurface rendering module
- [ ] Visualize charge density fields (CHGCAR/PARCHG)
- [ ] Adjustable isosurface level and opacity controls
- [ ] Multiple simultaneous isosurfaces
- [ ] Polyhedra visualization module
- [ ] Build coordination polyhedra around selected atoms
- [ ] Configurable central atom and neighbor cutoff logic
- [ ] Customizable polyhedra colors and edge styles
- [ ] VESTA-like visualization presets

### [ ] T17 - Defect correction workflows (`task/13-defect-corrections`)
- [ ] Freysoldt correction workflow automation
- [ ] Generate folder structure and correction input templates
- [ ] Prepare correction calculation points automatically
- [ ] Import calculated correction values
- [ ] Plot correction curves in-app
- [ ] Fit second-order correction terms
- [ ] GPU implementation of Freysoldt correction
- [ ] CUDA/OpenGL compute backend prototype
- [ ] Enable large-supercell correction workflow
- [ ]

### [ ] T18 - Other-builds (`task/ob-multiplatform`)
- [ ] Verify Premake5 generates valid Makefiles (premake5 gmake2)
- [ ] Build with g++ on WSL - fix any Linux-specific compilation errors
- [ ] Build with clang on WSL - fix any clang-specific warnings/errors
- [ ] Verify `scripts/ci_check.py` passes on WSL end-to-end

### [ ] T18 - Tests, samples, docs (`task/15-tests-samples-docs`)
- [ ] Parser unit tests + POSCAR round-trip
- [ ] `assets/samples` small input files
- [ ] README (build/run/controls)

## Current focus
- Recently completed: **T07 - Offscreen render and F12 pipeline**
- Next planned task: **T08 - UI panels and UX polishing**
- Planned branch: **task/08-ui-panels-ux**
