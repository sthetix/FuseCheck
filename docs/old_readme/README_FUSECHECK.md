# Fuse Compatibility Checker

A Nintendo Switch payload that checks your console's burnt fuse count against the installed firmware version to determine if Official Firmware (OFW) will boot.

## Features

- **Horizontal UI Layout** - Inspired by TegraExplorer for better readability
- **Silent Key Derivation** - Keys are derived in RAM only, no files are saved to SD card
- **Firmware Auto-Detection** - Automatically detects firmware version from BOOT0 PKG1 timestamp
- **Fuse Compatibility Check** - Compares burnt fuses against firmware requirements
- **Clear Visual Feedback** - Color-coded status (green=safe, red=critical, yellow=warning)

## How It Works

1. **Derives encryption keys silently in RAM** - No key files are written to SD card
2. **Reads burnt fuse count** from hardware fuse registers (ODM7)
3. **Detects firmware version** from BOOT0 partition PKG1 timestamp
4. **Calculates required fuses** based on firmware version
5. **Displays horizontal status screen** showing compatibility

## Status Indicators

### CRITICAL ERROR (Red)
- **Condition**: Burnt fuses < Required fuses
- **Result**: OFW WILL NOT BOOT (black screen)
- **What will work**: Semi-stock, sysMMC CFW, emuMMC CFW
- **What won't work**: Stock OFW

### SAFE (OVERBURNT) (Green)
- **Condition**: Burnt fuses > Required fuses
- **Result**: OFW WILL BOOT NORMALLY
- **Note**: Cannot downgrade below FW version matching burnt fuses

### PERFECT MATCH (Green)
- **Condition**: Burnt fuses = Required fuses
- **Result**: OFW WILL BOOT NORMALLY
- **Note**: System is in ideal state

## Building

### Option 1: Replace main.c (recommended)
```bash
cd D:/Coding/fusecheck
cp source/main.c source/main.c.lockpick_original
cp source/main_fusecheck.c source/main.c
make
```

### Option 2: Modify Makefile
Edit `Makefile` to use `main_fusecheck.c` instead of `main.c`

## Usage

1. Copy `fusecheck.bin` to `sd:/bootloader/payloads/`
2. Boot into Hekate
3. Launch fusecheck payload
4. View results and press any button to reboot

## Technical Details

### Fuse-to-Firmware Mapping
Based on [switchbrew.org/wiki/Fuses](https://switchbrew.org/wiki/Fuses)

| Firmware Range | Burnt Fuses Required |
|----------------|---------------------|
| 1.0.0 | 1 |
| 2.0.0-2.3.0 | 2 |
| 3.0.0 | 3 |
| 3.0.1-3.0.2 | 4 |
| 4.0.0-4.1.0 | 5 |
| 5.0.0-5.1.0 | 6 |
| 6.0.0-6.1.0 | 7 |
| 6.2.0 | 8 |
| 7.0.0-8.0.1 | 9 |
| 8.1.0 | 10 |
| 9.0.0-9.0.1 | 11 |
| 9.1.0-9.2.0 | 12 |
| 10.0.0-10.2.0 | 13 |
| 11.0.0-12.0.1 | 14 |
| 12.0.2-13.1.0 | 15 |
| 13.2.1-14.1.2 | 16 |
| 15.0.0-15.0.1 | 17 |
| 16.0.0-16.1.0 | 18 |
| 17.0.0-18.1.0 | 19 |
| 19.0.0-19.0.1 | 20 |
| 20.0.0-20.5.0 | 21 |
| 21.0.0-21.0.1 | 22 |

### Key Derivation
- Uses `derive_bis_keys_silently()` from Lockpick_RCM
- Works on both Erista (TSEC keygen) and Mariko (Mariko KEK)
- All keys stay in RAM, no SD card writes

### Firmware Detection
- Reads PKG1 timestamp from BOOT0 at offset 0x100000
- Uses timestamp database from Hekate for accurate version detection
- Falls back to example version if detection fails

## Comparison to Original Projects

### vs Lockpick_RCM
- **Same**: Key derivation engine (derive_bis_keys_silently)
- **Different**: No key dumping to files, focused on fuse checking only

### vs fuse-check
- **Same**: Fuse compatibility logic
- **Different**: Completed implementation with horizontal UI and automatic firmware detection

### vs TegraExplorer
- **Same**: Horizontal menu UI style
- **Different**: Single-purpose tool for fuse checking

## Credits

- **CTCaer** - Hekate, PKG1 timestamp database
- **shchmue** - Lockpick_RCM, key derivation
- **TegraExplorer** - Horizontal UI inspiration
- **switchbrew.org** - Fuse documentation

## License

GPL-2.0 (same as parent projects)
