"""Static and host-C regression guards; not on-device display/touch proof.

Run: python -m unittest discover -s tests -p 'test_st7121_contract.py' -v
Set CC to a GCC/Clang-compatible command (quote executable paths with spaces;
use forward slashes on Windows). Python 3.8+ and a native C compiler are required.
"""
from pathlib import Path
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ST7121Contract(unittest.TestCase):
    def test_component_uses_header_version_without_redefining_it(self):
        cmake = (ROOT / 'components/esp_lcd_st7121/CMakeLists.txt').read_text(encoding='utf-8')
        self.assertNotIn('cu_pkg_define_version(', cmake)

    def test_firmware_version_selects_real_st7121_driver(self):
        bsp = (ROOT / 'components/bsp/src/bsp_tab5.c').read_text(encoding='utf-8')
        self.assertIn('esp_lcd_panel_io_rx_param(probe_io, 0x0000, &fw_version, 1)', bsp)
        self.assertIn('fw_version == 1', bsp)
        self.assertIn('panel = BSP_PANEL_ST7121;', bsp)
        self.assertIn('fw_version != 3', bsp)
        self.assertIn('.is_st7121 = panel == BSP_PANEL_ST7121', bsp)
        lcd = (ROOT / 'components/bsp/devices/st7123/st7123_lcd.c').read_text(encoding='utf-8')
        self.assertIn('esp_lcd_new_panel_st7121(state->io, &panel_config, &state->panel)', lcd)
        self.assertIn('st7121_vendor_config_t vendor_config_7121', lcd)
        for field, new, old in [('lane_bit_rate_mbps',965,870),('dpi_clock_freq_mhz',70,75),('hsync_pulse_width',2,40),('hsync_back_porch',40,140),('vsync_pulse_width',20,2),('vsync_back_porch',24,8),('vsync_front_porch',200,220)]:
            self.assertIn(f'.{field} = config->is_st7121 ? {new} : {old}', lcd)
        self.assertIn('esp_lcd_st7121', (ROOT / 'components/bsp/CMakeLists.txt').read_text(encoding='utf-8'))
        self.assertTrue((ROOT / 'components/esp_lcd_st7121/esp_lcd_st7121.c').is_file())

class PanelSelectionBehavior(unittest.TestCase):
    """Compile the production selection block with fake I2C, not a reimplementation.

    Requires a GCC/Clang-compatible host compiler. Set CC (e.g. 'zig cc' or
    'python -m ziglang cc') to override discovery; no ESP-IDF or hardware needed.
    """

    def test_auto_and_explicit_overrides(self):
        cc = shlex.split(os.environ['CC']) if os.environ.get('CC') else None
        if cc is None:
            cc = next(([p] for name in ('cc', 'gcc', 'clang')
                       if (p := shutil.which(name))), None)
        self.assertIsNotNone(cc, 'Install a host C compiler or set CC')
        bsp = (ROOT / 'components/bsp/src/bsp_tab5.c').read_text(encoding='utf-8')
        enum = re.search(r'typedef enum \{[^}]+\} bsp_panel_type_t;', bsp).group()
        start = bsp.index('    if (!have_st7123_tp && !gt911_addr)')
        end = bsp.index('    // Initialize the panel', start)
        block = bsp[start:end]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define ESP_OK 0
#define ESP_ERR_NOT_SUPPORTED -1
#define ESP_ERR_NOT_FOUND -2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define BSP_RETURN_ERR(e) do { if ((e) != ESP_OK) return (e); } while (0)
typedef void *esp_lcd_panel_io_handle_t;
typedef struct { int scl_speed_hz; } esp_lcd_panel_io_i2c_config_t;
#define ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG() {0}
static int create_error, read_error, creates, reads, deletes;
static uint8_t firmware;
static int esp_lcd_new_panel_io_i2c(int bus, const esp_lcd_panel_io_i2c_config_t *c,
                                  esp_lcd_panel_io_handle_t *io) {
    assert(bus == 0 && c->scl_speed_hz == 100000);
    creates++;
    if (!create_error) *io = (void *)1;
    return create_error;
}
static int esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t io, int reg,
                                   uint8_t *value, int size) {
    assert(io == (void *)1 && reg == 0 && size == 1);
    reads++; *value = firmware; return read_error;
}
static int esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io) {
    assert(io == (void *)1); deletes++; return 0;
}
ENUM
static int select_panel(bool have_st7123_tp, int gt911_addr, int *selected) {
    int err = 0, i2c0 = 0;
BLOCK
    *selected = panel;
    return ESP_OK;
}
int main(void) {
    for (int fw = 0; fw < 256; fw++) {
        for (int fail = 0; fail < 3; fail++) {
            firmware = fw;
            create_error = fail == 1 ? -10 : 0;
            read_error = fail == 2 ? -11 : 0;
            creates = reads = deletes = 0;
            int panel = -99;
            int result = select_panel(true, 0, &panel);
#if defined(CONFIG_BSP_TAB5_PANEL_FORCE_ILI9881C)
            assert(result == 0 && panel == BSP_PANEL_ILI9881C);
            assert(creates == 0 && reads == 0 && deletes == 0);
#elif defined(CONFIG_BSP_TAB5_PANEL_FORCE_ST7123)
            assert(result == 0 && panel == BSP_PANEL_ST7123);
            assert(creates == 0 && reads == 0 && deletes == 0);
#else
            assert(creates == 1);
            assert(reads == (fail != 1) && deletes == (fail != 1));
            if (fail) assert(result == (fail == 1 ? -10 : -11) && panel == -99);
            else if (fw == 1) assert(result == 0 && panel == BSP_PANEL_ST7121);
            else if (fw == 3) assert(result == 0 && panel == BSP_PANEL_ST7123);
            else assert(result == ESP_ERR_NOT_SUPPORTED && panel == -99);
#endif
        }
    }
    creates = reads = deletes = 0;
    int panel = -99;
    assert(select_panel(false, 0, &panel) == ESP_ERR_NOT_FOUND);
    assert(panel == -99);
    assert(select_panel(false, 0x5d, &panel) == ESP_OK);
#ifdef CONFIG_BSP_TAB5_PANEL_FORCE_ST7123
    assert(panel == BSP_PANEL_ST7123);
#else
    assert(panel == BSP_PANEL_ILI9881C);
#endif
    assert(creates == 0 && reads == 0 && deletes == 0);
    return 0;
}
'''.replace('ENUM', enum).replace('BLOCK', block)
        with tempfile.TemporaryDirectory(prefix='st7121-test-') as tmp:
            source = Path(tmp) / 'selection.c'
            source.write_text(harness, encoding='utf-8')
            for mode in ('AUTO', 'FORCE_ILI9881C', 'FORCE_ST7123'):
                with self.subTest(mode=mode):
                    binary = Path(tmp) / ('selection.exe' if os.name == 'nt' else 'selection')
                    build = subprocess.run(cc + ['-std=c99', '-D' + 'CONFIG_BSP_TAB5_PANEL_' + mode,
                                                 str(source), '-o', str(binary)],
                                           capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                    run = subprocess.run([str(binary)], capture_output=True, text=True)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
