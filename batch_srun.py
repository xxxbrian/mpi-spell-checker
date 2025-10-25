#!/usr/bin/env python3

import re
import csv
import argparse
import subprocess
from pathlib import Path
from datetime import datetime

# Configuration
DATA_DIR = "/data/projects/punim0520/2025/Project2"

DICT_WORDS_PAIRS = [
    {
        "name": "626k-658k",
        "dict": "dict626623.txt",
        "words": "words658976.txt",
        "hash": "c6939e15ceaa4338f8f37a394ec66414",
    },
    {
        "name": "100k-100k",
        "dict": "dict100000.txt",
        "words": "words100000.txt",
        "hash": "0134d51e743d94a453b368be0014508d",
    },
    {
        "name": "10k-100k",
        "dict": "dict10000.txt",
        "words": "words100000.txt",
        "hash": "7322bb279422262b4f52545ad90bcc68",
    },
    {
        "name": "100k-10k",
        "dict": "dict100000.txt",
        "words": "words10000.txt",
        "hash": "b49704b453e7a3bab1696b7297c932c6",
    },
    {
        "name": "10k-10k",
        "dict": "dict10000.txt",
        "words": "words10000.txt",
        "hash": "074bf2e9ab0382f37fb732cbc2b53cb1",
    },
]

RESULT_DIR = Path("results")
OUTPUT_DIR = RESULT_DIR / "output"
LOG_DIR = RESULT_DIR / "log"
CSV_PATH = RESULT_DIR / "summary.csv"

MISSPELLED_PATH = Path("results/word_list_misspelled.txt")

# Regex patterns
RE_TIMING = re.compile(
    r"^\[MPI\]\[timing\]\s+([a-zA-Z0-9_]+)\s+avg=([0-9.]+)s\s+min=([0-9.]+)s\s+max=([0-9.]+)s"
)
RE_START_RANKS = re.compile(
    r"^\[MPI\]\s+Starting distributed spell check with\s+(\d+)\s+ranks"
)
RE_LOCAL_SHARD = re.compile(r"^\[MPI\]\s+Local dict shard size \(rank 0\):\s+(\d+)")
RE_BLOOM_ENABLED = re.compile(r"^\[\*\]\s+Bloom filter:\s+(enabled|disabled)")
RE_FORCE_MPI = re.compile(r"^\[\*\]\s+Force MPI:\s+(enabled|disabled)")
RE_DICT_PATH = re.compile(r"^\[\*\]\s+Using dictionary:\s+(.+)$")
RE_WORDS_PATH = re.compile(r"^\[\*\]\s+Using word list:\s+(.+)$")
RE_BLOOM_CONFIG = re.compile(
    r"^\[MPI\]\s+Bloom filter enabled with\s+(\d+)\s+bits\s+and\s+(\d+)\s+hashes"
)
RE_WORD_LIST_SIZE = re.compile(r"^\[MPI\]\s+Word list size:\s+(\d+)")
RE_RANK0_MISSPELLED = re.compile(r"^\[MPI\]\s+Rank 0 owns\s+(\d+)\s+misspelled words")
RE_TOTAL_MISSPELLED = re.compile(r"^\[MPI\]\s+Total misspelled words:\s+(\d+)")
RE_BLOOM_SUMMARY = re.compile(
    r"^\[MPI\]\[bloom\]\s+checked=(\d+)\s+rejected=(\d+)\s+\(([0-9.]+)%\)\s+forwarded=(\d+)\s+confirmed=(\d+)\s+\(([0-9.]+)%\)"
)
RE_OUTPUT_PATH = re.compile(r"^\[\*\]\s+Output:\s+(.+)$")
RE_WALL_TIME = re.compile(
    r"^\[\*\]\s+Wall time:\s+(\d+)\s+mins\s+(\d+)\s+secs\s+(\d+)\s+ms;\s+(\d+)"
)


# Helpers
def ensure_dirs():
    """Create results/, results/output/, results/log/."""
    RESULT_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    LOG_DIR.mkdir(parents=True, exist_ok=True)


def run_job(parallel: int, pair_name: str, dictfile: str, wordsfile: str):
    """Run one srun, save stdout/stderr into results/output/ and results/log/."""
    dict_path = f"{DATA_DIR}/{dictfile}"
    words_path = f"{DATA_DIR}/{wordsfile}"

    output_file = OUTPUT_DIR / f"{pair_name}-{parallel}.output"
    log_file = LOG_DIR / f"{pair_name}-{parallel}.log"

    cmd = [
        "srun",
        "-n",
        str(parallel),
        "--cpus-per-task=1",
        "build/spellcheck",
        dict_path,
        words_path,
    ]

    proc = subprocess.run(cmd, text=True, capture_output=True)
    output_file.write_text(proc.stdout, encoding="utf-8", errors="ignore")
    log_file.write_text(proc.stderr, encoding="utf-8", errors="ignore")

    # Merge both streams for parsing
    combined = proc.stdout.splitlines() + proc.stderr.splitlines()
    return combined


