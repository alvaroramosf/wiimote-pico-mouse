# Third-party licenses

The source code in this repository (everything under `src/`, `tests/`, `docs/`) is licensed under the
**GNU GPL v3.0** (see [`LICENSE`](LICENSE)).

The firmware is built against the following components from the Raspberry Pi
[`pico-sdk`](https://github.com/raspberrypi/pico-sdk). They are **not** included in this repository, but
they **are linked into the released `.uf2` firmware images**, so their terms apply to those binaries.

| Component | License | Notes |
|---|---|---|
| pico-sdk | BSD-3-Clause | Raspberry Pi Ltd |
| TinyUSB | MIT | USB device stack (hathach) |
| BTstack | BlueKitchen license (BSD-style **with a non-commercial clause**) | See below |
| cyw43-driver | Raspberry Pi / George Robotics license | May only be used with Raspberry Pi silicon (RP2040 / RP2350) |

## BTstack: non-commercial use only

BTstack's license (`lib/btstack/LICENSE` in the pico-sdk) states, in clause 4:

> Any redistribution, use, or modification is done solely for personal benefit and not for any commercial
> purpose or for monetary gain.

Commercial use requires a separate license from BlueKitchen GmbH (<contact@bluekitchen-gmbh.com>).

**Consequences for this project:**

- The firmware images published here are intended for **personal, hobby, and educational use only**.
- Do not sell devices, or distribute the firmware as part of a commercial product, without first obtaining
  a commercial BTstack license.
- This clause is stricter than the GPL allows for a combined work. The GPLv3 terms apply to the code written
  for this project; the binaries additionally carry the restrictions of the components above. If you need a
  clean, commercially usable build, you would have to replace the Bluetooth stack (or license BTstack) and
  seek your own legal advice.

## cyw43-driver

The Wi-Fi/Bluetooth chip driver (`lib/cyw43-driver`) may only be used with Raspberry Pi semiconductor
devices. This project targets only the Raspberry Pi Pico W / Pico 2 W, which satisfies that condition.

---

*This file is informational and is not legal advice. Always check the license files shipped with the
version of pico-sdk you build against.*
