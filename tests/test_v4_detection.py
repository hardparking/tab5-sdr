"""Execute production identity and tuner-selection blocks with host USB stubs.

The I2C probe and ESP-IDF lifecycle are not emulated: the fixture supplies the
probed tuner type and cached descriptors. See test_usb_strings.py for CC usage.
"""
import re
import unittest
from test_usb_strings import ROOT, USB_TYPES, decoder, function, run_c


class V4Detection(unittest.TestCase):
    def test_identity_and_clock_selection(self):
        source = (ROOT / 'components/librtlsdr/src/librtlsdr.c').read_text(encoding='utf-8')
        opened = function(source, 'int rtlsdr_open(')
        identity = opened.split('dev->dev_lost = 0;', 1)[1].split('/* Probe tuners */', 1)[0]
        selection = opened.split('found:', 1)[1].split('ESP_LOGD(TAG,"tuner init")', 1)[0]
        constants = '\n'.join(re.findall(r'^#define (?:DEF_RTL_XTAL_FREQ|R828D_XTAL_FREQ)\b.*', source, re.M))
        # R828D_XTAL_FREQ is supplied by the production tuner header.
        header = (ROOT / 'components/librtlsdr/src/tuner_r82xx.h').read_text(encoding='utf-8')
        constants += '\n' + '\n'.join(re.findall(r'^#define R828D_XTAL_FREQ\b.*', header, re.M))
        run_c(USB_TYPES + constants + r'''
enum { RTLSDR_TUNER_UNKNOWN, RTLSDR_TUNER_E4000, RTLSDR_TUNER_FC0012,
       RTLSDR_TUNER_FC0013, RTLSDR_TUNER_FC2580, RTLSDR_TUNER_R820T, RTLSDR_TUNER_R828D };
typedef struct { int unused; } tuner_t;
static tuner_t tuners[7];
typedef struct {
    void *devh;
    char manufact[256], product[256];
    int tuner_type, rtl_xtal, tun_xtal;
    tuner_t *tuner;
} rtlsdr_dev_t;
typedef struct {
    const usb_str_desc_t *str_desc_manufacturer, *str_desc_product;
} usb_device_info_t;
static usb_device_info_t cached;
static int info_result, info_calls, closed, repeater, writes;
static void *expected_handle;
#define ESP_OK 0
#define TAG "test"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define R82XX_IF_FREQ 3570000
int usb_host_device_info(void *handle, usb_device_info_t *info) {
    CHECK(handle == expected_handle); ++info_calls;
    if (info_result == ESP_OK) *info = cached;
    return info_result;
}
int rtlsdr_close(rtlsdr_dev_t *dev) { CHECK(dev != NULL); ++closed; return 0; }
void rtlsdr_set_i2c_repeater(rtlsdr_dev_t *dev, int value) { (void)dev; repeater=value; }
void rtlsdr_demod_write_reg(rtlsdr_dev_t *dev, int a, int b, int c, int d) {
    (void)dev; (void)a; (void)b; (void)c; (void)d; ++writes;
}
void rtlsdr_set_if_freq(rtlsdr_dev_t *dev, int freq) { (void)dev; (void)freq; }
void rtlsdr_set_direct_sampling(rtlsdr_dev_t *dev, int mode) { (void)dev; (void)mode; }
''' + decoder() + function(source, 'int rtlsdr_check_dongle_model(') + r'''
static int configure(rtlsdr_dev_t **out_dev, rtlsdr_dev_t *dev) {
''' + identity + '\n' + selection + r'''
    *out_dev = dev;
    return 0;
}
static usb_str_desc_t desc(const char *text) {
    usb_str_desc_t d = {0}; d.bDescriptorType=3; d.bLength=2+2*strlen(text);
    for (unsigned i=0;i<strlen(text);++i) d.wData[i]=text[i];
    d.wData[strlen(text)]='Q'; /* A code unit beyond the descriptor. */
    return d;
}
static void check(int tuner, int accepted, int v4, int clock) {
    rtlsdr_dev_t dev = {0}, *out = &dev;
    dev.devh=&dev; expected_handle=dev.devh;
    dev.rtl_xtal=DEF_RTL_XTAL_FREQ; dev.tuner_type=tuner;
    info_calls=closed=writes=0; repeater=1;
    int result=configure(&out, &dev);
    CHECK(info_calls==1);
    if (accepted) {
        CHECK(result==0 && out==&dev && closed==0);
        CHECK(dev.tun_xtal==clock);
        CHECK(rtlsdr_check_dongle_model(&dev,"RTLSDRBlog","Blog V4")==v4);
    } else {
        CHECK(result==-EIO && out==NULL && closed==1 && repeater==0 && writes==0);
    }
}
int main(void) {
    usb_str_desc_t vendor=desc("RTLSDRBlog"), product=desc("Blog V4");
    cached.str_desc_manufacturer=&vendor; cached.str_desc_product=&product;
    check(RTLSDR_TUNER_R828D,1,1,28800000);
    product=desc("Blog V3"); check(RTLSDR_TUNER_R828D,1,0,16000000);
    product=desc("Blog V4 extra"); check(RTLSDR_TUNER_R828D,1,0,16000000);
    vendor=desc("Other"); product=desc("Blog V4"); check(RTLSDR_TUNER_R828D,1,0,16000000);
    vendor=desc("RTLSDRBlog"); product=desc("blog v4"); check(RTLSDR_TUNER_R828D,1,0,16000000);
    for (int mode=0;mode<6;++mode) {
        vendor=desc("RTLSDRBlog"); product=desc("Blog V4");
        cached.str_desc_manufacturer=&vendor; cached.str_desc_product=&product; info_result=0;
        if (mode==0) info_result=-1;
        if (mode==1) cached.str_desc_manufacturer=NULL;
        if (mode==2) cached.str_desc_product=NULL;
        if (mode==3) product.bLength=3;
        if (mode==4) vendor.bLength=2;
        if (mode==5) product.bDescriptorType=2;
        check(RTLSDR_TUNER_R828D,0,0,0);
        for (int tuner=RTLSDR_TUNER_UNKNOWN;tuner<RTLSDR_TUNER_R828D;++tuner)
            check(tuner,1,0,28800000);
    }
    puts("PASS: cached identity, exact model/clock selection, R828D ambiguity and other tuners");
    return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
