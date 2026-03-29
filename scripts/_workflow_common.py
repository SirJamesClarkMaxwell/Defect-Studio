from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
import zipfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, NoReturn


ROOT = Path(__file__).resolve().parent.parent
SCRIPTS_DIR = ROOT / "scripts"
LOCAL_BUILD_CONFIG_PATH = SCRIPTS_DIR / "local" / "build-toolchain.local.json"
PREMAKE_DIR = ROOT / "tools" / "premake"


@dataclass(slots=True)
class VisualStudioToolchainInfo:
    installation_path: str
    installation_version: str
    display_version: str
    major_version: str
    msbuild_path: str
    platform_toolset: str | None


def section(title: str) -> None:
    print()
    print(f"=== {title} ===")


def fail(message: str, exit_code: int = 1) -> NoReturn:
    print(message)
    raise SystemExit(exit_code)


def run(
    command: list[str],
    *,
    cwd: Path | None = None,
    check: bool = True,
    capture_output: bool = False,
) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        command,
        cwd=str(cwd or ROOT),
        text=True,
        stdout=subprocess.PIPE if capture_output else None,
        stderr=subprocess.STDOUT if capture_output else None,
        check=False,
    )
    if check and completed.returncode != 0:
        raise RuntimeError(f"Command failed with exit code {completed.returncode}: {' '.join(command)}")
    return completed


def venv_python_path() -> Path:
    return ROOT / ".venv" / "Scripts" / "python.exe"


def sync_submodules(skip_submodule_sync: bool) -> None:
    if skip_submodule_sync:
        print("Skipping git submodule sync/update.")
        return

    section("Sync git submodules")
    if run(["git", "submodule", "sync", "--recursive"], check=False).returncode != 0:
        fail("Submodule sync failed.")
    if run(["git", "submodule", "update", "--init", "--recursive"], check=False).returncode != 0:
        fail("Submodule update failed.")


def ensure_required_vendor_paths() -> None:
    for relative in ("vendor/glfw", "vendor/glad", "vendor/imgui", "vendor/glm"):
        if not (ROOT / relative).exists():
            fail(f"Missing required dependency path: {relative}")


def ensure_glad_generated() -> None:
    header = ROOT / "vendor" / "glad_gen" / "include" / "glad" / "gl.h"
    source = ROOT / "vendor" / "glad_gen" / "src" / "gl.c"
    if header.exists() and source.exists():
        return

    section("Generate GLAD")
    run(
        [
            str(venv_python_path()),
            "-m",
            "glad",
            "--api",
            "gl:core=4.1",
            "--out-path",
            "../glad_gen",
            "c",
        ],
        cwd=ROOT / "vendor" / "glad",
    )


def ensure_premake() -> Path:
    PREMAKE_DIR.mkdir(parents=True, exist_ok=True)
    premake_exe = PREMAKE_DIR / "premake5.exe"
    if premake_exe.exists():
        return premake_exe

    section("Download Premake")
    url = "https://github.com/premake/premake-core/releases/download/v5.0.0-beta2/premake-5.0.0-beta2-windows.zip"
    with tempfile.TemporaryDirectory(prefix="defectsstudio-premake-") as temp_dir_string:
        temp_dir = Path(temp_dir_string)
        archive_path = temp_dir / "premake.zip"
        urllib.request.urlretrieve(url, archive_path)
        with zipfile.ZipFile(archive_path) as archive:
            archive.extractall(PREMAKE_DIR)

    if not premake_exe.exists():
        fail(f"Premake bootstrap failed. Expected executable at: {premake_exe}")
    return premake_exe


def get_vswhere_path() -> Path | None:
    program_files_x86 = os.environ.get("ProgramFiles(x86)")
    if not program_files_x86:
        return None

    vswhere = Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    return vswhere if vswhere.exists() else None


def get_available_platform_toolsets(installation_path: str) -> list[str]:
    toolsets_path = Path(installation_path) / "MSBuild" / "Microsoft" / "VC" / "v180" / "Platforms" / "x64" / "PlatformToolsets"
    if not toolsets_path.exists():
        return []

    toolsets = [entry.name for entry in toolsets_path.iterdir() if entry.is_dir() and entry.name.startswith("v") and entry.name[1:].isdigit()]
    return sorted(toolsets, key=lambda name: int(name[1:]), reverse=True)


