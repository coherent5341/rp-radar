#!/usr/bin/env python3
"""
Live view of the CSV stream from swing_tracker or ball_speed.

    python3 tools/monitor.py /dev/ttyACM0
    python3 tools/monitor.py /dev/ttyACM0 --raw | tee swing.csv

--raw passes lines straight through, which is what you want when capturing to a
file for tools/plot_swing.py. Without it, track records are collapsed into a
table that refreshes in place, and shots are printed as they complete.

Needs pyserial (pip install pyserial). Reading the port with any other terminal
program works too; this only exists to make the stream readable while you swing.
"""

import argparse
import sys
import time

CLASS_COLOUR = {
    "CLUB": "\033[36m",
    "BALL": "\033[33m",
    "----": "\033[90m",
}
RESET = "\033[0m"


def open_port(path, baud):
    try:
        import serial
    except ImportError:
        sys.exit("pyserial is required: pip install pyserial")

    try:
        # USB CDC ignores the baud rate, but pyserial still wants one.
        return serial.Serial(path, baud, timeout=0.2)
    except Exception as exc:  # noqa: BLE001 - surfaced verbatim to the user
        sys.exit(f"could not open {path}: {exc}")


class Display:
    """Collapses the per-frame firehose into something a human can read."""

    def __init__(self, refresh_hz=10.0):
        self.tracks = {}
        self.frame = None
        self.shots = []
        self.stats = None
        self.period = 1.0 / refresh_hz
        self.last_draw = 0.0
        self.peak_speed = 0.0

    def handle(self, line):
        parts = line.split(",")
        kind = parts[0]

        try:
            if kind == "T" and len(parts) >= 8:
                t_ms = int(parts[1])
                track_id = int(parts[2])
                self.tracks[track_id] = {
                    "t_ms": t_ms,
                    "class": parts[3],
                    "range_m": float(parts[4]),
                    "speed": float(parts[5]),
                    "amp_db": float(parts[6]),
                    "hits": int(parts[7]),
                }
                self.peak_speed = max(self.peak_speed, abs(float(parts[5])))
            elif kind == "F" and len(parts) >= 5:
                self.frame = {
                    "t_ms": int(parts[1]),
                    "detections": int(parts[2]),
                    "coarse": float(parts[3]),
                    "conf": float(parts[4]),
                }
            elif kind == "V" and len(parts) >= 4:
                self.peak_speed = max(self.peak_speed, abs(float(parts[2])))
                self.frame = {
                    "t_ms": int(parts[1]),
                    "detections": 1,
                    "coarse": float(parts[2]),
                    "conf": float(parts[3]),
                }
            elif kind == "SHOT" and len(parts) >= 8:
                self.shots.append(
                    {
                        "id": int(parts[1]),
                        "club": float(parts[2]),
                        "ball": float(parts[3]),
                        "smash": float(parts[4]),
                        "confidence": float(parts[6]),
                        "flags": ",".join(parts[8:]) if len(parts) > 8 else "",
                    }
                )
            elif kind == "S" and len(parts) >= 5:
                self.stats = {
                    "frames": int(parts[1]),
                    "delayed": int(parts[2]),
                    "saturated": int(parts[3]),
                    "recal": int(parts[4]),
                }
            else:
                return False
        except ValueError:
            return False

        return True

    def expire(self, now_ms, ttl_ms=500):
        stale = [k for k, v in self.tracks.items() if (now_ms - v["t_ms"]) > ttl_ms]
        for key in stale:
            del self.tracks[key]

    def draw(self, force=False):
        now = time.time()
        if not force and (now - self.last_draw) < self.period:
            return
        self.last_draw = now

        if self.frame is not None:
            self.expire(self.frame["t_ms"])

        out = ["\033[H\033[J"]  # home, clear
        out.append("rp-radar monitor    ctrl-c to stop\n\n")

        if self.frame is not None:
            out.append(
                f"  t {self.frame['t_ms'] / 1000.0:9.3f} s"
                f"   detections {self.frame['detections']:2d}"
                f"   peak seen {self.peak_speed:6.1f} m/s\n\n"
            )

        out.append("  id  class   range     speed      amp   hits\n")
        out.append("  " + "-" * 46 + "\n")

        if self.tracks:
            for track_id in sorted(self.tracks):
                t = self.tracks[track_id]
                colour = CLASS_COLOUR.get(t["class"], "")
                out.append(
                    f"  {track_id:3d}  {colour}{t['class']:5s}{RESET}"
                    f"  {t['range_m']:6.2f} m  {t['speed']:7.1f} m/s"
                    f"  {t['amp_db']:6.1f}  {t['hits']:4d}\n"
                )
        else:
            out.append("  (nothing moving)\n")

        if self.shots:
            out.append("\n  shots\n")
            out.append("  " + "-" * 46 + "\n")
            for shot in self.shots[-8:]:
                flag = "  !" if "implausible" in shot["flags"] else ""
                out.append(
                    f"  {shot['id']:3d}  club {shot['club']:5.1f}"
                    f"  ball {shot['ball']:5.1f}"
                    f"  smash {shot['smash']:4.2f}"
                    f"  conf {shot['confidence']:4.2f}{flag}\n"
                )

        if self.stats is not None:
            out.append(
                f"\n  frames {self.stats['frames']}"
                f"   delayed {self.stats['delayed']}"
                f"   saturated {self.stats['saturated']}"
                f"   recalibrations {self.stats['recal']}\n"
            )
            if self.stats["delayed"] > 0:
                out.append("  delayed frames: raise the SPI clock or shorten frames\n")

        sys.stdout.write("".join(out))
        sys.stdout.flush()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="serial device, e.g. /dev/ttyACM0 or COM3")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--raw", action="store_true", help="pass lines through unchanged")
    args = parser.parse_args()

    port = open_port(args.port, args.baud)
    display = Display()

    try:
        while True:
            raw = port.readline()
            if not raw:
                if not args.raw:
                    display.draw()
                continue

            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue

            if args.raw:
                print(line, flush=True)
                continue

            # Anything the parser does not recognise is firmware log output,
            # which is worth seeing rather than swallowing.
            if not display.handle(line):
                sys.stdout.write("\033[H\033[J" + line + "\n")
                sys.stdout.flush()
                display.last_draw = 0.0
            else:
                display.draw()
    except KeyboardInterrupt:
        if not args.raw:
            display.draw(force=True)
        print()


if __name__ == "__main__":
    main()
