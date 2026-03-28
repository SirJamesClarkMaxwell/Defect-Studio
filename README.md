# DefectsStudio

**DefectsStudio** is a desktop application for **visualizing, editing, and preparing atomic structures and defects for VASP-style workflows**.

The project aims to stay **fast, practical, and scientist-friendly**:

- C++23
- OpenGL renderer
- Dear ImGui with docking
- editor-style workflow focused on structures, defects, and simulation prep

---

# Overview

Working with atomistic structures for DFT workflows often means repetitive editing, defect creation, selection-heavy transforms, and endless export / re-export loops.

DefectsStudio is meant to be a lightweight alternative to heavier tools, with emphasis on:

- direct structure editing
- viewport-first workflow
- fast POSCAR-style iteration
- defect engineering use cases
- local desktop responsiveness instead of web-style UI overhead

---

# Current Capabilities

The app currently includes:

- 3D visualization of atomic structures
- POSCAR / CONTCAR import and export
- multi-file structure loading through collections
- instanced atom rendering
- auto and manual bonds
- bond length labels and angle labels
- atom / bond / helper selection and transform workflows
- scene collections, groups, and outliner-style management
- dockable editor panels
- dockable Periodic Table and `Element Catalog`
- per-element atom defaults with YAML-backed config
- YAML-backed app / UI config migration from legacy INI files
- undo / redo for core scene edit actions
- shortcut reference panel and customizable input behavior
- persistent ImGui layout and core UI settings
- offscreen render export to PNG / JPG
- dockable live render preview window
- crop-based render export workflow
- render look overrides and label styling for export
- Tracy profiling hooks (CPU + GPU zones)

---

# Planned Directions

Near-term and mid-term roadmap items are tracked in:

- `docs/project-control/TODO.md`
- `docs/project-control/SESSION_HANDOFF.md`
- `docs/project-control/hazel-ui-style-guide.md`
- `docs/project-control/TASK_HELPER.md`

Planned future areas include:

### UI / UX
- editing workflow polish (selection completion, transform undo/redo, copy / duplicate / multi-edit actions)
- project-level appearance override workflow cleanup
- continued panel / workflow refinement after the large T09 pass

### Rendering
- advanced render architecture follow-up after refactor and docs
- richer label rendering strategy
- SVG export
- multi-viewport workflows

### Volumetrics
- CHGCAR / PARCHG support
- isosurface controls and visualization

### Materials-science tooling
- CIF support
- convergence helpers
- structure generators
- VASP ecosystem integration

---

# Building The Project

## Requirements

- Windows 10 / 11
- Visual Studio 2022
- C++ toolset v143
- PowerShell

## Setup

1. Clone the repo with submodules:
   `git clone --recurse-submodules https://github.com/SirJamesClarkMaxwell/Defect-Studio.git`
2. If needed, sync submodules manually:
   `git submodule update --init --recursive`
3. Run:
   `./scripts/Setup.bat`
4. Open:
   `DefectsStudio.sln`
5. Build:
   `Debug` or `Release`

Set **DefectsStudio** as the startup project and run with `F5`.

---

# Build Utilities

Included helper scripts:

- `scripts/Setup.bat` - generate / refresh the Visual Studio solution
- `scripts/Verify-Build.bat` - verify Debug and Release builds
- `scripts/Verify-Build-And-Run.bat` - build and then launch the app
- `scripts/Run.bat` - run an existing Debug / Release build

External dependencies are managed through git submodules, including:

- `vendor/glfw`
- `vendor/glad`
- `vendor/imgui`
- `vendor/glm`
- `vendor/imguizmo`
- `vendor/imviewguizmo`
- `vendor/tracy`

If project files or resource wiring change, rerun `scripts/Setup.bat`.

---

# Rendering Notes

Current render/export workflow highlights:

- `F12` opens the render/export workflow
- export supports `PNG` and `JPG`
- render preview is live and dockable
- preview/export rendering is independent from the main viewport render target
- export supports crop rectangles and label composition

---

# Configuration Model

The project currently uses a split config model:

- `config/default.yaml` - app-level defaults such as the startup sample
- `config/atom_settings.yaml` - global per-element defaults and future atom catalog data
- `config/ui_settings.yaml` - persisted editor and viewport UI state

Current default startup sample:

- `assets/samples/reduced_diamond_bulk.vasp`

Project-specific appearance overrides still exist, but are planned to be separated more cleanly from scene state in a follow-up task.

---

# Profiling (Tracy)

The app includes Tracy instrumentation for CPU and OpenGL GPU zones.

## Local usage

1. Build and run DefectsStudio.
2. Build and run Tracy GUI from `vendor/tracy/profiler`.
3. Connect Tracy GUI to the running app (`localhost`, default port).
4. Inspect update / render / frame timing and GPU zones.

## Quick launch

- `scripts/Run-Tracy.bat`
- `scripts/Run-Tracy.ps1`

On first run, Tracy profiler may be built to a short Windows path:

- `D:\t\tracy-build`

---

# Project Control Docs

For active planning and handoff, use:

- `docs/project-control/TODO.md`
- `docs/project-control/SESSION_HANDOFF.md`

Local workflow docs may also be present in `docs/project-control/`, for example `COPILOT_GUIDELINES.md`, `hazel-ui-style-guide.md`, and the per-task `TASK_HELPER.md` scratchpad.

---

# Project Vision

The long-term goal of **DefectsStudio** is to become a specialized environment for defect engineering and atomistic modeling that combines:

- the usability of modern editor-style tools
- lightweight native performance
- workflows tuned for materials science and VASP preparation
