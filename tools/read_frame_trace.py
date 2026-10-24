#!/usr/bin/env python3
"""Decode the bounded DMTR capture; submissions never imply scanout or input latency."""
import argparse
import csv
import json
from pathlib import Path
import struct

HEADER = struct.Struct("<6IQ")
RECORDS = {1: struct.Struct("<10I6i14q"), 2: struct.Struct("<10I7i4x14q")}
FIELDS = ("iteration native_completed generation windowed dynamic_upload uploaded_iteration "
          "submitted_iteration present_attempts source destination").split()
RESULTS = ("source_lock upload_lock upload_unlock source_unlock upload_copy present final_copy").split()
POINTS = ("entry source_lock_begin source_lock_end upload_lock_begin upload_lock_end "
          "upload_unlock_begin upload_unlock_end source_unlock_begin source_unlock_end "
          "upload_copy_begin upload_copy_end final_copy_begin present_begin present_end").split()


def read_trace(path):
    data = Path(path).read_bytes()
    if len(data) < HEADER.size:
        raise ValueError("incomplete trace header (capture must finish before game exit)")
    magic, version, size, count, pid, witness, frequency = HEADER.unpack_from(data)
    record = RECORDS.get(version)
    if magic != 0x52544D44 or record is None or size != record.size or not frequency or count > 32768:
        raise ValueError("unsupported or invalid trace header")
    if len(data) != HEADER.size + count * size:
        raise ValueError("incomplete trace records")
    rows = []
    result_count = 6 if version == 1 else 7
    for values in record.iter_unpack(data[HEADER.size:]):
        row = dict(zip(FIELDS, values[:10]))
        row["final_copy_hr"] = 0x7fffffff
        row.update(zip((name + "_hr" for name in RESULTS), values[10:10 + result_count]))
        row.update(zip(POINTS, values[10 + result_count:]))
        if rows and (row["iteration"] <= rows[-1]["iteration"] or row["entry"] < rows[-1]["entry"]):
            raise ValueError("trace iterations or QPC timestamps are not ordered")
        rows.append(row)
    return {"version": version, "pid": pid, "native_witness": bool(witness), "qpc_frequency": frequency}, rows


def distribution(values):
    if not values:
        return None
    ordered = sorted(values)
    def percentile(fraction):
        return ordered[min(len(ordered) - 1, int((len(ordered) - 1) * fraction + .5))]
    return {"count": len(ordered), "p50": percentile(.5), "p95": percentile(.95),
            "p99": percentile(.99), "max": ordered[-1], "over_1ms": sum(x > 1 for x in values),
            "over_25ms": sum(x > 25 for x in values)}


def summarize(header, rows):
    frequency = header["qpc_frequency"]
    seconds = (rows[-1]["entry"] - rows[0]["entry"]) / frequency if len(rows) > 1 else 0
    updates = ((rows[-1]["native_completed"] - rows[0]["native_completed"]) & 0xffffffff
               if rows and header["native_witness"] else None)
    successful = [r for r in rows if r["present_attempts"] and r["present_hr"] == 0]
    matched = [r for r in successful if r["submitted_iteration"] == r["iteration"] and r["final_copy_hr"] == 0]
    intervals = [(b["entry"] - a["entry"]) * 1000 / frequency for a, b in zip(rows, rows[1:])]
    pairs = {name: (name + "_begin", name + "_end") for name in RESULTS[:5]}
    pairs.update(present=("present_begin", "present_end"),
                 entry_to_present=("entry", "present_begin"),
                 final_copy_to_present=("final_copy_begin", "present_begin"))
    timings = {}
    for name, (begin, end) in pairs.items():
        timings[name] = distribution([(r[end] - r[begin]) * 1000 / frequency
                                     for r in rows if r[begin] and r[end] >= r[begin]])
    return {
        "iterations": len(rows), "first_iteration": rows[0]["iteration"] if rows else None,
        "last_iteration": rows[-1]["iteration"] if rows else None, "span_seconds": seconds,
        "render_iterations_per_second": (len(rows) - 1) / seconds if seconds else None,
        "native_completed_updates": updates,
        "native_updates_per_second": updates / seconds if updates is not None and seconds else None,
        "present_attempts": sum(r["present_attempts"] for r in rows),
        "successful_submissions": len(successful), "same_iteration_submissions": len(matched),
        "unidentified_submissions": len(successful) - len(matched),
        "windowed_iterations": sum(bool(r["windowed"]) for r in rows),
        "dynamic_upload_iterations": sum(bool(r["dynamic_upload"]) for r in rows),
        "present_hresult_counts": {f"{hr & 0xffffffff:08x}": sum(
            r["present_attempts"] for r in rows if r["present_hr"] == hr)
            for hr in sorted({r["present_hr"] for r in rows if r["present_attempts"]})},
        "iteration_intervals_ms": distribution(intervals), "timings_ms": timings,
        "displayed_images": None, "driver_queue_age_ms": None, "input_latency_ms": None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--csv", type=Path)
    parser.add_argument("--first", type=int, default=1, help="first iteration, inclusive")
    parser.add_argument("--last", type=int, default=0, help="last iteration, inclusive; 0 = all")
    args = parser.parse_args()
    try:
        header, rows = read_trace(args.trace)
        rows = [r for r in rows if r["iteration"] >= args.first and
                (not args.last or r["iteration"] <= args.last)]
        if args.csv:
            with args.csv.open("w", newline="", encoding="utf-8") as output:
                writer = csv.DictWriter(output, fieldnames=FIELDS + [n + "_hr" for n in RESULTS] + POINTS)
                writer.writeheader()
                writer.writerows(rows)
        print(json.dumps({"header": header, **summarize(header, rows)}, indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"trace: {error}\n")


if __name__ == "__main__":
    main()