def get_visual_studio_toolchain_info() -> VisualStudioToolchainInfo:
    section("Locate Visual Studio toolchain")
    vswhere = get_vswhere_path()
    if vswhere is None:
        fail("vswhere.exe was not found. Install Visual Studio 2022 with MSBuild.")

    completed = run(
        [str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.Component.MSBuild", "-format", "json"],
        check=False,
        capture_output=True,
    )
    if completed.returncode != 0 or not completed.stdout.strip():
        fail("Visual Studio / MSBuild was not found. Install Visual Studio 2022 with Desktop development with C++.")

    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError("Failed to parse vswhere JSON output.") from error

    if not payload:
        fail("Visual Studio / MSBuild was not found. Install Visual Studio 2022 with Desktop development with C++.")

    entry = payload[0]
    installation_path = str(entry.get("installationPath", "")).strip()
    installation_version = str(entry.get("installationVersion", "")).strip()
    if not installation_path:
        fail("Visual Studio installation path is missing in vswhere output.")

    msbuild_path = Path(installation_path) / "MSBuild" / "Current" / "Bin" / "MSBuild.exe"
    if not msbuild_path.exists():
        fail("MSBuild.exe was not found in the detected Visual Studio installation.")

    catalog = entry.get("catalog") or {}
    available_toolsets = get_available_platform_toolsets(installation_path)
    platform_toolset = available_toolsets[0] if available_toolsets else None

    return VisualStudioToolchainInfo(
        installation_path=installation_path,
        installation_version=installation_version,
        display_version=str(catalog.get("productDisplayVersion", "")).strip(),
        major_version=installation_version.split(".", 1)[0] if installation_version else "",
        msbuild_path=str(msbuild_path),
        platform_toolset=platform_toolset,
    )


def resolve_premake_action(toolchain: VisualStudioToolchainInfo) -> str:
    explicit = os.environ.get("DEFECTSSTUDIO_PREMAKE_ACTION", "").strip()
    if explicit:
        return explicit

    if toolchain.major_version.isdigit() and int(toolchain.major_version) >= 17:
        return "vs2022"
    if toolchain.major_version == "16":
        return "vs2019"
    return "vs2022"


def local_build_config_payload(toolchain: VisualStudioToolchainInfo, premake_action: str) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "schemaVersion": 2,
        "os": "windows",
        "generatedBy": "scripts/setup.py",
        "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
        "premakeAction": premake_action,
        "compiler": "visualstudio",
        "visualStudioVersion": toolchain.display_version,
        "visualStudioInstallationVersion": toolchain.installation_version,
        "visualStudioMajor": toolchain.major_version,
        "installationPath": toolchain.installation_path,
        "msbuildPath": toolchain.msbuild_path,
    }
    if toolchain.platform_toolset:
        payload["platformToolset"] = toolchain.platform_toolset
    return payload


def write_local_build_config(toolchain: VisualStudioToolchainInfo, premake_action: str) -> None:
    LOCAL_BUILD_CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    LOCAL_BUILD_CONFIG_PATH.write_text(
        json.dumps(local_build_config_payload(toolchain, premake_action), indent=2),
        encoding="utf-8",
    )
    print(f"Updated local build toolchain config: {LOCAL_BUILD_CONFIG_PATH}")


def read_local_build_config() -> dict[str, Any] | None:
    if not LOCAL_BUILD_CONFIG_PATH.exists():
        return None
    try:
        return json.loads(LOCAL_BUILD_CONFIG_PATH.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return None


def generate_project_files(*, skip_submodule_sync: bool) -> VisualStudioToolchainInfo:
    sync_submodules(skip_submodule_sync)
    ensure_required_vendor_paths()
    ensure_glad_generated()

    premake_exe = ensure_premake()
    toolchain = get_visual_studio_toolchain_info()
    premake_action = resolve_premake_action(toolchain)
    write_local_build_config(toolchain, premake_action)

    section("Generate project files")
    run([str(premake_exe), premake_action], cwd=ROOT)
    return toolchain


def stop_running_defectsstudio() -> None:
    section("Close running DefectsStudio")
    result = run(["tasklist", "/FI", "IMAGENAME eq DefectsStudio.exe"], check=False, capture_output=True)
    if "DefectsStudio.exe" not in (result.stdout or ""):
        print("DefectsStudio is not running.")
        return

    if run(["taskkill", "/F", "/IM", "DefectsStudio.exe"], check=False).returncode != 0:
        fail("Failed to stop DefectsStudio. Close the app manually and retry.")


def resolve_msbuild_path() -> tuple[Path, str | None]:
    config = read_local_build_config() or {}
    configured_msbuild = str(config.get("msbuildPath", "")).strip()
    configured_toolset = str(config.get("platformToolset", "")).strip() or None

    if configured_msbuild:
        candidate = Path(configured_msbuild)
        if candidate.exists():
            return candidate, configured_toolset

    toolchain = get_visual_studio_toolchain_info()
    write_local_build_config(toolchain, resolve_premake_action(toolchain))
    return Path(toolchain.msbuild_path), toolchain.platform_toolset


def build_windows() -> None:
    project_path = ROOT / "DefectsStudio.vcxproj"
    if not project_path.exists():
        fail(f"Project file not found: {project_path}")

    msbuild_path, platform_toolset = resolve_msbuild_path()
    build_args = []
    if platform_toolset:
        build_args.append(f"/p:PlatformToolset={platform_toolset}")
        print(f"Using PlatformToolset: {platform_toolset}")

    section("Build Debug|x64")
    if run(
        [str(msbuild_path), str(project_path), "/t:Build", "/p:Configuration=Debug", "/p:Platform=x64", "/v:minimal", *build_args],
        check=False,
    ).returncode != 0:
        fail("Debug build failed.")

    section("Build Release|x64")
    if run(
        [str(msbuild_path), str(project_path), "/t:Build", "/p:Configuration=Release", "/p:Platform=x64", "/v:minimal", *build_args],
        check=False,
    ).returncode != 0:
        fail("Release build failed.")


def resolve_built_application_path() -> Path | None:
    debug_exe = ROOT / "bin" / "Debug-windows-x86_64" / "DefectsStudio" / "DefectsStudio.exe"
    release_exe = ROOT / "bin" / "Release-windows-x86_64" / "DefectsStudio" / "DefectsStudio.exe"
    if debug_exe.exists():
        return debug_exe
    if release_exe.exists():
        return release_exe
    return None


def run_built_application() -> None:
    executable = resolve_built_application_path()
    if executable is None:
        fail("Could not find DefectsStudio executable in Debug or Release output folders.")

    build_name = "Debug" if "Debug-windows-x86_64" in str(executable) else "Release"
    section("Run application")
    print(f"Starting {build_name} build...")
    process = subprocess.Popen([str(executable)], cwd=str(ROOT))
    time.sleep(0.35)
    if process.poll() is not None:
        fail(f"DefectsStudio exited immediately after launch ({build_name}).")
