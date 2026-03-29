from __future__ import annotations

import argparse

from _workflow_common import build_windows, fail, generate_project_files, stop_running_defectsstudio


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verify DefectsStudio builds in Debug and Release configurations.")
    parser.add_argument(
        "--skip-setup",
        action="store_true",
        help="Skip project-file generation and build with the current solution/toolchain config.",
    )
    parser.add_argument(
        "--skip-submodule-sync",
        action="store_true",
        help="Skip git submodule sync/update because the wrapper already handled it.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        if not args.skip_setup:
            generate_project_files(skip_submodule_sync=args.skip_submodule_sync)

        stop_running_defectsstudio()
        build_windows()
        print("Build verification passed for Debug and Release.")
        return 0
    except RuntimeError as error:
        fail(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
