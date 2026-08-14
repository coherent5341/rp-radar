#!/usr/bin/env python3
"""
Plot a capture from swing_tracker or ball_speed.

    python3 tools/monitor.py /dev/ttyACM0 --raw | tee swing.csv
    python3 tools/plot_swing.py swing.csv

Produces range against time and speed against time, one series per track,
with detected shots marked. Needs matplotlib.
"""

import argparse
import sys
from collections import defaultdict

CLASS_STYLE = {
    "CLUB": {"color": "tab:blue", "marker": "o"},
    "BALL": {"color": "tab:orange", "marker": "^"},
    "----": {"color": "tab:gray", "marker": "."},
}


def parse(path):
    tracks = defaultdict(lambda: {"t": [], "range": [], "speed": [], "class": "----"})
    live = {"t": [], "speed": []}
    shots = []

    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            parts = line.split(",")
            try:
                if parts[0] == "T" and len(parts) >= 8:
                    track_id = int(parts[2])
                    entry = tracks[track_id]
                    entry["t"].append(int(parts[1]) / 1000.0)
                    entry["class"] = parts[3]
                    entry["range"].append(float(parts[4]))
                    entry["speed"].append(float(parts[5]))
                elif parts[0] == "V" and len(parts) >= 4:
                    live["t"].append(int(parts[1]) / 1000.0)
                    live["speed"].append(float(parts[2]))
                elif parts[0] == "SHOT" and len(parts) >= 8:
                    shots.append(
                        {
                            "id": int(parts[1]),
                            "club": float(parts[2]),
                            "ball": float(parts[3]),
                            "smash": float(parts[4]),
                            "t_impact": int(parts[5]) / 1000.0,
                        }
                    )
            except (ValueError, IndexError):
                # Partial line from an interrupted capture; skip it.
                continue

    return tracks, live, shots


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", help="capture from monitor.py --raw")
    parser.add_argument("--save", help="write to this image file instead of showing")
    args = parser.parse_args()

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit("matplotlib is required: pip install matplotlib")

    tracks, live, shots = parse(args.csv)

    if not tracks and not live:
        sys.exit(f"no track or speed records found in {args.csv}")

    fig, (ax_range, ax_speed) = plt.subplots(2, 1, sharex=True, figsize=(11, 7))

    for track_id, entry in sorted(tracks.items()):
        style = CLASS_STYLE.get(entry["class"], CLASS_STYLE["----"])
        label = f"track {track_id} ({entry['class']})"

        ax_range.plot(entry["t"], entry["range"], linestyle="-", markersize=3,
                      label=label, **style)
        ax_speed.plot(entry["t"], entry["speed"], linestyle="-", markersize=3,
                      label=label, **style)

    if live["t"]:
        ax_speed.plot(live["t"], live["speed"], ".", color="tab:green", markersize=3,
                      label="peak speed")

    for shot in shots:
        for axis in (ax_range, ax_speed):
            axis.axvline(shot["t_impact"], color="tab:red", linestyle="--", alpha=0.6)
        ax_speed.annotate(
            f"shot {shot['id']}\nclub {shot['club']:.1f}\nball {shot['ball']:.1f}\n"
            f"smash {shot['smash']:.2f}",
            xy=(shot["t_impact"], shot["ball"]),
            xytext=(6, 6),
            textcoords="offset points",
            fontsize=8,
            color="tab:red",
        )

    ax_range.set_ylabel("range (m)")
    ax_range.grid(alpha=0.3)
    ax_range.legend(fontsize=8, loc="upper right")

    ax_speed.set_ylabel("radial speed (m/s)")
    ax_speed.set_xlabel("time (s)")
    ax_speed.axhline(0.0, color="black", linewidth=0.6)
    ax_speed.grid(alpha=0.3)

    fig.suptitle(args.csv)
    fig.tight_layout()

    if args.save:
        fig.savefig(args.save, dpi=140)
        print(f"wrote {args.save}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
