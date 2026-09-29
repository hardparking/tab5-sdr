"""Native tests of production C, without ESP-IDF or device access.

Run: python -m unittest discover -s tests -p 'test_*.py' -v
Set CC to a GCC/Clang-compatible command (including arguments), e.g.
CC='python -m ziglang cc'. TemporaryDirectory respects TMPDIR/TEMP.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
USB_TYPES = '''
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
typedef struct { uint8_t bLength, bDescriptorType; uint16_t wData[127]; } usb_str_desc_t;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\\n",__LINE__,#x); exit(1); } } while(0)
'''


def function(source, signature):
    """Extract a balanced production function (these functions have no brace literals)."""
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def decoder():
    return function((ROOT / 'components/librtlsdr/src/libusb.c').read_text(encoding='utf-8'),
                    'int getUsbDescString(')


def run_c(source):
    command = shlex.split(os.environ.get('CC', 'cc'))
    if not command:
        raise ValueError('CC must contain a compiler command')
    with tempfile.TemporaryDirectory(prefix='rtlsdr-test-') as tmp:
        path = Path(tmp)
        src = path / 'test.c'
        exe = path / ('test.exe' if os.name == 'nt' else 'test')
        src.write_text(source)
        subprocess.run(command + ['-std=c99', '-Wall', '-Wextra', '-Werror',
                                 str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


class USBStrings(unittest.TestCase):
    def test_production_decoder(self):
        run_c(USB_TYPES + decoder() + r'''
int main(void) {
    char out[256];
    usb_str_desc_t d = { .bLength=16, .bDescriptorType=3,
        .wData={'B','l','o','g',' ','V','4','Q'} };
    CHECK(getUsbDescString(&d,out)==0);
    CHECK(strcmp(out,"Blog V4")==0); /* Q is beyond bLength. */
    const char *vendor="RTLSDRBlog";
    d.bLength=2+2*strlen(vendor);
    for (unsigned i=0;i<strlen(vendor);++i) d.wData[i]=vendor[i];
    d.wData[strlen(vendor)]='Q';
    CHECK(getUsbDescString(&d,out)==0 && strcmp(out,vendor)==0);
    CHECK(getUsbDescString(&d,NULL)==0);
    CHECK(getUsbDescString(NULL,NULL)==0);
    out[0]='X'; CHECK(getUsbDescString(NULL,out)<0 && out[0]==0);
    d.bLength=0; CHECK(getUsbDescString(&d,out)<0 && out[0]==0);
    d.bLength=1; CHECK(getUsbDescString(&d,out)<0 && out[0]==0);
    d.bLength=3; CHECK(getUsbDescString(&d,out)<0 && out[0]==0);
    d.bLength=255; CHECK(getUsbDescString(&d,out)<0 && out[0]==0);
    d.bLength=2; CHECK(getUsbDescString(&d,out)==0 && out[0]==0);
    d.bLength=4; d.bDescriptorType=2; CHECK(getUsbDescString(&d,out)<0);
    d.bDescriptorType=3; d.wData[0]=0x1234;
    CHECK(getUsbDescString(&d,out)==0 && strcmp(out,"?")==0);
    memset(out, 'Z', sizeof(out));
    d.bLength=254; for(int i=0;i<126;++i) d.wData[i]='A';
    d.wData[126]='Q';
    CHECK(getUsbDescString(&d,out)==0 && strlen(out)==126 && out[127]=='Z');
    puts("PASS: production USB decoder bounds and malformed/optional inputs");
    return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
