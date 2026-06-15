#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "usb/usb_host.h"


#ifdef __cplusplus
extern "C" {
#endif

extern usb_host_client_handle_t uh_handle;

void usbhost_begin();
void usbhost_daemon_task();
void usbhost_one_client_task();
void usbhost_clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *arg);
int libusb_init();
int getUsbDescString(const usb_str_desc_t *str_desc, char* str);

/* sync I/O*/
int libusb_control_transfer(usb_device_handle_t dev_handle,uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,	unsigned char *data, uint16_t wLength, unsigned int timeout);
int libusb_bulk_transfer(usb_device_handle_t dev_handle, unsigned char endpoint, unsigned char *data, int length,	int *transferred, unsigned int timeout);


enum libusb_error {
	/** Success (no error) */
	LIBUSB_SUCCESS = 0,

	/** Input/output error */
	LIBUSB_ERROR_IO = -1,

	/** Invalid parameter */
	LIBUSB_ERROR_INVALID_PARAM = -2,

	/** Access denied (insufficient permissions) */
	LIBUSB_ERROR_ACCESS = -3,

	/** No such device (it may have been disconnected) */
	LIBUSB_ERROR_NO_DEVICE = -4,

	/** Entity not found */
	LIBUSB_ERROR_NOT_FOUND = -5,

	/** Resource busy */
	LIBUSB_ERROR_BUSY = -6,

	/** Operation timed out */
	LIBUSB_ERROR_TIMEOUT = -7,

	/** Overflow */
	LIBUSB_ERROR_OVERFLOW = -8,

	/** Pipe error */
	LIBUSB_ERROR_PIPE = -9,

	/** System call interrupted (perhaps due to signal) */
	LIBUSB_ERROR_INTERRUPTED = -10,

	/** Insufficient memory */
	LIBUSB_ERROR_NO_MEM = -11,

	/** Operation not supported or unimplemented on this platform */
	LIBUSB_ERROR_NOT_SUPPORTED = -12,

	/* NB: Remember to update LIBUSB_ERROR_COUNT below as well as the
	   message strings in strerror.c when adding new error codes here. */

	/** Other error */
	LIBUSB_ERROR_OTHER = -99
};

#ifdef __cplusplus
}
#endif


