# DefectsStudio (MVP WIP)

Desktop editor/visualizer for VASP structures (Windows, OpenGL + Dear ImGui docking).

## Quick start

1. Run:
   - `scripts/Setup.bat`
2. Open generated `DefectsStudio.sln` in Visual Studio 2022.
3. Build `Debug|x64` or `Release|x64`.
4. Run `DefectsStudio`.

## Current status

- Premake5 build generation for VS2022
- C++23 workspace with Debug/Release
- App starts with ImGui DockSpace and MVP viewport panel
- Foundational layered architecture in place (`Core`, `Layers`, `Renderer`, `IO`, `DataModel`, `Editor`, `UI`)
- In-app logger with `Log / Errors` panel

## Controls (current MVP)

- Docking enabled in ImGui
- Menu placeholders for import/export
- Theme presets in `Tools` panel (including `PhotoshopStyle`)
- Theme and UI toggles are persisted in `config/editor_ui_settings.ini`

## VS Code integration

Project includes `.vscode` configuration with:
- Build/debug launch profile for `DefectsStudio`
- Tasks for setup, debug/release build, full verification, and run

Useful commands:
- `Terminal -> Run Task -> setup: generate vs2022 solution`
- `Terminal -> Run Task -> verify: full build check`
- `Terminal -> Run Task -> run: debug exe`

## Notes

- `scripts/FetchDeps.ps1` fetches initial dependencies into `vendor/`.
- Additional editor features from prompt are tracked in `TODO.md` by task branches.
