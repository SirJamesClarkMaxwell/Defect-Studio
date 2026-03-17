# DefectsStudio

**DefectsStudio** is a lightweight desktop application for **visualizing, editing, and preparing atomic structures and defects for VASP simulations**.

The goal of the project is to provide a **fast, minimal, and scientist-friendly environment** for working with crystal structures.

The application is written in **C++23**, uses **OpenGL** for rendering and **Dear ImGui (Docking)** for the user interface.

---

# Overview

Working with atomistic structures for **DFT calculations (e.g. VASP)** often requires repetitive manual editing of structures, defect manipulation, and visualization of atomic configurations.

Many existing tools are either:

- too heavy
- difficult to automate
- or not well suited for defect engineering workflows

**DefectsStudio** aims to provide a lightweight alternative focused on:

- defect creation and manipulation
- structure visualization
- fast editing of POSCAR-style structures
- preparing simulation inputs

---

# Key Features

Current capabilities include:

- **3D visualization of atomic structures**
- **POSCAR / CONTCAR import and export**
- **instanced atom rendering**
- **orbit camera navigation**
- **gizmo-based transformations**
---

# Planned Features

Future development will include:

### Bonds and Measurements
- automatic bond detection
- configurable cutoff distances
- element-specific bonding rules
- bond length measurement
- bond angle measurement

### Volumetric Data
- support for **CHGCAR**
- support for **PARCHG**
- isosurface visualization

### Rendering
- offscreen rendering
- high resolution export
- screenshot system

### UI Improvements
- better settings persistence
- improved viewport overlays
- logging and diagnostics tools

### Testing and Documentation
- parser validation tests
- round-trip file validation
- example structures and datasets

---


# Building the Project

## Requirements

- Windows 10 / 11
- Visual Studio 2022
- C++ toolset v143
- PowerShell

## Setup
1. Clone the repo (preferred): `git clone --recurse-submodules https://github.com/SirJamesClarkMaxwell/Defect-Studio.git`
2. If you already cloned without submodules, run: `git submodule update --init --recursive`
3. Run the setup script `./scripts/Setup.bat`
4. Open: `DefectsStudio.sln` in **Visual Studio 2022**.
5. Build configuration: `Debug` or `Release`

Set **DefectsStudio** as the startup project and run the application (`F5`).

---

# Build Utilities

Helper scripts included in the repository:
- `scripts/Verify-Build.bat` - Verifies Debug and Release builds.
- `scripts/Verify-Build-And-Run.bat` - Builds and launches the application.
- `scripts/Run.bat` - Launches existing Debug/Release executable.

External dependencies are managed using **git submodules** (for example `vendor/glfw`, `vendor/glad`, `vendor/imgui`, `vendor/glm`, `vendor/imguizmo`, `vendor/imviewguizmo`).
`scripts/Setup.bat` automatically runs submodule sync/init/update.

If project resources are modified (for example `assets/icon.rc`), run the setup script again.

---

# Project Vision

The long-term goal of **DefectsStudio** is to become a **specialized environment for defect engineering and atomistic modeling**, combining:

- the usability of modern DCC tools
- lightweight performance
- workflows optimized for materials science
