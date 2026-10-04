"""Records IR sweeps at known distances, to see how the margin depends on them.

For every distance the target (a sheet of white paper, say) is placed that
far from the IR end of the stick, and the script takes a number of `sw`
sweeps and appends them to a CSV file:

    cm, sweep, db, state, noise, carrier, bursts, gap, h0 .. h23

At the end it prints the mean and spread of the margin per distance.

    PY=~/.platformio/penv/bin/python
    $PY tools/ir_experiment.py ir.csv 5 10 20 40        # asks before each distance
    $PY tools/ir_experiment.py ir.csv 15 --no-prompt    # measures right away
    $PY tools/ir_experiment.py ir.csv --summary         # only prints the summary

A distance of 0 stands for "nothing in front": the stick looking into an
empty room.
"""
import argparse
import csv
import os
import statistics
import time

from serial_cmd import Reader, open_port, send

LEVELS = 24


def sweep(link, reader):
    """One `sw` sweep as a dict, or None if the board did not answer."""
    for _ in range(3):  # right after the port opens an answer can go missing
        send(link, "sw")
        line = reader.line(time.time() + 3, want="SW ")
        if line:
            break
    else:
        return None
    fields = dict(part.split("=", 1) for part in line.split()[1:])
    hits = [int(h) for h in fields["hits"].split(",")]
    return {
        "db": float(fields["db"]),
        "state": fields["state"],
        "noise": int(fields["noise"]),
        "carrier": int(fields["f"]),
        "bursts": int(fields["n"]),
        "gap": int(fields["gap"]),
        "hits": hits,
    }


def record(path, cm, sweeps, link, reader):
    new = not os.path.exists(path)
    with open(path, "a", newline="") as f:
        out = csv.writer(f)
        if new:
            out.writerow(["cm", "sweep", "db", "state", "noise", "carrier", "bursts", "gap"]
                         + ["h%d" % i for i in range(LEVELS)])
        for i in range(sweeps):
            s = sweep(link, reader)
            if s is None:
                print("  no answer from the board")
                continue
            out.writerow([cm, i, s["db"], s["state"], s["noise"], s["carrier"], s["bursts"], s["gap"]]
                         + s["hits"])
            print("  %5.1f dB  %s" % (s["db"], s["state"]))


def summary(path):
    by_cm = {}
    with open(path) as f:
        for row in csv.DictReader(f):
            by_cm.setdefault(float(row["cm"]), []).append(row)
    print("   cm   n   mean dB  stdev  min   max   states")
    for cm in sorted(by_cm):
        rows = by_cm[cm]
        dbs = [float(r["db"]) for r in rows]
        states = {}
        for r in rows:
            states[r["state"]] = states.get(r["state"], 0) + 1
        spread = statistics.stdev(dbs) if len(dbs) > 1 else 0.0
        print("%5g  %2d   %6.1f   %5.1f  %5.1f %5.1f  %s" % (
            cm, len(rows), statistics.mean(dbs), spread, min(dbs), max(dbs),
            " ".join("%s:%d" % kv for kv in sorted(states.items()))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("distances", nargs="*", type=float, help="cm; 0 = nothing in front")
    ap.add_argument("--sweeps", type=int, default=10)
    ap.add_argument("--no-prompt", action="store_true")
    ap.add_argument("--summary", action="store_true")
    args = ap.parse_args()

    if args.distances and not args.summary:
        link = open_port()
        reader = Reader(link)
        for cm in args.distances:
            if not args.no_prompt:
                input("Target at %g cm, then Enter: " % cm)
            print("%g cm:" % cm)
            record(args.csv, cm, args.sweeps, link, reader)
    if os.path.exists(args.csv):
        summary(args.csv)


if __name__ == "__main__":
    main()
