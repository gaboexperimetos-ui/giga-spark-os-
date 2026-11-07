#!/usr/bin/env python3
"""Compare current C kernels with a git baseline using identical Release flags.
Requires cmake, a C compiler, SDL2 and Python 3. Run from any directory:
  python3 tests/bench/compare.py 6e5c14568ec66b11c9cc58114a8f24474831ef6f --runs 5
"""
import argparse
import csv
import io
import statistics
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, check=True, **kw)

def configure_build(name, core=None, sim=None, strict=True):
    build = ROOT / "build_bench" / name
    cmd = ["cmake", "-S", str(ROOT / "tests/host"), "-B", str(build),
           "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
           f"-DGIGASPARK_STRICT_WARNINGS={'ON' if strict else 'OFF'}"]
    if core: cmd.append(f"-DGIGASPARK_CORE_DIR={core}")
    if sim: cmd.append(f"-DGIGASPARK_SIM_DIR={sim}")
    run(cmd, stdout=subprocess.DEVNULL)
    run(["cmake", "--build", str(build), "--parallel", "2"], stdout=subprocess.DEVNULL)
    return build

def samples(build, binary, runs):
    collected = {}
    hashes = {}
    for _ in range(runs):
        out = subprocess.check_output([str(build / binary)], text=True, stderr=subprocess.DEVNULL)
        rows = list(csv.DictReader(io.StringIO(out)))
        for row in rows:
            key = row.get("benchmark") or "screen_" + row["screen"]
            collected.setdefault(key, []).append(float(row["ns_per_operation"] if "benchmark" in row else row["ns_per_frame"]))
            if "hash_first" in row:
                hashes[key] = (row["hash_first"], row["hash_last"])
    return {k: statistics.median(v) for k, v in collected.items()}, hashes

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline")
    parser.add_argument("--runs", type=int, default=5)
    args = parser.parse_args()
    if args.runs < 1: parser.error("--runs must be positive")
    baseline_src = ROOT / "build_bench" / "baseline-src"
    baseline_src.mkdir(parents=True, exist_ok=True)
    for rel in ["m4/src/mem_engine.c", "m4/src/mem_engine.h", "m4/src/zram_comp.c",
                "m4/src/zram_comp.h", "simulator/main.c"]:
        data = subprocess.check_output(["git", "show", f"{args.baseline}:{rel}"], cwd=ROOT)
        (baseline_src / Path(rel).name).write_bytes(data)
    old = configure_build("baseline", baseline_src, baseline_src, strict=False)
    new = configure_build("current")
    old_core, _ = samples(old, "core_bench", args.runs)
    new_core, _ = samples(new, "core_bench", args.runs)
    old_sim, old_hashes = samples(old, "sim_bench", args.runs)
    new_sim, new_hashes = samples(new, "sim_bench", args.runs)
    if old_hashes != new_hashes:
        raise SystemExit("Pixel hashes differ; investigate before accepting performance changes")
    print(f"baseline={args.baseline}; runs={args.runs}; median of best-of-5 per run")
    print("benchmark,baseline_ns,current_ns,speedup")
    for key in old_core:
        print(f"{key},{old_core[key]:.3f},{new_core[key]:.3f},{old_core[key]/new_core[key]:.3f}x")
    for key in old_sim:
        print(f"{key},{old_sim[key]:.3f},{new_sim[key]:.3f},{old_sim[key]/new_sim[key]:.3f}x")
    print("Pixel hashes match. This does not measure idle CPU, RTOS latency or hardware.")

if __name__ == "__main__":
    main()
