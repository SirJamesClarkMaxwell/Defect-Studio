# DefectsStudio - TODO / Progress

## Project workflow rules
- One feature branch per task: `task/NN-short-name`
- Merge to `main` only after build and basic verification pass
- Keep `scripts/Setup.bat` as the canonical local setup entrypoint
- Track only significant tasks/features in this TODO; do not add tiny fixes

## Current priority queue (ordered)
- [ ] P1: Start T11 volumetrics MVP (`CHGCAR` / `PARCHG`, iso controls, block handling)
- [ ] P1: Run a focused manual smoke test for T10 project workflow, collection editing, and drag-drop
- [ ] P2: Start T12 Python build-script migration after T11 is scoped
- [ ] P3: After T13a/T13b, start deferred advanced render architecture task (MSDF / SVG / multi-viewport / mesh-only follow-up)

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

### [x] T07 - Offscreen render and F12 pipeline (`task/07-offscreen-render`)
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

### [x] T08 - Config architecture and element catalog foundations (retroactively documented)
- [x] Migrate default config, UI settings, and atom settings to YAML-backed files
- [x] Set `assets/samples/reduced_diamond_bulk.vasp` as default startup sample through config
- [x] Add legacy config migration path from old INI files to YAML
- [x] Introduce global per-element atom defaults with full periodic-table coverage
- [x] Add `Element Catalog` editor flow for per-element color/size defaults
- [x] Expose the Periodic Table in `Window` and reuse it as both a dockable panel and an inline picker widget
- [x] Separate global atom data ownership from per-project appearance override ownership

### [x] T09 - UI panels and UX polishing (`task/09-ui-panels-ux`)
- [x] Dockspace + core editor panels
- [x] Extract dedicated `Settings` window to separate UI class (`SettingsPanel`)
- [x] Viewport settings panel: background, grid, lighting, projection mode, atom color override
- [x] File dialogs with fallback path
- [x] Configurable UI spacing scale (saved in editor settings)
- [x] Hazel-like visual pass guided by `docs/project-control/hazel-ui-style-guide.md`
- [x] Define and apply a clear panel taxonomy for `Appearance`, `Settings`, `Viewport Settings`, and `Render Image`
- [x] Separate scene appearance controls (atoms, bonds, labels, colors) from action-oriented panels
- [x] Refine toolbar, panel headers, tab states, and section framing for a consistent editor feel
- [x] Persist axis settings (colors + orientation) together with renderer settings
- [x] Persist ImGui dock/panel layout across runs
- [x] Toggles, profiler integration, config defaults
- [x] Nice logging window with per-level icons and source metadata (file / line / function)
- [x] Improve collapsible hierarchy readability and consistent section indentation across panels
- [x] Add editor undo/redo stack (`Ctrl+Z` / `Ctrl+Y`) for delete/hide and core scene edit actions
- [x] Add keyboard shortcut reference panel (overlay or separate window)
- [x] Save remaining UI settings (colors, checkboxes, atom colors, bond appearance, panel toggles, layout defaults)
- [x] add (?) help affordances near advanced controls to explain how to use things
- [x] keep scene hierarchy rows text-first (remove icons)
- [x] rename selected collection via `F2` in Scene Outliner
- [x] add saving UI theme defined by ImGuiDemo window
- [x] add possibility of changing shortcuts or mouse behavior
- [x] add possibility of changing viewport resolution to reduce GPU load on weaker/slower hardware

### [x] T10 - Editing workflow polish (`task/10-editing-workflow-polish`)
- [x] add possibility of regenerating bonds after atom movement/addition/removal
- [x] add deselect (shift+c) in circle selection mode
- [x] add undo-redo for atom movement
- [x] add copy-paste, duplicate for atoms, collections, and multi-atom type changes
- [x] add more actions into context menu in viewport
- [x] stabilize `Periodic Table` / `Element Catalog` selection UX for add-atom workflow and elements outside the loaded structure
- [x] extract per-project element appearance overrides from `scene_state.ini` into a dedicated project config file
- [x] add import/export/reset workflow for project element appearance overrides
- [x] add context menu for scene outline (rename, duplicate, delete)

