#include <libusb.h>
#include <string.h> /* memcpy — GCC 14 errors on the implicit declaration */
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"

usb_host_client_handle_t uh_handle;
int usbhost_initok=0;

#define TAG "libusb"

void usbhost_begin()
{
	const usb_host_config_t config = {
		.skip_phy_setup = false,
		.intr_flags = ESP_INTR_FLAG_LEVEL1,
	  };
	esp_err_t err = usb_host_install(&config);
	if (err != ESP_OK) {
		ESP_LOGE("libusb", "usb_host_install() err=%x", err);
	  } else {
		ESP_LOGD("libusb", "usb_host_install() ESP_OK");
	  }
	  const usb_host_client_config_t client_config = {
		.is_synchronous = false,
		.max_num_event_msg = 10,
		.async = {
		  .client_event_callback = usbhost_clientEventCallback,
		  .callback_arg = 0,
		}
	  };

	  err = usb_host_client_register(&client_config, &uh_handle);
	  if (err != ESP_OK) {
		ESP_LOGE("libusb", "usb_host_client_register() err=%x", err);
	  } else {
		ESP_LOGD("libusb", "usb_host_client_register() ESP_OK");
	  }

	  TaskHandle_t taskHandle = NULL;
	  BaseType_t taskStatus = xTaskCreate(
		usbhost_daemon_task,
		"uh_daemon",
		10000,
		NULL,
		10,
		&taskHandle
	  );
	
	  if (taskStatus != pdPASS) {
		ESP_LOGE("libusb","Error creating uh daemon task!");
		while (1) { 
			vTaskDelay(1000 / portTICK_PERIOD_MS); 
		}
	  }

	  TaskHandle_t taskHandle2 = NULL;
	  BaseType_t taskStatus2 = xTaskCreate(
		usbhost_one_client_task,
		"uh_client",
		10000,
		NULL,
		10,
		&taskHandle2
	  );
	
	  if (taskStatus2 != pdPASS) {
		ESP_LOGE("libusb","Error creating uh client task!");
		while (1) { 
 		   vTaskDelay(1000 / portTICK_PERIOD_MS); 
		}
	  }


	  usbhost_initok = 1;
}
	
void usbhost_clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *arg) {
	  esp_err_t err;
	  switch (eventMsg->event) {
		case USB_HOST_CLIENT_EVENT_NEW_DEV:
			ESP_LOGI("libusb", "USB_HOST_CLIENT_EVENT_NEW_DEV new_dev.address=%d", eventMsg->new_dev.address);		
	  }
}

void usbhost_daemon_task(){
	uint32_t event_flags;
	while(1){
		esp_err_t err = usb_host_lib_handle_events(1, &event_flags);
		if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
	  		ESP_LOGI("libusb", "usb_host_lib_handle_events() err=%x eventflags=%x", err, &event_flags);
		}
		vTaskDelay(1 / portTICK_PERIOD_MS); 
	}	
}		

void usbhost_one_client_task(){
	uint32_t event_flags;
	while(1){
		esp_err_t err = usb_host_client_handle_events(uh_handle, 1);
		if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
	  		ESP_LOGI("libusb", "usb_host_client_handle_events() err=%x", err);
		}
		vTaskDelay(1 / portTICK_PERIOD_MS); 
	}
}


int libusb_init(){
	return usbhost_initok;
}

void  libusb_exit(){
	
}

/* sync IO */
SemaphoreHandle_t cbSemaphore;
static void sync_transfer_cb(usb_transfer_t *transfer){
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGV(TAG, "Control transfer completed");
        // Обработка полученных данных в transfer->data_buffer
    } else {
        ESP_LOGE(TAG, "Control transfer failed, status: %d", transfer->status);
    }
	xSemaphoreGive(cbSemaphore);
};


static inline uint16_t libusb_cpu_to_le16(const uint16_t x)
{
        union {
                uint8_t  b8[2];
                uint16_t b16;
        } _tmp;
        _tmp.b8[1] = (uint8_t) (x >> 8);
        _tmp.b8[0] = (uint8_t) (x & 0xff);
        return _tmp.b16;
}

