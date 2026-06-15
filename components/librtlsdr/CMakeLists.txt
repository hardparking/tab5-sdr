# librtlsdr ported onto the ESP-IDF USB host stack.
# Source taken verbatim from _rtlsdr_ref/xtrsdr/librtlsdr (the P4-proven variant):
# librtlsdr.c calls usb_host_* directly and libusb.c is a thin libusb shim over
# the same API. The public header is src/rtl-sdr.h; src/libusb.h is the shim.
file(GLOB LIBRTLSDR_SRCS "src/*.c")
idf_component_register(
    SRCS ${LIBRTLSDR_SRCS}
    INCLUDE_DIRS "src"
    REQUIRES usb pthread)

# Upstream librtlsdr is not warning-clean; keep its warnings from failing the build.
target_compile_options(${COMPONENT_LIB} PRIVATE -w)
