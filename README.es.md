# wiimote-pico-mouse (Español)

🇬🇧 [Read in English](README.md)

Firmware en C (`pico-sdk`) para **Raspberry Pi Pico 2 W** (o **Pico W**) que conecta un **Wii Remote original** (`RVL-CNT-01` / `RVL-CNT-01-TR`) por **Bluetooth Classic** y lo expone por USB como un **puntero absoluto + teclado + teclas multimedia**. Funciona sin drivers ni software en el PC (Linux Wayland/X11).

Los detalles de hardware (BOM, colocación de los 4 LEDs IR, esquema) están en el [README en inglés](README.md) y en [`docs/wiring_diagram.md`](docs/wiring_diagram.md). Las matemáticas del seguimiento están en [`docs/math_tracking.md`](docs/math_tracking.md).

## Compilar y flashear

1. **Dependencias**

   - Fedora: `sudo dnf install gcc g++ cmake make git python3 arm-none-eabi-gcc-cs arm-none-eabi-gcc-cs-c++ arm-none-eabi-binutils arm-none-eabi-newlib`
   - Debian/Ubuntu: `sudo apt install build-essential cmake git python3 gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib`
   - Arch: `sudo pacman -S base-devel cmake git python arm-none-eabi-gcc arm-none-eabi-newlib`

2. **pico-sdk**

   ```bash
   git clone https://github.com/raspberrypi/pico-sdk.git
   cd pico-sdk && git submodule update --init
   export PICO_SDK_PATH="$(pwd)"; cd ..
   ```

3. **Compilar** (Pico 2 W por defecto; añade `-DPICO_BOARD=pico_w` para Pico W)

   ```bash
   mkdir -p build && cd build
   cmake .. -DPICO_SDK_PATH="$PICO_SDK_PATH"
   make -j"$(nproc)"
   ```

4. **Flashear**: mantén pulsado **BOOTSEL** al conectar la Pico por USB y copia `build/wiimote_pico_mouse.uf2` a la unidad `RP2350` / `RPI-RP2`.

## Emparejamiento y calibración

- **Emparejar:** pulsa el botón rojo `SYNC` (bajo la tapa de las pilas) o mantén `1 + 2`. Después, cualquier botón reconecta automáticamente.
- **Calibrar (2 puntos):** mantén `+` y `-` durante 3 s, apunta a la esquina superior izquierda y pulsa `A`; luego a la inferior derecha y pulsa `A`.

| Patrón en el Wiimote | Significado |
|---|---|
| LED 1 parpadeando | Esperando la muestra de la esquina superior izquierda |
| Vibración breve y LED 4 parpadeando | Paso 1 aceptado; esperando la esquina inferior derecha |
| Todos los LEDs fijos + vibración (~1 s), luego LED 1 fijo | Calibración guardada en Flash |
| LEDs 1+3 / 2+4 alternando + vibración larga (~1 s) | **Error:** muestra degenerada (se restaura la calibración anterior) o fallo al escribir en Flash |
| LED 1 + vibración muy corta | Calibración cancelada |

## Configurador WebHID

1. Instala la regla udev una sola vez:

   ```bash
   sudo cp docs/99-wiimote-pico.rules /etc/udev/rules.d/
   sudo udevadm control --reload-rules && sudo udevadm trigger
   ```

   Si tu sistema no soporta `uaccess`, cambia `TAG+="uaccess"` por `GROUP="plugdev"` (Debian/Ubuntu) y ejecuta `sudo usermod -aG plugdev $USER` (hay que reiniciar la sesión).

2. **Opción más fácil (alojado):** activa GitHub Pages en tu fork (Settings → Pages → Source: *GitHub Actions*; el workflow [`pages.yml`](.github/workflows/pages.yml) lo publica) y abre `https://<tu-usuario>.github.io/wiimote-pico-mouse/` en Chromium/Chrome. No hace falta servidor local, y la propia página incluye una guía de primera configuración con el comando de la regla udev listo para copiar.

   **Alternativa local:** sirve la página desde la raíz del repositorio (WebHID no funciona con `file://`):

   ```bash
   python3 -m http.server 8000
   ```

3. Abre `http://localhost:8000/docs/configurator.html` en **Chromium/Chrome** (o la URL de Pages), pulsa **Connect Pico** (las siguientes veces se reconecta solo), ajusta los botones o el filtro —o carga un perfil: *Mouse*, *Presentaciones*, *Media center*, o importa/exporta JSON— y pulsa **Save to Pico Flash**. Si el firmware es antiguo, la página te avisa para que lo vuelvas a flashear. La página espera la confirmación de la Pico e informa de errores (CRC, valores fuera de rango o fallo de Flash).

## Resolución de problemas

| Síntoma | Causa probable / solución |
|---|---|
| El Wiimote no empareja | Pulsa `SYNC` o `1 + 2` con la Pico encendida; cambia las pilas; revisa el log UART (GP0, 115200 baudios). |
| Conecta pero el cursor no se mueve | Comprueba que los 4 LEDs IR reciben alimentación (GP15) con la cámara del móvil y que al menos 2 están a la vista. |
| El cursor salta o deriva | Recalibra; elimina otras fuentes IR (sol, velas, halógenos). |
| El cursor va a tirones o con retraso | Ajusta `min_cutoff` / `beta` o prueba los presets del configurador. |
| El configurador dice "WebHID is unavailable" | Usa Chromium/Chrome y `http://localhost`, no `file://`. |
| El configurador no ve la Pico | Instala la regla udev, reconecta la Pico y cierra otras pestañas que usen el dispositivo. |
| "Writing to Flash FAILED" | El ajuste solo vale para esta sesión; reconecta y reintenta. |

## Tests

```bash
make -C tests test
```

La integración continua (`.github/workflows/ci.yml`) ejecuta los tests con ASan/UBSan y compila el firmware para `pico2_w` y `pico_w`.

## Licencia

GPLv3. Ver [`LICENSE`](LICENSE).
