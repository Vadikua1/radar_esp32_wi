#include "mdns.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/sockets.h"

extern void start_webserver(void);
extern void send_radar_data(int angle, int distance);
extern void wifi_init_softap(void);
extern void start_mdns_service(void);
extern void start_webserver(void);
extern void start_dns_server(void);

// задача-імітатор
void radar_mock_task(void *pvParameters) {
    float angle = 0.0; 
    
    // Швидкість: 60 град / 0.17 сек = 352.94 град/сек
    // Крок за один такт (50мс): 352.94 / 20 = ~17.65
    const float angle_step = 8.823; 

    while(1) {
        angle += angle_step;
        if (angle >= 360.0) {
            angle -= 360.0;
        }

        int dist = 0;
        if (rand() % 10 == 0) { 
            dist = 50 + (rand() % 150); 
        }

        send_radar_data((int)angle, dist);
        
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    printf("Starting Radar System...\n");
    wifi_init_softap();
    start_mdns_service();
    start_webserver();
    xTaskCreate(radar_mock_task, "radar_mock", 4096, NULL, 5, NULL);
    
    printf("System Ready! Connect to 'Radar' and go to http://ultracoolradar.local\n");
}
