"""Benchmark a scene from a save state: boot, quick-load the state, let it settle, measure game frames per second.

usage: python tools/autotest/bench_state.py [--state bench/states/select4.state] [--measure 20] [--env K=V ...]
Output: bench/runs/<tag>/ (urban.out/err, final.png) and one line with frames/s over the measured window.
Game frames are counted from the per-frame call to 0x128dc0 (URBAN_CALL_TRACE), as in the FPS overlay.
"""
import argparse
import os
import re
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WINDOWS = os.name == "nt"
BIN = ROOT / "build" / ("port" if WINDOWS else "linux") / "bin"
CAPTURE = ROOT / "tools" / "win" / "capture_window.ps1"


def capture(out: Path) -> bool:
    if WINDOWS:
        cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(CAPTURE),
               "-TitleEquals", "Urban Reign", "-OutFile", str(out)]
    else:
        cmd = ["import", "-window", "Urban Reign", str(out)]  # ImageMagick (X11 / XWayland)
    try:
        r = subprocess.run(cmd, capture_output=True)
    except FileNotFoundError:
        return False
    return r.returncode == 0 and out.exists() and out.stat().st_size > 0


def linux_thread_times(pid: int) -> dict:
    """Linux: CPU seconds used so far by each thread name of the process (/proc)."""
    result = {}
    tick = os.sysconf("SC_CLK_TCK")
    for tid in os.listdir(f"/proc/{pid}/task"):
        try:
            name = open(f"/proc/{pid}/task/{tid}/comm").read().strip()
            fields = open(f"/proc/{pid}/task/{tid}/stat").read().rsplit(")", 1)[1].split()
            result[name] = result.get(name, 0.0) + (int(fields[11]) + int(fields[12])) / tick
        except OSError:
            pass
    return result


def frame_times(path: Path):
    times = []
    for line in open(path, encoding="utf-8", errors="replace"):
        if "call 0x128dc0" in line:
            m = re.match(r"\[\s*([0-9.]+)\]", line)
            if m:
                times.append(float(m.group(1)))
    return times


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--state", default=str(ROOT / "bench" / "states" / "select4.state"))
    ap.add_argument("--load-after", type=float, default=10.0, help="seconds after start to request the quick-load")
    ap.add_argument("--warmup", type=float, default=8.0, help="seconds after the load before measuring")
    ap.add_argument("--measure", type=float, default=20.0)
    ap.add_argument("--reload-after", type=float, default=0.0,
                    help="load the state again this many seconds after the first load (mid-game), then measure")
    ap.add_argument("--tag", default=time.strftime("bench_%H%M%S"))
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--affinity", default="",
                    help="logical CPUs for the game from boot (PS2X_PROCESS_AFFINITY), e.g. 0-3 or 0,1,2,3: stands in for a smaller machine")
    ap.add_argument("--exe", default="urban.exe" if WINDOWS else "urban",
                    help="executable in build/port/bin, build/linux/bin on Linux (e.g. urban_avx2.exe to compare builds)")
    ap.add_argument("--threads", action="store_true",
                    help="run tools/thread_usage.py over the measured window; output in <tag>/threads.txt")
    ap.add_argument("--pnach", default="none",
                    help="PCSX2 .pnach file(s) (';'-separated) active during the run; default none, whatever controls.ini says")
    a = ap.parse_args()

    out = ROOT / "bench" / "runs" / a.tag
    out.mkdir(parents=True, exist_ok=True)
    fifo = out / "pad_fifo.txt"
    fifo.write_text("")
    # Always progressive (the state was saved in it), whatever controls.ini says: environment values win over it.
    env = dict(os.environ, PS2X_PAD_FIFO=str(fifo), URBAN_QUICK_STATE=str(Path(a.state).resolve()),
               URBAN_CALL_TRACE="128dc0", URBAN_PROGRESSIVE="1", PS2X_PNACH=a.pnach)
    if a.affinity:
        # Applied by the game at boot (Port thread_policy.cpp), before the GL context exists, like a real small machine.
        env["PS2X_PROCESS_AFFINITY"] = a.affinity
    for kv in a.env:
        k, v = kv.split("=", 1)
        env[k] = v
    t0 = time.time()
    game = subprocess.Popen([str(BIN / a.exe)], cwd=BIN, env=env,
                            stdout=open(out / "urban.out", "w"), stderr=open(out / "urban.err", "w"))
    usage = None
    try:
        time.sleep(a.load_after)
        with open(fifo, "a") as f:
            f.write("!load\n")
        load_at = time.time() - t0
        if a.reload_after > 0:
            time.sleep(a.reload_after)
            with open(fifo, "a") as f:
                f.write("!load\n")
            load_at = time.time() - t0
        time.sleep(a.warmup)
        before = linux_thread_times(game.pid) if a.threads and not WINDOWS else None
        if a.threads and WINDOWS:
            usage = subprocess.Popen(["python", str(ROOT / "tools" / "thread_usage.py"), "--process", a.exe,
                                      "--seconds", str(max(5.0, a.measure - 2.0)), "--interval", "5"],
                                     stdout=open(out / "threads.txt", "w"), stderr=subprocess.STDOUT)
        time.sleep(a.measure)
        if usage:
            usage.wait()
        if before is not None:
            after = linux_thread_times(game.pid)
            rows = sorted(((after[n] - before.get(n, 0.0)) / a.measure * 100.0, n) for n in after)
            (out / "threads.txt").write_text("".join(f"{n}: {u:.1f}%\n" for u, n in reversed(rows) if u >= 0.5))
        capture(out / "final.png")
    finally:
        game.kill()
        game.wait()
    if a.pnach != "none":
        err = (out / "urban.err").read_text(encoding="utf-8", errors="replace")
        loaded = [l for l in err.splitlines() if "patches loaded" in l]
        applied = [l for l in err.splitlines() if "writes applied so far" in l]
        if not loaded or not applied:
            print(f"{a.tag}: PNACH NOT ACTIVE (loaded: {loaded}, applied: {applied[-1:] if applied else []})")
            return
        print(f"{a.tag}: pnach active: {loaded[0].split('] ', 1)[-1]}; {applied[-1].split('] ', 1)[-1]}")
    times = frame_times(out / "urban.out")
    start = load_at + a.warmup
    window = [t for t in times if start <= t < start + a.measure]
    if not window:
        print(f"{a.tag}: no frames in the window (game hung or exited?)")
        return
    per_second = [sum(1 for t in window if s <= t < s + 1) for s in range(int(start), int(start + a.measure))]
    print(f"{a.tag}: {len(window) / a.measure:.1f} frames/s (median per second {statistics.median(per_second)}, "
          f"min {min(per_second)}, max {max(per_second)})")


if __name__ == "__main__":
    main()
