from __future__ import annotations

import argparse

from _workflow_common import build_windows, fail, generate_project_files, run_built_application, stop_running_defectsstudio


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prepare DefectsStudio on Windows: project files, MSVC config, build, and optional app launch."
    )
    parser.add_argument(
        "--skip-submodule-sync",
        action="store_true",
        help="Skip git submodule sync/update because the wrapper already handled it.",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Stop after generating project files and local toolchain config.",
    )
    parser.add_argument(
        "--skip-run",
        action="store_true",
        help="Build the project but do not start the application.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        generate_project_files(skip_submodule_sync=args.skip_submodule_sync)

        if args.skip_build:
            print("Setup complete.")
            return 0

        stop_running_defectsstudio()
        build_windows()

        if args.skip_run:
            print("Setup and build complete.")
            return 0

        run_built_application()
        print("Setup, build, and run complete.")
        return 0
    except RuntimeError as error:
        fail(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
