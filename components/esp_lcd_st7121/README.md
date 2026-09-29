# Tab5 ST7121 panel driver

## Provenance and licenses

Vendored from [M5Stack/M5Tab5-UserDemo](https://github.com/m5stack/M5Tab5-UserDemo/tree/b4e356bc491ca070d54004718dad789c07d5fc93/platforms/tab5/components/esp_lcd_st7121),
revision `b4e356bc491ca070d54004718dad789c07d5fc93`.
`esp_lcd_st7121.c` and `include/esp_lcd_st7121.h` are unchanged from that revision,
including their Espressif copyright and Apache-2.0 SPDX notices. See
[LICENSE](LICENSE) for the Apache License, Version 2.0.

Local packaging changes: add `idf_component.yml` for ESP-IDF >=5.4;
remove `cu_pkg_define_version` from CMake because the vendor header already
supplies version macros; expand this README and include license texts.
The upstream repository's MIT notice is retained in [LICENSE-M5Stack](LICENSE-M5Stack).
It does not replace the driver files' explicit Apache-2.0 license.

## Tab5 integration

The firmware-byte mapping and Tab5 timing are sourced from
[`m5stack_tab5.c`](https://github.com/m5stack/M5Tab5-UserDemo/blob/b4e356bc491ca070d54004718dad789c07d5fc93/platforms/tab5/components/m5stack_tab5/m5stack_tab5.c)
at the same revision. Touch address `0x55`, register `0x0000`, byte `1` selects
ST7121; byte `3` selects ST7123. Auto detection fails on unknown bytes or I2C
read errors rather than silently sending ST7123 commands. Existing explicit
ILI9881C/ST7123 Kconfig overrides bypass firmware identification, but still
require a detected touch controller. Touch initialization remains independent
of the forced display selection.

The existing `st7123_lcd` BSP wrapper shares framebuffer/backlight handling;
its `is_st7121` flag selects this distinct panel driver and the Tab5 timing:
720x1280, two DSI lanes at 965 Mbps, DPI 70 MHz, horizontal pulse/back/front
2/40/40 and vertical 20/24/200. The older ST7123 timing and initialization path
remain unchanged. Do not use the vendor header's generic 720x1560 timing macro
for Tab5.

## Host regression tests

From the repository root:

```sh
python -m unittest discover -s tests -p 'test_st7121_contract.py' -v
```

Requires Python 3.8+ and a GCC/Clang-compatible native C compiler. Tests discover
`cc`, `gcc`, or `clang`, or accept a command through `CC`, such as `CC='zig cc'`.
Use forward slashes and quote executable paths containing spaces on Windows.
The tests compile the production panel-selection block against fake I2C calls
in auto and both override modes, exercising all firmware byte values, I/O
errors, cleanup, no-touch rejection and the GT911 path. Static guards check
panel-driver wiring and both timing sets. These tests and an ESP-IDF build do
not establish physical display or touch behavior.