- [x] add extract selected to new collection
- [x] add toggle for adding auto recalculation of the bonds
- [x] in circle select scroll should (in/de)crease size of circle (block the world zooming)
- [x] add behaviour that after pressing the . in view the camera would go to the 3D cursor with some distance 
- [x] ctrl+d for duplicating things
- [x] add gizmo snap with movement when ctrl is pressed
- [x] add movement into selected axes and typing number
- [x] make proper settings, in settings window should be two columns (label - setting) and those should be alligned properly
- [x] make UI more aligned
- [x] important bug, podczas eksportu rzeczy typu `project_apperance` tworzy się nested sequence config->project->config->project
- [x] dodaj zewnętrzny folder projektu
- [x] możliwość open, create, recent open, project
- [x] add export collection to poscar
- [x] add ctrl+a to select everything
- [x] when I want to create/open the project the path to something should be remebered
- [x] stabilize Scene Outliner context menu rendering / IDs
- [x] keep Scene Outliner trees expanded when actions are triggered from context menus (dalej nie działa)
- [x] add align empty to camera view
- [x] add camera clip diagnostics / tuning in `Settings`
- [x] improve scene-aware camera clipping while orbiting medium/far from the structure
- [x] make `.` focus adjustable from `Settings` (distance factor, minimum distance, selection padding)
- [x] multiple selection in collections
- [X] resolve how the poscar are saved if the whole cell was moved (maybe some internal coordinate system)
- [X] range selection in collections when `Shift` is pressed
- [x] serialization of all settings
- [x] serialize editor and viewport settings into the project as project-local overrides
- [x] drag-drop atoms between collections in Scene Outliner
- [x] `Ctrl+D` and `Delete` for active collection while Scene Outliner is focused
- [x] keyboard shortcuts for `Create Project`, `Open Project`, and `Open Recent Project`

### [ ] T11 - Volumetrics MVP (`task/11-volumetrics-mvp`)
- [ ] CHG/CHGCAR/PARCHG parser 
- [ ] Multi-block support (what does it mean?)
- [ ] Iso-surface controls incl. dual iso mode (what does it mean)

### [ ] T12 - Migrate build scripts to Python (`task/12-scripts-python-migration`)
- [ ] Add uv as dependency to project
- [ ] make python project and during the installation create a .venv, install all dependencies
- [ ] Replace `scripts/Setup.bat` with `scripts/setup.py`
- [ ] Replace `scripts/Verify-Build.bat` with `scripts/verify_build.py`
- [ ] Ensure Python scripts produce identical output/exit codes to original bat equivalents
- [ ] Verify scripts work on Windows (native Python) and WSL (same scripts, no changes)
- [ ] Update `COPILOT_GUIDELINES.md` and `SESSION_HANDOFF.md` - replace bat references with py
- [ ] Update `README.md` with new canonical commands

### [ ] T13a - General code refactor (`task/13a-general-refactor`)
- [ ] Define refactor scope and module boundaries (no feature changes)
- [ ] Stronger Core <-> App separation (dependency direction and responsibilities)
- [ ] Modularize large source files into focused modules/components
- [ ] Extract reusable parts into separate libraries (where it reduces coupling)
- [ ] Replace complex inline lambdas with named functions/methods where readability improves
- [ ] Reduce nesting depth by guard clauses and early returns
- [ ] Split large classes into smaller cohesive classes (single-responsibility focus)
- [ ] Standardize naming and file layout for new modules/classes
- [ ] Verify no regressions via Debug/Release build and smoke run
- [ ] Formalize config ownership boundaries (`default.yaml`, `ui_settings.yaml`, global atom catalog, per-project overrides) and remove leftover duplication
- [ ] Audit all magic numbers - extract to named constexpr constants
- [ ] Replace raw arrays with std::array where size is compile-time known
- [ ] Audit all catch-by-value and catch(...) blocks - replace with typed catches or remove
- [ ] Replace error-code returns in IO layer (parser, exporter) with exceptions or std::expected<T, E>
- [ ] Add noexcept where function provably cannot throw (getters, math utilities, pure transforms)
- [ ] Ensure destructors are noexcept (compiler default, but verify explicitly in RAII wrappers)
- [ ] Wrap all file I/O entry points (POSCAR/CIF/CHGCAR load/save) in top-level try/catch with user-facing error message
- [ ] Do NOT propagate exceptions through OpenGL/render hot path - document this boundary explicitly
- [ ] Add static_assert or comment at renderer boundary: "exception-free zone below this point"

### [ ] T13b - Local code documentation site (mdBook) (`task/13b-local-docs-mdbook`)
- [ ] Start after T13a completion (minimum fallback: after T11)
- [ ] Initialize local mdBook project in `docs/`
- [ ] Define documentation structure (`SUMMARY.md`) for core modules
- [ ] Add architecture pages for Core, DataModel, IO, Renderer, Layers, UI
- [ ] Add developer workflows: build, run, debug, branch/task conventions
- [ ] Add API-oriented pages for key classes and data flow
- [ ] Add local scripts to serve/build docs (`scripts/Docs-Serve.bat`, `scripts/Docs-Build.bat`)
- [ ] Add quick link/instructions in main `README.md`
- [ ] Verify fully offline local usage (`mdbook serve` / `mdbook build`)

### [ ] T13c - Advanced render architecture follow-up (`task/13c-advanced-render-architecture`)
- [ ] Start only after T11a and T11b are in good shape
- [ ] Evaluate msdfgen / 3D label strategy and decide implementation path
- [ ] Investigate a fuller mesh-based atoms/bonds rendering direction (replace remaining line-based paths, scene/ECS implications, `entt?`)
- [ ] Add SVG export
- [ ] Multi-viewport support (different defects in different viewports)


