#!/usr/bin/env python3
"""
FuseCheck Database Generator
Fetches data from FuseNCA repository and generates fusecheck_db.txt

Usage:
    python scripts/update_db.py              # Output to stdout
    python scripts/update_db.py -o db.txt    # Output to file
"""

import json
import urllib.request
import urllib.error
import sys
from typing import List, Dict, Tuple
from dataclasses import dataclass
from itertools import groupby


# URLs
FUSENCA_JSON_URL = "https://raw.githubusercontent.com/sthetix/FuseNCA/master/fuses.json"
OUTPUT_PATH = "fusecheck_db.txt"


@dataclass
class Version:
    """Version tuple for sorting"""
    major: int
    minor: int
    patch: int

    @classmethod
    def from_string(cls, s: str) -> 'Version':
        parts = s.split('.')
        return cls(
            major=int(parts[0]),
            minor=int(parts[1]) if len(parts) > 1 else 0,
            patch=int(parts[2]) if len(parts) > 2 else 0
        )

    def __lt__(self, other: 'Version') -> bool:
        return (self.major, self.minor, self.patch) < (other.major, other.minor, other.patch)

    def __str__(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"


@dataclass
class FuseEntry:
    version: str
    version_obj: Version
    fuses: int
    nca: str


def fetch_fusenca_data(url: str) -> List[Dict]:
    """Fetch and parse FuseNCA fuses.json"""
    print(f"Fetching {url}...", file=sys.stderr)
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            data = json.loads(response.read().decode())
            print(f"Last updated: {data.get('last_updated', 'unknown')}", file=sys.stderr)
            return data.get('data', [])
    except urllib.error.URLError as e:
        print(f"Error fetching data: {e}", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error parsing JSON: {e}", file=sys.stderr)
        sys.exit(1)


def group_fuse_ranges(entries: List[FuseEntry]) -> List[Tuple[str, int]]:
    """
    Group consecutive versions with the same fuse count into ranges.
    Returns list of (version_range, fuse_count) tuples.
    """
    if not entries:
        return []

    result = []
    entries_sorted = sorted(entries, key=lambda e: (e.version_obj, e.fuses))

    i = 0
    while i < len(entries_sorted):
        current_fuse = entries_sorted[i].fuses
        start_version = entries_sorted[i].version_obj
        end_version = start_version
        j = i + 1

        # Find consecutive versions with same fuse count
        while j < len(entries_sorted):
            next_entry = entries_sorted[j]
            if next_entry.fuses != current_fuse:
                break

            # Check if versions are consecutive
            curr_ver = entries_sorted[j - 1].version_obj
            next_ver = next_entry.version_obj

            # Simple sequential check (minor versions usually)
            if (next_ver.major == curr_ver.major and
                next_ver.minor == curr_ver.minor + 1 and
                next_ver.patch == 0):
                end_version = next_ver
                j += 1
            elif (next_ver.major == curr_ver.major and
                  next_ver.minor == curr_ver.minor and
                  next_ver.patch == curr_ver.patch + 1):
                end_version = next_ver
                j += 1
            else:
                break

        # Build version range string
        if i == j - 1:
            version_range = str(start_version)
        else:
            version_range = f"{start_version}-{end_version}"

        result.append((version_range, current_fuse))
        i = j

    return result


def generate_db(entries: List[FuseEntry]) -> str:
    """Generate fusecheck_db.txt content"""
    # Group fuse ranges
    fuse_ranges = group_fuse_ranges(entries)

    # Sort NCA entries by version descending
    nca_entries = sorted(entries, key=lambda e: e.version_obj, reverse=True)

    lines = [
        "# Fusecheck Database Configuration v2",
        "# Copy to sd:/config/fusecheck/fusecheck_db.txt to override built-in databases",
        "# Lines starting with # are comments and will be ignored",
        "# Auto-generated from https://github.com/sthetix/FuseNCA",
        "",
        "# ===== FUSE COUNT DATABASE =====",
        "# Format: [FUSE] <version_range> <prod_fuses>",
        "# Source: https://switchbrew.org/wiki/Fuses (Anti Downgrade section)",
        "",
    ]

    for version_range, fuses in fuse_ranges:
        lines.append(f"[FUSE] {version_range} {fuses}")

    lines.extend([
        "",
        "# ===== NCA DATABASE =====",
        "# Format: [NCA] <version> <nca_filename>",
        "# Source: SystemVersion Title ID 0100000000000809",
        "",
    ])

    for entry in nca_entries:
        lines.append(f"[NCA] {entry.version} {entry.nca}")

    return "\n".join(lines) + "\n"


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Generate FuseCheck database from FuseNCA")
    parser.add_argument("-o", "--output", default=OUTPUT_PATH, help="Output file path")
    parser.add_argument("-u", "--url", default=FUSENCA_JSON_URL, help="FuseNCA fuses.json URL")
    parser.add_argument("--stdout", action="store_true", help="Output to stdout instead of file")
    args = parser.parse_args()

    # Fetch data
    raw_data = fetch_fusenca_data(args.url)

    # Parse into FuseEntry objects
    entries = []
    for item in raw_data:
        entries.append(FuseEntry(
            version=item["version"],
            version_obj=Version.from_string(item["version"]),
            fuses=item["fuses_production"],
            nca=item["system_title_nca"]
        ))

    print(f"Loaded {len(entries)} firmware versions", file=sys.stderr)

    # Generate database
    db_content = generate_db(entries)

    # Output
    if args.stdout:
        print(db_content, end="")
    else:
        with open(args.output, "w", newline="\n") as f:
            f.write(db_content)
        print(f"Database written to {args.output}", file=sys.stderr)
        print(f"  {len([l for l in db_content.split('\\n') if l.startswith('[FUSE]')])} fuse entries", file=sys.stderr)
        print(f"  {len([l for l in db_content.split('\\n') if l.startswith('[NCA]')])} NCA entries", file=sys.stderr)


if __name__ == "__main__":
    main()
