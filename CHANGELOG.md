# Changelog

All notable changes to this project are documented here.
Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/). The version lives in the [`VERSION`](VERSION) file.

## [0.1.1-beta] - 2026-11-22

### Fixed
- Fixed inverted PixArt IR camera X/Y axes (`WIIMOTE_IR_INVERT_X=0`, `WIIMOTE_IR_INVERT_Y=1`) so pointer movement matches physical aiming on real hardware.
- Rejected negative-delta 2-point calibrations and ignored invalid/negative calibration coefficients loaded from Flash.
- Updated `docs/math_tracking.tex` coordinate-centering equations to match the corrected sensor axes.

### Changed
- Updated default button bindings: `A` short press = Left Click, `A` long press (2 s) = Right Click; `+`/`-` short press = Volume Up/Down, `+`/`-` long press (1 s, separately) = Mute/Unmute; `Home` = Super/GUI key; `1`/`2` = Scroll Up/Down; `D-Pad Up/Down` = unassigned.
- Increased default `SCROLL_INTERVAL_MS` from `40 ms` to `120 ms` for smoother, slower scrolling.

## [0.1.0-beta] - 2026-11-20

First public pre-release. Not yet validated on real hardware.

### Added
- Wii Remote (`RVL-CNT-01` / `-TR`) support over Bluetooth Classic (BTstack L2CAP HID host) with persistent pairing.
- Composite USB HID device: absolute pointer, keyboard, and consumer control (media keys).
- 4-LED IR diamond tracking with 4 → 3 → 2 LED fallback, accelerometer roll compensation, and 1-Euro smoothing.
- On-controller 2-point calibration stored in Flash, with LED/rumble feedback for each step, success, error, and cancel.
- WebHID configurator (`docs/configurator.html`): button remapping, filter and scroll tuning, layout profiles
  (Mouse / Presentations / Media center), JSON import/export, automatic reconnect, first-time setup guide,
  and firmware-version warning. Deployable to GitHub Pages.
- Flash writes requested over WebHID are deferred to the main loop and their result (CRC, validation, or
  Flash failure) is reported back to the configurator.
- Host unit tests, CI (tests with ASan/UBSan, firmware builds for `pico2_w` and `pico_w`), and a tag-driven
  release workflow.
- Documentation, including per-distro setup and troubleshooting.

### Notes
- Firmware binaries link BTstack (non-commercial license) and cyw43-driver (Raspberry Pi hardware only); see `THIRD_PARTY_LICENSES.md`.
- The USB VID/PID (`cafe:4010`) is a development placeholder, intended only for personal/non-commercial use.
