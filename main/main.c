#include "freertos/idf_additions.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include "hal/gpio_types.h"
#include "soc/gpio_num.h"
#include "hal/lcd_types.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "freertos/queue.h"


#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_0

extern void start_webserver(void);
extern void send_radar_data(int angle, int distance);
extern void wifi_init_softap(void);
extern void start_mdns_service(void);
extern void start_webserver(void);
extern void start_dns_server(void);

static QueueHandle_t angle_queue = NULL;
static QueueHandle_t distance_queue = NULL;

enum {
	PIN_18 = 18,
	T_PIN = 5,
	E_PIN = 17,
};

uint32_t calculateDistance(void);

void engine_init(void);

void hc_sr04_data_receiver_task(void *pvParameters) {
	int distance = 0;
	float angle = 0.0;
	const float angle_step = 8.823; 
	engine_init();


	while(1) {
		distance = calculateDistance();
		angle += angle_step;
        if (angle >= 360.0) {
            angle = 0.0;
        }
        int angle_to_send = angle;
		send_radar_data(angle, distance);
		vTaskDelay(pdMS_TO_TICKS(60)); 
	}
}


void app_main(void)
{	    
	angle_queue = xQueueCreate(20, sizeof(int));
	distance_queue = xQueueCreate(20, sizeof(int));
	// gpio
	gpio_reset_pin(T_PIN);
	gpio_reset_pin(E_PIN);
	gpio_set_direction(T_PIN, GPIO_MODE_OUTPUT);
	gpio_set_direction(E_PIN, GPIO_MODE_INPUT);
	
	 esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_softap();
    start_mdns_service();
    start_webserver();
    xTaskCreate(hc_sr04_data_receiver_task, "hc_sr04_data_receiver_task", 4096, NULL, 5, NULL);
    printf("System Ready! Connect to 'Radar' and go to http://ultracoolradar.local\n");

}

void engine_init(void) {
		ledc_timer_config_t ledc_timer = {
		.speed_mode = LEDC_MODE,
		.timer_num = LEDC_TIMER_0,
		.freq_hz = 50,
		.clk_cfg = LEDC_AUTO_CLK,
		.duty_resolution = LEDC_TIMER_14_BIT,  
	};
	ledc_timer_config(&ledc_timer);
	
	ledc_channel_config_t ledc_channel = {
		.gpio_num = PIN_18,
		.speed_mode = LEDC_MODE,
		.channel = LEDC_CHANNEL,
		.timer_sel = LEDC_TIMER_0,
		.duty = 1024,
		.hpoint = 0
	};
	ledc_channel_config(&ledc_channel);
}

uint32_t calculateDistance(void) {
	gpio_set_level(T_PIN, 1);
	esp_rom_delay_us(10);
	gpio_set_level(T_PIN, 0);
	uint64_t startTime = 0;
	uint64_t endTime = 0;
	uint32_t distance = 0;
	
	while (gpio_get_level(E_PIN) == 0) {
	}
	startTime = esp_timer_get_time();

	while (gpio_get_level(E_PIN) == 1) {
	}
	endTime = esp_timer_get_time();
	
	distance = (endTime - startTime) / 58;
	return distance;
	
}
