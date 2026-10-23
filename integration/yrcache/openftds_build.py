"""Build OpenFTDS as a C11 static archive for a torch C++/CUDA extension."""

from __future__ import annotations

import os
import shlex
import subprocess
from pathlib import Path


def build_openftds(build_temp: str | os.PathLike[str]) -> str:
    """Return the absolute path to a PIC libopenftds.a.

    This file is expected at third_party/open-ftds/integration/yrcache/ inside
    YRCache. It deliberately invokes CC and AR itself so torch/ninja never
    sends openftds.c to nvcc or compiles it as C++.
    """
    source_root = Path(__file__).resolve().parents[2]
    output = Path(build_temp).resolve() / "openftds"
    output.mkdir(parents=True, exist_ok=True)
    obj = output / "openftds.o"
    archive = output / "libopenftds.a"
    cc = shlex.split(os.environ.get("CC", "cc"))
    ar = shlex.split(os.environ.get("AR", "ar"))
    command = [
        *cc,
        *shlex.split(os.environ.get("CPPFLAGS", "")),
        *shlex.split(os.environ.get("CFLAGS", "")),
        "-O2",
        "-fPIC",
        "-std=c11",
        "-pthread",
        f"-I{source_root / 'include'}",
        "-c",
        str(source_root / "src" / "openftds.c"),
        "-o",
        str(obj),
    ]
    subprocess.check_call(command)
    subprocess.check_call([*ar, "rcs", str(archive), str(obj)])
    return str(archive)
