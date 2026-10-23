#!/usr/bin/env python3
"""Exercise the YRCache static-library helper with the host toolchain."""

import importlib.util
import os
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    helper_path = root / "integration" / "yrcache" / "openftds_build.py"
    spec = importlib.util.spec_from_file_location("openftds_build", helper_path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with tempfile.TemporaryDirectory() as temporary:
        archive = Path(module.build_openftds(temporary))
        assert archive.is_file()
        listing = subprocess.run(
            [os.environ.get("AR", "ar"), "t", str(archive)],
            check=True,
            text=True,
            capture_output=True,
        ).stdout
        assert "openftds.o" in listing
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