def parse_metrics(lines):
    m = {}
    for line in lines:
        s = line.strip()

        mt = RE_TIMING.match(s)
        if mt:
            key = mt.group(1)
            m[f"{key}_avg_s"] = mt.group(2)
            m[f"{key}_min_s"] = mt.group(3)
            m[f"{key}_max_s"] = mt.group(4)
            continue

        if ms := RE_START_RANKS.match(s):
            m["ranks_reported"] = ms.group(1)
        elif ml := RE_LOCAL_SHARD.match(s):
            m["local_dict_shard_size_rank0"] = ml.group(1)
        elif mbe := RE_BLOOM_ENABLED.match(s):
            m["bloom_enabled"] = "1" if mbe.group(1) == "enabled" else "0"
        elif mfm := RE_FORCE_MPI.match(s):
            m["force_mpi"] = "1" if mfm.group(1) == "enabled" else "0"
        elif mdp := RE_DICT_PATH.match(s):
            m["dict_path"] = mdp.group(1)
        elif mwp := RE_WORDS_PATH.match(s):
            m["words_path"] = mwp.group(1)
        elif mbc := RE_BLOOM_CONFIG.match(s):
            m["bloom_bits"] = mbc.group(1)
            m["bloom_hashes"] = mbc.group(2)
        elif mwls := RE_WORD_LIST_SIZE.match(s):
            m["word_list_size"] = mwls.group(1)
        elif mr0 := RE_RANK0_MISSPELLED.match(s):
            m["rank0_misspelled"] = mr0.group(1)
        elif mtm := RE_TOTAL_MISSPELLED.match(s):
            m["total_misspelled"] = mtm.group(1)
        elif mbs := RE_BLOOM_SUMMARY.match(s):
            m["bloom_checked"] = mbs.group(1)
            m["bloom_rejected"] = mbs.group(2)
            m["bloom_rejected_pct"] = mbs.group(3)
            m["bloom_forwarded"] = mbs.group(4)
            m["bloom_confirmed"] = mbs.group(5)
            m["bloom_confirmed_pct"] = mbs.group(6)
        elif mop := RE_OUTPUT_PATH.match(s):
            m["output_path"] = mop.group(1)
        elif mwt := RE_WALL_TIME.match(s):
            m["wall_time_min"] = mwt.group(1)
            m["wall_time_sec"] = mwt.group(2)
            m["wall_time_ms_fragment"] = mwt.group(3)
            m["wall_time_ms_total"] = mwt.group(4)

    return m


def deduce_all_headers(records):
    keys = set()
    for r in records:
        keys.update(r.keys())
    keys.discard("name")
    timing_keys = sorted(
        [k for k in keys if k.endswith(("_avg_s", "_min_s", "_max_s"))]
    )
    other_keys = sorted([k for k in keys if k not in timing_keys])
    return ["name"] + other_keys + timing_keys


def append_to_csv(rows, header):
    write_header = not CSV_PATH.exists()
    with open(CSV_PATH, "a", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=header)
        if write_header:
            w.writeheader()
        for r in rows:
            row = {k: r.get(k, "") for k in header}
            w.writerow(row)


def compute_md5sum_via_md5sum(path: Path) -> str:
    try:
        out = subprocess.check_output(["md5sum", str(path)], text=True)
        # md5sum output: "<hash>  <filename>"
        return out.split()[0].strip()
    except Exception:
        # Fallback: hashlib
        import hashlib

        h = hashlib.md5()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        return h.hexdigest()


def is_blank_or_zeros(s: str) -> bool:
    t = (s or "").strip().lower()
    return (t == "") or (t == "0" * 32)


def verify_misspelled_hash(expected_hash: str):
    if not MISSPELLED_PATH.exists():
        raise FileNotFoundError(f"Expected output file not found: {MISSPELLED_PATH}")

    actual = compute_md5sum_via_md5sum(MISSPELLED_PATH)

    if is_blank_or_zeros(expected_hash):
        print(f"[hash] Expected hash not set (blank/zeros). Actual MD5: {actual}")
        return

    if actual != expected_hash.lower():
        raise RuntimeError(
            f"MD5 mismatch for {MISSPELLED_PATH.name}: expected {expected_hash}, got {actual}"
        )
    else:
        print(f"[hash] OK: {actual}")


# Main
def main():
    parser = argparse.ArgumentParser(description="Batch srun runner & metrics parser")
    parser.add_argument(
        "--pmin", type=int, default=1, help="minimum parallel (inclusive)"
    )
    parser.add_argument(
        "--pmax", type=int, default=64, help="maximum parallel (inclusive)"
    )
    parser.add_argument("--dry-run", action="store_true", help="print commands only")
    args = parser.parse_args()

    ensure_dirs()

    all_rows = []
    for pair in DICT_WORDS_PAIRS:
        pair_name = pair["name"]
        dictf = pair["dict"]
        wordsf = pair["words"]
        expected_hash = pair.get("hash", "")

        for p in range(args.pmin, args.pmax + 1):
            run_name = f"{pair_name}-{p}"

            if args.dry_run:
                cmd = f"srun -n {p} --cpus-per-task=1 build/spellcheck {DATA_DIR}/{dictf} {DATA_DIR}/{wordsf}"
                print(cmd)
                continue

            print(
                f"[{datetime.now().isoformat(timespec='seconds')}] Running {run_name} ..."
            )
            combined = run_job(p, pair_name, dictf, wordsf)

            verify_misspelled_hash(expected_hash)

            metrics = parse_metrics(combined)
            metrics["name"] = run_name
            metrics["parallel"] = str(p)
            metrics["pair"] = pair_name
            metrics["dictfile"] = dictf
            metrics["wordsfile"] = wordsf

            all_rows.append(metrics)
            header = deduce_all_headers(all_rows)
            append_to_csv([metrics], header)

    print(f"All runs finished. Summary CSV: {CSV_PATH.resolve()}")


if __name__ == "__main__":
    main()