### [ ] T14 - Python ecosystem integration (`task/14-python-integration`)
- [ ] Python integration layer
- [ ] Embedded Python interpreter lifecycle management
- [ ] Script runner with project-context bindings
- [ ] Interoperability with scientific Python libraries
- [ ] ASE integration
- [ ] Import/export structures through ASE
- [ ] Structure manipulation using ASE workflows
- [ ] Optional ASE calculator bridge (phase 2)

### [ ] T15 - Project concept
- [ ] Tracking added files
- [ ] auto-save
- [ ] conception of the defect
- [ ] different chanrge states (adding filters (charge state, spin channel))
- [ ] identifying defect symetry
- [ ] adding tags
- [ ] change from collection to poscar amd contcar


### [ ] T16 - Advanced materials science tools (`task/15-materials-tools`)
- [ ] Crystal generator from Bravais lattices
- [ ] Define lattice system presets (sc, bcc, fcc, hcp, diamond, custom)
- [ ] Define and edit basis atoms (element + fractional coordinates)
- [ ] Interactive editing of lattice vectors and lattice constants
- [ ] Supercell generation options at creation time (Nx, Ny, Nz)
- [ ] Quick generation wizard for common structures
- [ ] KPOINTS convergence generator
- [ ] EnergyCutoff convergence generator
- [ ] supercell convergence test
- [ ] INCAR editor
- [ ] atoms info panel (when the atom in periodic table is selected the second popup is opened and there some informationa about the atom gonna be displayed, maybe interactive preview of the structure)
- [ ] defining a user set of materials
- [ ] CIF file support
- [ ] Import CIF structures
- [ ] Convert CIF model to internal structure model
- [ ] Export imported CIF structures to POSCAR/CONTCAR
- [ ] CIF validation report (unsupported symmetry/occupancy fallback)

### [ ] T16 - Local CI equivalents (`task/16-local-ci`)
- [ ] Add `scripts/ci_check.py` - master check script that runs full verification sequence locally
- [ ] ci_check.py step 1: run `verify_build.py` (Debug + Release)
- [ ] ci_check.py step 2: run build on WSL (g++/clang) via subprocess
- [ ] ci_check.py step 3: report summary - pass/fail per platform with exit code
- [ ] Integrate `ci_check.py` into Copilot workflow: run before every merge to main
- [ ] Document usage in README.md

### [ ] T17 - VASP ecosystem integration (`task/17-vasp-integration`)
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

### [ ] T18 - Volumetric visualization extensions (`task/18-volumetric-extensions`)
- [ ] Isosurface rendering module
- [ ] Visualize charge density fields (CHGCAR/PARCHG)
- [ ] Adjustable isosurface level and opacity controls
- [ ] Multiple simultaneous isosurfaces
- [ ] Polyhedra visualization module
- [ ] Build coordination polyhedra around selected atoms
- [ ] Configurable central atom and neighbor cutoff logic
- [ ] Customizable polyhedra colors and edge styles
- [ ] VESTA-like visualization presets

### [ ] T19 - Defect correction workflows (`task/19-defect-corrections`)
- [ ] Freysoldt correction workflow automation
- [ ] Generate folder structure and correction input templates
- [ ] Prepare correction calculation points automatically
- [ ] Import calculated correction values
- [ ] Plot correction curves in-app
- [ ] Fit second-order correction terms
- [ ] GPU implementation of Freysoldt correction
- [ ] CUDA/OpenGL compute backend prototype
- [ ] Enable large-supercell correction workflow
### T20 - remote axes to list of servers
- [ ] winscp like
- [ ] autorefresh server
- [ ] copy-paste files
### [ ] T20 - Other-builds (`task/20-multiplatform`)
- [ ] Verify Premake5 generates valid Makefiles (premake5 gmake2)
- [ ] Build with g++ on WSL - fix any Linux-specific compilation errors
- [ ] Build with clang on WSL - fix any clang-specific warnings/errors
- [ ] Verify `scripts/ci_check.py` passes on WSL end-to-end

### [ ] T21 - Tests, samples, docs (`task/21-tests-samples-docs`)
- [ ] Parser unit tests + POSCAR round-trip
- [ ] `assets/samples` small input files
- [ ] README (build/run/controls)
- [ ] Config migration regression tests (legacy INI -> YAML, malformed YAML fallback)
- [ ] Document global atom catalog vs per-project appearance override workflow

## Current focus
- Recently completed: **T08/T09 config + UI pass (YAML configs, Element Catalog, dockable Periodic Table, panel taxonomy, logging UX, persistence, undo/redo)**
- Next planned task: **T10 - Editing workflow polish**
- Planned branch: **task/10-editing-workflow-polish**
