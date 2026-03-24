#!/usr/bin/env python3
import json
import sys
import os
from typing import Any, cast

def load_json(path: str) -> dict[str, Any] | None:
    if not os.path.exists(path):
        print(f"Warning: {path} not found.")
        return None
    with open(path, 'r') as f:
        try:
            data = json.load(f)
            if not isinstance(data, dict):
                print(f"Warning: Unexpected data structure in {path}.")
                return None
            return data
        except json.JSONDecodeError:
            print(f"Warning: Failed to parse {path}.")
            return None

def format_diff(new, old):
    if old is None or new is None or old == 0:
        return "N/A"
    diff = (new - old) / old * 100
    color = "🟢" if diff < -1 else "🔴" if diff > 1 else "⚪"
    return f"{diff:+.2f}% {color}"

def main():
    if len(sys.argv) < 3:
        print("Usage: compare_benchmarks.py old.json new.json")
        sys.exit(1)

    old_data = load_json(sys.argv[1])
    new_data = load_json(sys.argv[2])

    print("# Performance Comparison Report")
    
    if old_data is None or new_data is None:
        print("\n> [!WARNING]")
        print("> Could not compare performance because one of the result files is missing or invalid.")
        print("> This is expected during the very first run of the CI until benchmarking is merged into main.")
        return

    old = cast(dict[str, Any], old_data)
    new = cast(dict[str, Any], new_data)

    print("\n| Metric | Base (ms) | PR (ms) | Diff |")
    print("| :--- | :---: | :---: | :---: |")

    # Image Loading
    for key in ["loading_grayscale", "loading_rgb"]:
        if key in old and key in new:
            o_data = old[key]
            n_data = new[key]
            if isinstance(o_data, dict) and isinstance(n_data, dict):
                o_val = o_data.get("avg_ms")
                n_val = n_data.get("avg_ms")
                if o_val is not None and n_val is not None:
                    print(f"| Loading ({key.split('_')[1]}) | {o_val:.4f} | {n_val:.4f} | {format_diff(n_val, o_val)} |")

    # Hashing Algorithms
    if "hashing" in old and "hashing" in new:
        old_h_list = old["hashing"]
        new_h_list = new["hashing"]
        
        if isinstance(old_h_list, list) and isinstance(new_h_list, list):
            old_h = {v["name"]: v["avg_ms"] for v in old_h_list if isinstance(v, dict) and "name" in v and "avg_ms" in v}
            new_h = {v["name"]: v["avg_ms"] for v in new_h_list if isinstance(v, dict) and "name" in v and "avg_ms" in v}
            
            for name in old_h:
                if name in new_h:
                    o_val = old_h[name]
                    n_val = new_h[name]
                    print(f"| Hash: {name} | {o_val:.4f} | {n_val:.4f} | {format_diff(n_val, o_val)} |")

    # Full Pipeline
    if "full_pipeline" in old and "full_pipeline" in new:
        o_data = old["full_pipeline"]
        n_data = new["full_pipeline"]
        if isinstance(o_data, dict) and isinstance(n_data, dict):
            o_val = o_data.get("avg_ms")
            n_val = n_data.get("avg_ms")
            if o_val is not None and n_val is not None:
                print(f"| Full Pipeline | {o_val:.4f} | {n_val:.4f} | {format_diff(n_val, o_val)} |")

if __name__ == "__main__":
    main()
