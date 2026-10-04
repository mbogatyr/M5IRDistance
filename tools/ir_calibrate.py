"""Calibrates the IR range finder: teaches the board which echo margin
means which distance, and checks the result.

The margin depends on the target's surface, so calibrate with the target
you mean to measure (a sheet of white paper by default). The target must
stand still: on its own, upright and facing the IR end of the stick, not
held in a hand. At 10 cm a 5 mm shift is about 1 dB, and a hand's wobble
and tilt spread the readings by several dB. Keep hands, phones and people
out of the beam while the board measures.

    PY=~/.platformio/penv/bin/python
    $PY tools/ir_calibrate.py measure               # 5, 10, 20, 40 cm, asking first
    $PY tools/ir_calibrate.py measure 4 7 12 25 40  # other distances
    $PY tools/ir_calibrate.py check 7 15 30         # the board's estimates vs the truth
    $PY tools/ir_calibrate.py set 5:38.4 10:22.9    # store points measured earlier
    $PY tools/ir_calibrate.py list
    $PY tools/ir_calibrate.py clear

`measure` and `set` replace the points on the board unless --keep is given.
`measure` appends what it stored to docs/data/calibration-log.csv; `check`
appends to docs/data/check-log.csv.

Distances are from the IR end of the stick (the end opposite the USB
socket) to the target's surface.
"""
import argparse
import csv
import datetime
import os
import statistics
import sys
import time

from serial_cmd import Reader, open_port, send

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "data")
STEP_AWAY_S = 3


def answer(link, reader, line, prefixes, seconds):
    """Sends a command and returns the first answer line with one of the
    prefixes, or None."""
    send(link, line)
    deadline = time.time() + seconds
    while time.time() < deadline:
        got = reader.line(deadline)
        if got is not None and got.startswith(prefixes):
            return got
    return None


def fields(line):
    return dict(part.split("=", 1) for part in line.split()[1:] if "=" in part)


def points(link, reader):
    send(link, "callist")
    found = []
    deadline = time.time() + 3
    while time.time() < deadline:
        line = reader.line(deadline)
        if line is None:
            break
        if line.startswith("CALP "):
            f = fields(line)
            found.append((float(f["cm"]), float(f["db"])))
        elif line.startswith("CALN "):
            break
    return found


def show(found):
    if not found:
        print("No calibration points on the board.")
        return
    print("Calibration points on the board:")
    for cm, db in found:
        print("  %5.1f cm  %5.1f dB" % (cm, db))


def clear(link, reader):
    if answer(link, reader, "calclr", ("CALN",), 3) is None:
        sys.exit("The board did not answer calclr.")


def wait_for_target(cm):
    input("Place the target at %g cm, press Enter and step away: " % cm)
    for left in range(STEP_AWAY_S, 0, -1):
        print("  measuring in %d s" % left, end="\r", flush=True)
        time.sleep(1)
    print(" " * 30, end="\r")


def log(name, header, row):
    os.makedirs(DATA, exist_ok=True)
    path = os.path.join(DATA, name)
    new = not os.path.exists(path)
    with open(path, "a", newline="") as f:
        out = csv.writer(f)
        if new:
            out.writerow(header)
        out.writerow(row)


def measure(link, reader, distances, keep):
    if not keep:
        clear(link, reader)
    stamp = datetime.datetime.now().isoformat(timespec="seconds")
    for cm in distances:
        while True:
            wait_for_target(cm)
            # cal takes up to 10 full sweeps of about 1.6 s each.
            line = answer(link, reader, "cal %g" % cm, ("CAL ", "ERR"), 30)
            if line is not None and line.startswith("CAL "):
                f = fields(line)
                print("  %g cm: %s dB, spread %s dB" % (cm, f["db"], f["sd"]))
                log("calibration-log.csv", ["time", "cm", "db", "sd"],
                    [stamp, cm, f["db"], f["sd"]])
                break
            print("  " + (line or "no answer from the board"))
            print("  The board needs 5 clean sweeps: a still target, no IR noise,")
            print("  and an echo that is neither missing nor too strong.")
            if input("  Retry this distance? [Y/n] ").strip().lower() == "n":
                break
    show(points(link, reader))


def check(link, reader, distances, sweeps):
    if len(points(link, reader)) < 2:
        sys.exit("The board has fewer than 2 calibration points: run measure first.")
    stamp = datetime.datetime.now().isoformat(timespec="seconds")
    print("   true   board  error   spread  sweeps")
    for cm in distances:
        wait_for_target(cm)
        estimates = []
        states = []
        for _ in range(sweeps):
            line = answer(link, reader, "sw", ("SW ",), 8)
            if line is None:
                continue
            f = fields(line)
            states.append(f["state"])
            if float(f["cm"]) >= 0:
                estimates.append(float(f["cm"]))
        if not estimates:
            print("  %5g      -      -        -   %s" % (cm, " ".join(states)))
            continue
        mean = statistics.mean(estimates)
        spread = statistics.stdev(estimates) if len(estimates) > 1 else 0.0
        print("  %5g  %6.1f  %+5.1f  %6.1f   %d of %d" % (
            cm, mean, mean - cm, spread, len(estimates), sweeps))
        log("check-log.csv", ["time", "cm", "board_cm", "spread", "sweeps"],
            [stamp, cm, round(mean, 2), round(spread, 2), len(estimates)])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("measure", help="measure distances and store them")
    m.add_argument("distances", nargs="*", type=float, default=[5, 10, 20, 40])
    m.add_argument("--keep", action="store_true", help="add to the points already stored")
    c = sub.add_parser("check", help="compare the board's estimates with the truth")
    c.add_argument("distances", nargs="+", type=float)
    c.add_argument("--sweeps", type=int, default=5)
    s = sub.add_parser("set", help="store points measured earlier, as cm:db")
    s.add_argument("points", nargs="+")
    s.add_argument("--keep", action="store_true")
    sub.add_parser("list")
    sub.add_parser("clear")
    args = ap.parse_args()

    link = open_port()
    reader = Reader(link)
    time.sleep(0.3)  # answers can go missing right after the port opens
    if args.cmd == "measure":
        measure(link, reader, args.distances, args.keep)
    elif args.cmd == "check":
        check(link, reader, args.distances, args.sweeps)
    elif args.cmd == "set":
        if not args.keep:
            clear(link, reader)
        for point in args.points:
            cm, db = point.split(":")
            line = answer(link, reader, "calset %s %s" % (cm, db), ("CAL ", "ERR"), 3)
            print("  " + (line or "no answer from the board"))
        show(points(link, reader))
    elif args.cmd == "list":
        show(points(link, reader))
    elif args.cmd == "clear":
        clear(link, reader)
        print("Calibration cleared.")


if __name__ == "__main__":
    main()
