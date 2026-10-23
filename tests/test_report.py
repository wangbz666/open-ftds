#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    report = sys.argv[1]
    with tempfile.TemporaryDirectory() as raw:
        path = Path(raw) / "sample.ios"
        rows = [
            {"schema": 1, "captured_at_ns": 1, "pid": 7, "event_id": 42,
             "module": "cache", "name": "READ", "level": 1, "count": 10,
             "errors": 1, "slow": 2, "total_ns": 100_000, "min_ns": 1_000,
             "max_ns": 40_000, "hist_us_log2": [0, 2, 3, 5] + [0] * 28},
            {"schema": 1, "captured_at_ns": 2, "pid": 7, "event_id": 42,
             "module": "cache", "name": "READ", "level": 1, "count": 10,
             "errors": 0, "slow": 1, "total_ns": 200_000, "min_ns": 2_000,
             "max_ns": 50_000, "hist_us_log2": [0, 0, 5, 5] + [0] * 28},
        ]
        path.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
        completed = subprocess.run([sys.executable, report, "--json", raw],
                                   text=True, capture_output=True, check=False)
        assert completed.returncode == 0, completed.stderr
        result = json.loads(completed.stdout)
        assert result[0]["count"] == 20
        assert result[0]["errors"] == 1
        assert result[0]["slow"] == 3
        assert result[0]["avg_us"] == 15.0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