int libusb_control_transfer(usb_device_handle_t dev_handle,uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,	unsigned char *data, uint16_t wLength, unsigned int timeout)
{
	ESP_LOGV(TAG,"control_transfer start");
	usb_transfer_t *transfer;
	int r;
	cbSemaphore = xSemaphoreCreateBinary();
	xSemaphoreTake(cbSemaphore, 0); 
    //dataReceived = false;
	
	esp_err_t err;
    err =  usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + wLength, 0, &transfer);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "ct: usb_host_transfer_alloc ERR");
	} else {
		ESP_LOGV(TAG, "ct: usb_host_transfer_alloc OK");
	}

	if ((bmRequestType & USB_BM_REQUEST_TYPE_DIR_IN) == 0 && wLength > 0 && data) {
        memcpy(transfer->data_buffer + sizeof(usb_setup_packet_t), data, wLength);
    }

	transfer->num_bytes = USB_SETUP_PACKET_SIZE + wLength;
	transfer->device_handle = dev_handle;
	transfer->bEndpointAddress = 0x00;
	transfer->callback = sync_transfer_cb;
	transfer->context = NULL;

	usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
	setup->bmRequestType = bmRequestType;
	setup->bRequest = bRequest;
	setup->wValue = wValue; //libusb_cpu_to_le16(wValue);
	setup->wIndex = wIndex; //libusb_cpu_to_le16(wIndex);
	setup->wLength = wLength;//libusb_cpu_to_le16(wLength);
	
	if (bmRequestType & USB_BM_REQUEST_TYPE_DIR_IN) {  
		transfer->flags = USB_TRANSFER_FLAG_ZERO_PACK;  
	}

	
	err = usb_host_transfer_submit_control(uh_handle,transfer);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "ct: usb_host_transfer_submit_control ERR=%d", err);
		return -1;
		usb_host_transfer_free(transfer);		
	} else {
		ESP_LOGV(TAG, "ct: usb_host_transfer_submit_control OK");
	}
	//vTaskDelay(1 / portTICK_PERIOD_MS);

	if (xSemaphoreTake(cbSemaphore, pdMS_TO_TICKS(timeout)) == pdTRUE) {
		ESP_LOGV(TAG, "data ready");
        if (bmRequestType & USB_BM_REQUEST_TYPE_DIR_IN) {
            if (data && wLength > 0) {
                memcpy(data, transfer->data_buffer + USB_SETUP_PACKET_SIZE, transfer->actual_num_bytes - USB_SETUP_PACKET_SIZE);
            }
        }		
		
		

    } else {
       return LIBUSB_ERROR_TIMEOUT; 
    }

	switch (transfer->status) {
		case USB_TRANSFER_STATUS_COMPLETED:      /**< The transfer was successful (but may be short) */
			r = transfer->actual_num_bytes - USB_SETUP_PACKET_SIZE;
			break;
		case USB_TRANSFER_STATUS_TIMED_OUT:      /**< The transfer failed due to a time out */
			r = LIBUSB_ERROR_TIMEOUT;
			break;

		case USB_TRANSFER_STATUS_SKIPPED:        /**< ISOC packets only. The packet was skipped due to system latency or bus overload */
		case USB_TRANSFER_STATUS_CANCELED:       /**< The transfer was canceled */
		case USB_TRANSFER_STATUS_ERROR:          /**< The transfer failed because due to excessive errors (e.g. no response or CRC error) */		
			r = LIBUSB_ERROR_IO;
			break;
		case USB_TRANSFER_STATUS_STALL:          /**< The transfer was stalled */
			r = LIBUSB_ERROR_PIPE;
			break;

		case USB_TRANSFER_STATUS_OVERFLOW:       /**< The transfer as more data was sent than was requested */
			r = LIBUSB_ERROR_OVERFLOW;
			break;

		case USB_TRANSFER_STATUS_NO_DEVICE:      /**< The transfer failed because the target device is gone */
			r = LIBUSB_ERROR_NO_DEVICE;
			break;

	}		

	usb_host_transfer_free(transfer);
	vSemaphoreDelete(cbSemaphore);
	return r;
}


//helpers
int getUsbDescString(const usb_str_desc_t *str_desc, char * str) {
	if (str_desc == NULL) {
		return -1;
	}
	int j =0;
	for (int i = 0; i < str_desc->bLength / 2; i++) {
	  if (str_desc->wData[i] > 0xFF) {
		continue;
	  }
	  str[j++] = (char)(str_desc->wData[i]);
	}
	str[j]='\0';
	return 0;
}
