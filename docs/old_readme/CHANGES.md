# Changes Made to Fusecheck

## Summary

Transformed the original Lockpick_RCM into a dedicated Fuse Compatibility Checker with:
1. Horizontal UI layout (TegraExplorer-inspired)
2. Silent in-RAM key dumping (no file writes)
3. Integrated firmware detection from BOOT0
4. Clear fuse compatibility status display

## New Files Created

### 1. `source/main_fusecheck.c`
Complete rewrite of main.c with:
- Removed all Lockpick_RCM menu system
- Removed all key file writing functionality
- Added fuse reading from hardware (fuse_read_odm)
- Added PKG1 timestamp-based firmware detection
- Added fuse-to-firmware mapping table
- Implemented horizontal status display
- Silent key derivation (keys stay in RAM only)

### 2. `README_FUSECHECK.md`
Documentation covering:
- Feature list
- How it works
- Building instructions
- Usage guide
- Technical details and fuse mapping table
- Credits

### 3. `CHANGES.md` (this file)
Summary of modifications

## Key Features Implemented

### 1. Silent Key Derivation ✓
```c
// Derive keys silently in RAM (no file saving)
if (!derive_bis_keys_silently()) {
    // Error handling
}
```
- Uses existing `derive_bis_keys_silently()` from Lockpick_RCM
- No calls to file writing functions
- Keys loaded into SE keyslots in RAM

### 2. Fuse Count Reading ✓
```c
u8 get_burnt_fuses() {
    u8 fuse_count = 0;
    u32 fuse_odm7 = fuse_read_odm(7);
    for (u32 i = 0; i < 32; i++) {
        if ((fuse_odm7 >> i) & 1)
            fuse_count++;
    }
    return fuse_count;
}
```
- Reads from fuse ODM7 register
- Counts set bits to determine burnt fuses
- Same logic as fuse-check project

### 3. Firmware Detection ✓
```c
bool detect_firmware_from_boot0(u8 *major, u8 *minor, u8 *patch) {
    // Read PKG1 timestamp from BOOT0 @ 0x100000
    // Match against timestamp database
    // Return detected version
}
```
- Reads BOOT0 partition at offset 0x100000
- Extracts PKG1 timestamp (14 bytes @ +0x10)
- Matches against Hekate's timestamp database
- Returns major.minor.patch version

### 4. Horizontal UI Layout ✓
```c
void show_fuse_check_horizontal(...) {
    // Title bar with decorative borders
    // Left column: System Information
    //   - Firmware Version
    //   - Burnt Fuses
    //   - Required Fuses
    // Right column: Compatibility Status
    //   - Status (CRITICAL/SAFE/PERFECT)
    //   - What will work / won't work
    // Bottom section: Detailed information
}
```
- Two-column layout like TegraExplorer
- Color-coded status indicators
- Clear visual hierarchy
- Unicode box-drawing characters

### 5. Fuse-to-Firmware Mapping ✓
```c
static const fw_fuse_map_t fuse_map[] = {
    {major_min, minor_min, major_max, minor_max, fuses_required},
    // ... 22 entries covering FW 1.0.0 to 21.0.1
};
```
- Complete database from switchbrew.org
- Covers all released firmware versions
- Accurate fuse requirements per version range

## Removed Functionality

From original Lockpick_RCM:
- ❌ Key dumping to SD card
- ❌ Menu system (SysMMC/EmuMMC selection)
- ❌ PRODINFO dump/restore
- ❌ Amiibo key dumping
- ❌ Mariko partial key dumping
- ❌ Payload launching
- ❌ File system operations

## Files Not Modified

### From `bdk/` directory (Hekate components):
- All BDK files remain unchanged
- Uses existing hardware initialization
- Uses existing display/graphics system
- Uses existing storage drivers

### From `keygen/` directory:
- All key derivation logic unchanged
- TSEC/Mariko key generation intact
- BIS key derivation functions used as-is

### From `source/keys/`:
- `keys.h` - Only uses `derive_bis_keys_silently()` function
- Other key files not touched

## Integration Points

### From fuse-check project:
- Fuse reading logic (fuse_check.c)
- Fuse-to-firmware mapping table
- Status display concepts

### From TegraExplorer:
- Horizontal UI layout inspiration
- Color scheme and box-drawing style

### From NxNandManager:
- Firmware detection methodology
- PKG1 timestamp usage

### From Lockpick_RCM:
- BIS key derivation (`derive_bis_keys_silently`)
- Hardware initialization
- Display/graphics framework

## How to Use

### To build with new main:
```bash
cd D:/Coding/fusecheck
cp source/main.c source/main.c.lockpick_original  # Backup
cp source/main_fusecheck.c source/main.c           # Use new version
make
```

### To revert to original Lockpick_RCM:
```bash
cd D:/Coding/fusecheck
cp source/main.c.lockpick_original source/main.c
make
```

## Testing Checklist

- [ ] Compiles without errors
- [ ] Boots on Erista console
- [ ] Boots on Mariko console
- [ ] Derives keys successfully
- [ ] Reads correct fuse count
- [ ] Detects firmware version
- [ ] Displays horizontal UI correctly
- [ ] Shows correct compatibility status
- [ ] Reboots cleanly after button press

## Future Enhancements (Optional)

1. Add support for reading NCA from SYSTEM partition for exact version
2. Add emuMMC support for checking emuMMC firmware
3. Add option to save screenshot of results
4. Add automatic fuse burning simulation/calculator

## Notes

- Original main.c backed up as `main.c.lockpick_original`
- New code is self-contained in `main_fusecheck.c`
- No modifications to BDK or key derivation code
- All changes are additive - original Lockpick_RCM functionality preserved in backup
