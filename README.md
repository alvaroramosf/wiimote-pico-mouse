[![CI](https://img.shields.io/github/actions/workflow/status/alvaroramosf/wiimote-pico-mouse/ci.yml?branch=main&label=CI)](https://github.com/alvaroramosf/wiimote-pico-mouse/actions/workflows/ci.yml)
[![Latest release](https://img.shields.io/github/v/release/alvaroramosf/wiimote-pico-mouse?include_prereleases)](https://github.com/alvaroramosf/wiimote-pico-mouse/releases/latest)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)
[![WebHID Configurator](https://img.shields.io/badge/WebHID-Configurator-brightgreen)](https://alvaroramosf.github.io/wiimote-pico-mouse/)

# wiimote-pico-mouse

Turn an **original Nintendo Wii Remote (`RVL-CNT-01` / `RVL-CNT-01-TR`)** into a USB **lightgun-style absolute pointer, keyboard, and media remote** for your **HTPC / couch PC**. It is an open-source **DolphinBar alternative**: complete C firmware (`pico-sdk`) for the **Raspberry Pi Pico 2 W** (RP2350 + CYW43439; also compatible with **Pico W** RP2040) that connects to the Wiimote over **Bluetooth Classic (BTstack L2CAP HID Host)** and presents a **composite USB HID absolute pointer + keyboard + multimedia control** device.

Because it is a standard USB HID device, it is 100% plug-and-play on **Linux (Wayland/X11), Windows, macOS, and SteamOS**, with no host drivers or background software. It has been tested primarily on Fedora Linux.

---

## Quick Start (Pre-built Firmware)

No toolchain needed. You only need the hardware from the [BOM](#bill-of-materials-bom) wired as described in [`docs/wiring_diagram.md`](docs/wiring_diagram.md).

1. Download the `.uf2` for your board from [GitHub Releases](https://github.com/alvaroramosf/wiimote-pico-mouse/releases/latest):
   - **Pico 2 W:** `wiimote-pico-mouse-<tag>-pico2_w.uf2`
   - **Pico W:** `wiimote-pico-mouse-<tag>-pico_w.uf2`
   - *(Optional)* verify it against `SHA256SUMS.txt` from the same release.
2. Hold the **`BOOTSEL`** button on the Pico while plugging it into USB. It mounts as a drive (`RP2350` or `RPI-RP2`).
3. Copy the `.uf2` file onto that drive. The Pico reboots automatically and enumerates as **`Wiimote IR Absolute Pointer + Media`**.
4. Pair the Wiimote: press the red **`SYNC`** button under the battery cover (permanent bonding) or hold **`1 + 2`**. **LED 1** lights up solid once connected, and the IR LEDs switch on.
5. Calibrate once by holding `+` and `-` for 3 s (see [Usage, Pairing, and Calibration](#usage-pairing-and-calibration)).

---

## Key Features

- **Zero Host Software or Drivers:** Composite USB HID device (TinyUSB) exposing an absolute pointer `0..32767` (Report ID 1), keyboard (Report ID 2), and Consumer Control media keys (Report ID 3) on Interface 0, plus a dedicated Vendor WebHID configurator interface (`0xFF00`, Report ID 4) on Interface 1.
- **Browser-Based WebHID Configurator (USB-Only):** Remap all 11 Wiimote buttons and tune 1-Euro filter smoothing & scroll repeat rate directly from Chromium/Chrome via [`docs/configurator.html`](docs/configurator.html), persisted in internal Flash with CRC-32 validation.
- **Native Bluetooth Classic (BTstack L2CAP):** Automatic pairing via `SYNC` or `1+2` for both original `RVL-CNT-01` and Wii Remote Plus (`RVL-CNT-01-TR`), with persistent link keys and auto-reconnect stored in Flash.
- **Robust 4-LED IR Diamond Tracking (920–940 nm):**
  - **4 LEDs visible:** Full 2D projective homography + accelerometer roll compensation.
  - **3 LEDs visible:** Missing 4th vertex reconstruction using live perspective asymmetry.
  - **2 LEDs visible:** 2D similarity transform using accelerometer roll and stored reference diagonals to reach TV corners at close distance or on large screens.
  - **1 / 0 LEDs visible:** Smooth cursor freeze at the last valid position while all buttons remain active.
- **1-Euro Adaptive Low-Pass Filter:** Eliminates hand tremor at 2.5 m+ when aiming or clicking without introducing perceptible lag during fast pointer sweeps.
- **Automatic Hardware Power Saving:** `GP15` switches the 4 IR LEDs on only while a Wiimote is connected via Bluetooth and turns them off automatically upon disconnect.
- **On-Controller Calibration:** Triggered by holding `+` and `-` for 3 s, using a 2-point target sequence stored in internal Flash with IEEE 802.3 CRC-32 validation.

---

## Bill of Materials (BOM)

| Qty | Component | Specification / Notes |
|---|---|---|
| 1 | **Raspberry Pi Pico 2 W** (or Pico W) | RP2350 + CYW43439 (`pico_w` supported via CMake) |
| 1 | Original Nintendo Wii Remote | `RVL-CNT-01` or `RVL-CNT-01-TR` (Wii MotionPlus Inside) |
| 4 | 920 nm – 940 nm IR LEDs | e.g. Vishay TSAL6200 / TSAL6400 (5 mm, $V_F \approx 1.4\text{ V}$) |
| 4 | **68 Ω** (or **100 Ω**) resistors (1/4 W) | One per LED in parallel (~50 mA/LED with 68 Ω, ~34 mA/LED with 100 Ω from 5 V VBUS) |
| 1 | NPN transistor **2N2222** or **BC337** | Or logic-level N-channel MOSFET (`AO3400`) |
| 1 | **470 Ω** resistor (1/4 W) | Base/gate resistor between `GP15` and the transistor |
| — | Thin copper wire / AWG 28–30 hookup wire | 4 independent 2-wire pairs (8 wires total) from the board to the TV bezel |

> **Estimated total cost:** approx. 10–12 € (excluding the Wiimote).

See [`docs/wiring_diagram.md`](docs/wiring_diagram.md) for the full schematic and resistor calculations.

---

## Diamond IR LED Placement Around the TV

Mount the 4 IR LEDs facing toward the seating area at the midpoint of each edge of the TV bezel (baseline target: 32" TV at 2.5 m, adaptable via calibration to any screen size and viewing distance):

- **Top:** center of top bezel edge.
- **Bottom:** center of bottom bezel edge.
- **Left:** center of left bezel edge.
- **Right:** center of right bezel edge.

Route an independent 2-wire pair (`+` / `-`) from each LED directly back to the board (**8 wires total**): each LED anode connects to `VBUS` (Pin 40) through its own `68 Ω` (or `100 Ω`) resistor on the board, and all 4 cathodes join at the transistor collector switched to `GND` by `GP15` (Pin 20).

---

## Usage, Pairing, and Calibration

### Bluetooth Pairing

- **Permanent bonding (`SYNC`):** Press the red `SYNC` button under the Wiimote battery cover. The Pico 2 W discovers it, completes binary PIN authentication automatically, and stores the link key in Flash. Afterwards, pressing **`A`** (or any button) on the Wiimote reconnects automatically in ~1–2 seconds.
- **Temporary pairing (`1 + 2`):** Hold `1 + 2` simultaneously while the Wiimote is off.

Once connected and initialized:
1. Wiimote **LED 1** lights up solid.
2. **GP15** goes HIGH (`3.3 V`) to power on the **4 IR LEDs** around the TV.
3. When the Wiimote powers off or disconnects, **GP15** immediately turns the IR LEDs off.

### 2-Point On-Controller Calibration (No PC Software)

1. Hold **`+` and `-` simultaneously for 3 seconds**. LED 1 starts blinking.
2. The Pico places the USB pointer at the top-left target (**10 %, 10 %**). Aim at the cursor and press **`A`** (at least 2 IR LEDs must be visible).
3. The Wiimote buzzes briefly and **LED 4** starts blinking. The pointer moves to the bottom-right target (**90 %, 90 %**). Aim and press **`A`**.
4. Calibration is saved to internal Flash (survives reboots and power loss).

Wiimote feedback during calibration:

| Pattern | Meaning |
|---|---|
| LED 1 blinking | Waiting for the top-left sample |
| Short buzz, then LED 4 blinking | Step 1 accepted, waiting for the bottom-right sample |
| All LEDs solid + buzz (≈1 s), then LED 1 solid | Calibration saved to Flash |
| LEDs 1+3 / 2+4 alternating + long buzz (≈1 s) | **Error:** degenerate sample (previous calibration restored) or Flash write failed |
| LED 1 + very short buzz | Calibration cancelled (hold `+` and `-` for 3 s again while calibrating) |

---

## Default Button & Media Mapping

Defaults are defined in [`src/config.h`](src/config.h) / [`src/flash_storage.c`](src/flash_storage.c) and can be customized live over USB using the WebHID Configurator:

| Wiimote Input | USB HID Action |
|---|---|
| **IR Aiming** | Absolute screen pointer (`0..32767` × `0..32767`) |
| **`B`** (trigger) | Left mouse button |
| **`A`** | Right mouse button *(confirms point during calibration)* |
| **D-Pad Up / Down** | Continuous vertical scroll wheel |
| **D-Pad Left / Right** | Mouse Back / Forward navigation buttons |
| **`+`** *(short press)* | Volume Increment |
| **`-`** *(short press)* | Volume Decrement |
| **`+` and `-`** *(hold 3 s)* | Enter / cancel calibration mode |
| **`Home`** | Multimedia `Play / Pause` |
| **`1`** | Middle mouse button |
| **`2`** | `Super` / Activities Overview on GNOME (`0xE3`) |

---

## WebHID Button & Sensitivity Configurator

No Wi-Fi or host daemon is needed. Interface `1` (`ITF_NUM_HID_CFG`, Vendor Usage Page `0xFF00`, Usage `0x01`, Report ID `4`) lets Chromium-based browsers (Chrome, Chromium, Edge) configure all 11 Wiimote buttons, 1-Euro smoothing (`min_cutoff`, `beta`), and `scroll_interval_ms` over USB and save them to the Pico's internal Flash. Firefox and Safari do not support WebHID.

### 1. Install the `udev` rule (Linux only, one-time setup)

> **Linux only.** This step is **not needed on Windows or macOS**; skip to [step 2](#2-open-the-configurator).

Copy [`docs/99-wiimote-pico.rules`](docs/99-wiimote-pico.rules) so your desktop session can access the Pico's `hidraw` vendor interface without `root`:

```bash
sudo cp docs/99-wiimote-pico.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

*(If the Pico was already plugged in before adding the rule, unplug and replug its USB cable once.)*

The same commands work on Fedora, Debian/Ubuntu, and Arch (anything using `systemd-udev`). Notes for other setups:

- **No `uaccess` support / minimal desktops:** replace `TAG+="uaccess"` with `GROUP="plugdev"` (Debian/Ubuntu) or `GROUP="input"`, and add your user with `sudo usermod -aG plugdev $USER` (re-login required).
- **Verify the rule applied:** `ls -l /dev/hidraw*` should show your user (via ACL) or the group with `rw` on the Pico's vendor interface; `udevadm info -a /dev/hidrawN | grep -E 'idVendor|idProduct'` should print `cafe` / `4010`.
- **Flatpak/Snap browsers:** WebHID access may be sandboxed; use a native Chromium/Chrome package if the device does not appear in the chooser.

### 2. Open the configurator

**Easiest (hosted):** open <https://alvaroramosf.github.io/wiimote-pico-mouse/> in Chromium/Chrome. No local server needed. The page also contains a first-time setup guide with a copy-paste command for the Linux `udev` rule. If you fork the repo, enable Pages for your fork (Settings → Pages → Source: *GitHub Actions*); the [`pages.yml`](.github/workflows/pages.yml) workflow publishes it.

**Local alternative:** serve `docs/configurator.html` yourself (see below).

WebHID requires a secure context (`http://localhost` or `https://`), not `file://`. To run it locally, start Python's built-in HTTP server from the repository root:

```bash
python3 -m http.server 8000
```

Then open **`http://localhost:8000/docs/configurator.html`** in **Chromium** or **Google Chrome**:

1. Click **Connect Pico (WebHID)** and select **`Wiimote IR Absolute Pointer + Media`**. On later visits the page reconnects automatically.
2. Adjust any button binding (Mouse Button, Scroll Wheel, Keyboard Key, or Consumer Media) and/or tune `min_cutoff`, `beta`, and `scroll_interval_ms`.
3. Optionally load a ready-made layout (*Mouse*, *Presentations*, *Media center*), or **Export / Import JSON** to back up and share profiles.
4. Click **Save to Pico Flash** to apply the settings immediately and persist them across reboots, or **Reset Defaults (Pico Flash)** to restore factory defaults. The page waits for the Pico's confirmation and reports errors (bad CRC, out-of-range values, or a failed Flash write) instead of assuming success.

---

## Building from Source

### 1. Install toolchain dependencies

**Fedora:**

```bash
sudo dnf install gcc g++ cmake make git python3 \
  arm-none-eabi-gcc-cs arm-none-eabi-gcc-cs-c++ \
  arm-none-eabi-binutils arm-none-eabi-newlib
```

**Debian / Ubuntu:**

```bash
sudo apt install build-essential cmake git python3 \
  gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
```

**Arch Linux:**

```bash
sudo pacman -S base-devel cmake git python \
  arm-none-eabi-gcc arm-none-eabi-newlib
```

### 2. Clone `pico-sdk` (with TinyUSB and BTstack submodules)

```bash
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH="$(pwd)"
cd ..
```

### 3. Build for **Raspberry Pi Pico 2 W** (default)

```bash
cd wiimote-pico-mouse
mkdir -p build && cd build
cmake .. -DPICO_SDK_PATH="$PICO_SDK_PATH"
make -j"$(nproc)"
```

*(Optional) Build for **Raspberry Pi Pico W (RP2040)**:*

```bash
mkdir -p build-picow && cd build-picow
cmake .. -DPICO_SDK_PATH="$PICO_SDK_PATH" -DPICO_BOARD=pico_w
make -j"$(nproc)"
```

### 4. Flash the firmware

1. Hold the **BOOTSEL** button on the Pico 2 W while plugging it into USB.
2. Copy the generated `.uf2` image to the mounted mass-storage drive (`RP2350` or `RPI-RP2`):

```bash
cp build/wiimote_pico_mouse.uf2 /run/media/$USER/RP2350/   # Fedora / Arch
cp build/wiimote_pico_mouse.uf2 /media/$USER/RP2350/       # Debian / Ubuntu
```

The Pico 2 W will reboot automatically and enumerate as **`Wiimote IR Absolute Pointer + Media`**.

---

## Repository Structure

```text
wiimote-pico-mouse/
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── LICENSE
├── README.md
├── docs/
│   ├── 99-wiimote-pico.rules  # Linux udev rule for WebHID hidraw access
│   ├── configurator.html      # Single-file WebHID keymap & 1-Euro tuner UI
│   ├── wiring_diagram.md      # Circuit schematic, pinout, and resistor math
│   ├── math_tracking.tex      # Homography, 4->3->2 LED fallback, and 1-Euro filter (LaTeX)
│   └── math_tracking.pdf      # Compiled PDF of math_tracking.tex
├── src/
│   ├── main.c                 # Main 100 Hz loop, calibration, and HID pipeline
│   ├── config.h               # GPIO pins, 1-Euro tuning, IR sensitivity, keymap schema
│   ├── btstack_config.h       # BTstack Classic (BR/EDR) configuration
│   ├── tusb_config.h          # TinyUSB Device HID configuration (2 HID interfaces)
│   ├── usb_descriptors.c      # Composite HID (ITF 0) + Vendor WebHID Configurator (ITF 1)
│   ├── usb_hid.c / .h         # Dynamic button mapping, HID queues, WebHID Feature Reports
│   ├── wiimote_bt.c / .h      # L2CAP HID Host, pairing, PixArt init, 0x33 parser
│   ├── diamond_tracker.c / .h # 4-LED homography, 3/2-LED recovery, calibration
│   ├── one_euro_filter.c / .h # Adaptive 1-Euro anti-jitter filter
│   ├── flash_storage.c / .h   # Internal Flash persistence (v2 + v1 migration) with CRC-32
│   └── ir_leds.c / .h         # GP15 transistor power control
└── tests/
    ├── Makefile
    ├── host_mocks/            # TinyUSB mock header for Linux host unit tests
    └── test_suite.c           # Math, 0x33 parser, Flash, and HID unit tests
```

---

## Host Unit Tests (No Hardware Required)

The tracking math, `0x33` binary packet parser, 1-Euro filter, CRC-32 Flash storage, and USB HID logic can be compiled and verified directly on Linux:

```bash
make -C tests test
```

---

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| Wiimote never pairs | Press the red `SYNC` button (under the battery cover) or hold `1 + 2` *while the Pico is powered*. Replace weak batteries; low voltage causes failed pairing. Watch the UART log (GP0 TX, 115200 baud). |
| Wiimote connects but cursor does not move | Check the 4 IR LEDs are powered (GP15 high, see [`docs/wiring_diagram.md`](docs/wiring_diagram.md)). Test them with a phone camera (IR glows purple). Make sure at least 2 LEDs are in view. |
| Cursor jumps or drifts | Recalibrate (`+` and `-` for 3 s). Remove other IR sources (sunlight, candles, halogen lamps). |
| Cursor lags or jitters | Tune `min_cutoff` / `beta` in the configurator (try the *Extra Steady Couch* or *Fast & Snappy* presets). |
| Configurator says "WebHID is unavailable" | Use Chromium/Chrome and open the hosted page or `http://localhost:8000/docs/configurator.html` (not `file://`). |
| Configurator warns "firmware is older" | Reflash the latest `.uf2` (from GitHub Releases). Older firmware still saves settings but cannot report Flash errors. |
| Configurator can't see / open the Pico | On Linux, install the `udev` rule, replug the Pico, and close any other tab using the device. See the udev notes above. |
| "Writing to Flash FAILED" in the configurator | The setting is active for this session only. Replug the Pico and retry; if it persists, reflash the firmware. |
| Wiimote shows the error LED pattern after calibration | The sample was degenerate (points too close or too few IR LEDs visible) or Flash failed. Retry from a normal seating distance. |
| Pico is not detected in `BOOTSEL` | Use a data-capable USB cable; hold `BOOTSEL` *before* plugging in. |

---

## Contributing and CI

Every push and pull request runs the host unit tests (with AddressSanitizer + UBSan) and builds the firmware for `pico2_w` and `pico_w` via [`.github/workflows/ci.yml`](.github/workflows/ci.yml). Run `make -C tests test` locally before sending changes.

---

## Versioning and Releases

The firmware version lives in [`VERSION`](VERSION) and is printed on the UART at boot. Release notes are in [`CHANGELOG.md`](CHANGELOG.md). Pushing a tag such as `v0.1.0` (it must match `VERSION`) triggers [`release.yml`](.github/workflows/release.yml), which publishes `.uf2` images for Pico 2 W and Pico W plus `SHA256SUMS.txt`. Tags with a hyphen (e.g. `v0.1.0-beta`) are marked as pre-releases.

> **USB IDs:** the device uses the development VID/PID `cafe:4010`. This is fine for personal use and hobby sharing, but it is not an officially allocated identifier and must not be used for commercial products.

---

## License

Distributed under the **GNU General Public License v3.0 (GPLv3)**. See [`LICENSE`](LICENSE).

> **Non-commercial use:** the firmware images link against [BTstack](https://github.com/bluekitchen/btstack), whose license only permits personal, non-commercial use (commercial licenses are available from BlueKitchen GmbH). Do not sell devices or ship this firmware in a commercial product without resolving that. Details for all third-party components are in [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md).
