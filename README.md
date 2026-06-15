# Tab5-SDR

A standalone **RTL-SDR receiver** that runs entirely on an **M5Stack Tab5**
(ESP32-P4) — no PC required. Plug an RTL-SDR dongle into the Tab5's USB-A host
port and get a live spectrum + waterfall, touch tuning, and demodulated audio
out the built-in speaker.

- **Wideband FM** (broadcast) and **narrowband FM** (NOAA weather, conventional
  public-safety voice) demodulation.
- On-device **FFT spectrum + scrolling waterfall** (esp-dsp), landscape, ~20 fps.
- **Touch UI**: drag the waterfall to scan, fine-step tuning, volume, squelch,
  and one-tap **bookmarks** for the NYC area (NOAA / FDNY / EMS).

## Hardware

- **M5Stack Tab5** (ESP32-P4 + ESP32-C6), 720×1280 MIPI-DSI panel.
- An **RTL-SDR** dongle (RTL2832U + R820T/R828D) on the **USB-A** port — a short
  USB-A extension cable is recommended (clears the USB-C port and reduces noise).
- The Tab5's **internal battery must be connected** — it browns out on USB power
  alone, especially under the dongle's ~300 mA load.

> The USB-A port's 5 V VBUS is **off by default**; the firmware enables it at boot
> (`bsp_tab5_set_usb_host_power`).

## Build & flash

Requires **ESP-IDF v5.4+** (developed on v5.4.4).

```bash
idf.py set-target esp32p4    # first time only
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The IDF component manager fetches `esp-dsp`, the LCD/touch/codec drivers, etc.
into `managed_components/` (pinned by `dependencies.lock`).

## Using it

- **Tune:** drag the spectrum/waterfall to scan (~100 MHz per swipe); `[-]`/`[+]`
  step by the selected **STEP** (1 k … 1 M, tap STEP to cycle); or tap a bookmark.
- **WFM/NFM:** toggle the mode button. Broadcast FM = WFM; NOAA and public-safety
  voice = NFM (bookmarks set this automatically).
- **Volume / Squelch:** `[-]`/`[+]`. The SIG meter (top-right) shows signal level;
  the red marker is the squelch threshold — audio mutes below it.
- **Bookmarks:** `WX` / `FDNY` / `EMS` each cycle through their channel list.

### NYC public-safety caveats

The FDNY fire and EMS presets in `main/bookmarks.h` are **conventional analog UHF**
channels from public scanner references — **verify against a current source**, and
note the EMS borough list is incomplete. **NYPD and some agencies use P25 digital
trunked radio, which this simple FM demodulator cannot decode.** NOAA weather
(162 MHz) is analog and works reliably.

## Architecture

The same P4 runs four cooperating pieces:

```
RTL-SDR ──USB-A host──► librtlsdr ──► IQ ring (PSRAM)
                          (libusb shim over esp-idf usb_host)
                                         │
   [core 1] iq_proc: FM demod ──► audio ring ──► [core 1] ES8388 / I2S speaker
            + FFT snapshot ──► spectrum buffer
                                         │
   [core 0] ui: spectrum + waterfall + controls → 640×360 buffer
            ──► PPA (scale ×2, rotate 90°) ──► 720×1280 panel
```

| Module | Role |
|--------|------|
| `main/sdr.c`   | librtlsdr device + async IQ stream → PSRAM ring buffer |
| `main/dsp.c`   | WFM/NFM demod → audio; windowed FFT → spectrum; squelch |
| `main/audio.c` | ES8388 speaker output (esp_codec_dev) via a StreamBuffer |
| `main/ui.c`    | landscape touch UI; PPA-rotated rendering |
| `main/bookmarks.h` | NYC NOAA / FDNY / EMS presets |
| `components/bsp/` | M5Stack Tab5 board support (panel, touch, I/O, audio, USB power) |
| `components/librtlsdr/` | librtlsdr ported onto the ESP-IDF USB host stack |

Rendering runs at half resolution (640×360) and uses the P4 **PPA** to rotate/scale
onto the portrait panel — full-res rotation costs ~46 ms/frame, half-res is ~4×
cheaper.

## Credits & license

This project is licensed under the **GNU General Public License v2.0 or later**
(see [LICENSE](LICENSE)), as required by librtlsdr.

- **[librtlsdr](https://github.com/steve-m/librtlsdr)** — Steve Markgraf,
  Dimitri Stolnikov, et al. (GPL-2.0+). The ESP-IDF USB-host port in
  `components/librtlsdr/` derives from
  [XTR1984/xtrsdr](https://github.com/XTR1984/xtrsdr), which runs librtlsdr on
  the ESP32 USB host stack.
- **Board support** (`components/bsp/`) — derived from
  [Tab5-HID-Device](https://github.com/hardparking/Tab5-HID-Device) by
  Hiroki Kawakami (MIT).
- **esp-dsp**, **esp_codec_dev**, and the LCD/touch drivers — Espressif (Apache-2.0).

Modifications to the vendored librtlsdr sources are documented inline (e.g. the
`usb_host` shim and a missing `<string.h>` include for GCC 14).
