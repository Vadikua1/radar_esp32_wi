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

static QueueHandle_t data_queue = NULL;

struct dto_struct {
			int angle;
			int dist; 
		};

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
	const float speed_deg_per_sec = 84.709f;	
	engine_init();
	struct dto_struct data;
	
	uint64_t last_time = esp_timer_get_time();

	while(1) {
		distance = calculateDistance();
		uint64_t current_time = esp_timer_get_time();
		float dt = (float)(current_time - last_time) / 1000000.0f;
		last_time = current_time;
		angle += speed_deg_per_sec * dt;;
        if (angle >= 360.0f) {
            angle -= 360.0f;
        }

		data.angle = (int)angle;
		data.dist = distance;
		xQueueSend(data_queue, &data,  10);
		vTaskDelay(pdMS_TO_TICKS(65)); 
	}
}

void data_sender_task(void *pvParameters) {
	struct dto_struct data;
	
	while (1) {
        UBaseType_t waiting_msgs = uxQueueMessagesWaiting(data_queue);
        
        if (waiting_msgs > 15) {
            xQueueReset(data_queue); 
            continue; 
        }

        if (xQueueReceive(data_queue, &data, portMAX_DELAY) == pdPASS) {
            send_radar_data(data.angle, data.dist);
            vTaskDelay(1); 
        }
    }
}


void app_main(void)
{	    
	data_queue = xQueueCreate(45, sizeof(struct dto_struct));
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
    printf("System Ready! Connect to 'Radar' and go to http://ultracoolradar.local\n");


    xTaskCreate(hc_sr04_data_receiver_task, "hc_sr04_data_receiver_task", 4096, NULL, 5, NULL);
	xTaskCreate(data_sender_task, "data_sender_task", 4096, NULL, 5, NULL);

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
		.duty = 1433,
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
	int timeout = 0;
	
	while (gpio_get_level(E_PIN) == 0) {
		timeout++;
		esp_rom_delay_us(1);
		if (timeout > 30000) return 0;
	}
	startTime = esp_timer_get_time();

	timeout = 0; 

	while (gpio_get_level(E_PIN) == 1) {
		timeout++;
		esp_rom_delay_us(1);
		if (timeout > 30000) return 0;
	}
	endTime = esp_timer_get_time();
	
	distance = (endTime - startTime) / 58;
	return distance;
}
