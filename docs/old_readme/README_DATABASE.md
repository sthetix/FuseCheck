# Fusecheck Database Configuration

## Overview

Fusecheck now uses an external, easy-to-update database file instead of hardcoded data. This allows you to update the NCA database and fuse count information without recompiling the tool.

## Database Location

Copy the database file to your SD card at:
```
sd:/config/fusecheck_db.txt
```

## Database Format

The database file uses a simple text format with two types of entries:

### 1. Fuse Count Entries

Format: `[FUSE] <version_range> <prod_fuses> <dev_fuses>`

Example:
```
[FUSE] 21.0.0-21.0.1 22 1
[FUSE] 20.0.0-20.5.0 21 1
[FUSE] 19.0.0-19.0.1 20 1
```

- **version_range**: Firmware version or range (e.g., "21.0.0" or "20.0.0-20.5.0")
- **prod_fuses**: Number of burnt fuses for production units
- **dev_fuses**: Number of burnt fuses for development units

Source: [switchbrew.org/wiki/Fuses](https://switchbrew.org/wiki/Fuses) (Anti Downgrade section)

### 2. NCA Entries

Format: `[NCA] <version> <nca_filename>`

Example:
```
[NCA] 21.0.1 e7273dd5b560d0ba282fc64206fecb56.nca
[NCA] 21.0.0 4b0130c8b9d2174a6574f6247655acc0.nca
[NCA] 20.5.0 23ce01f1fc55e55a783162d456e5ca58.nca
```

- **version**: Exact firmware version (e.g., "21.0.1")
- **nca_filename**: SystemVersion NCA filename (Title ID 0100000000000809)

## Comments

Lines starting with `#` are treated as comments and ignored:
```
# This is a comment
# You can add notes about updates here
```

## Sample Database File

A complete sample database file (`fusecheck_db.txt`) is included in the repository. Simply copy it to `sd:/config/fusecheck_db.txt` on your Switch's SD card.

## Updating the Database

When new Switch firmware is released:

1. Update the `[FUSE]` section with new fuse count info from switchbrew
2. Update the `[NCA]` section with the new SystemVersion NCA filename
3. Copy the updated file to `sd:/config/fusecheck_db.txt`
4. No recompilation needed!

## Controls

### Main Page (Fuse Check Results)
- **VOL +**: Go to Fuse List
- **VOL -**: Return to Hekate
- **Power**: Shutdown
- **VOL + & VOL -**: Take screenshot

### Fuse List Page (with Scrolling Support)
The fuse list now supports smooth scrolling to handle unlimited entries!

- **VOL +**: Scroll down (stops at bottom)
- **VOL -**: Scroll up (stops at top)
- **Power**: Return to main page
- **VOL + & VOL -**: Take screenshot

Scrolling behavior is simple and predictable - it just scrolls and stops at the edges. Use Power button to exit back to the main page.

## Changes from Previous Versions

- ✅ Removed hardcoded NCA database
- ✅ Removed hardcoded fuse count database
- ✅ Removed hardcoded PKG1 timestamp database
- ✅ Unified configuration in single text file
- ✅ Easy to update without recompiling
- ✅ **NEW**: Scrolling support for fuse list (handles unlimited entries!)
- ✅ **NEW**: Dynamic footer showing context-aware controls
- ✅ **NEW**: Scroll indicator showing position in list [start-end/total]
