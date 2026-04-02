## v1.0.2

### Bug Fixes
- Fixed firmware version incorrectly displaying as `1.0.0` when NCA detection fails
- Firmware and Required Fuses now display `N/A` instead of a misleading version when detection fails

### Improvements
- Added new `STATUS: FIRMWARE NOT DETECTED` state when the database is loaded but the firmware NCA hash is not found
- When firmware is not detected, users are now directed to check for an updated build at `github.com/sthetix/FuseCheck`
