#include <esp_http_server.h>
#include <esp_log.h>
#include <cJSON.h>
#include "lwip/sockets.h"
#include "mdns.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

static const char *TAG = "WEB_SERVER";
httpd_handle_t server = NULL;


void send_radar_data(int angle, int distance) {
    if (server == NULL) return;

    char json_string[64];
    snprintf(json_string, sizeof(json_string), "{\"a\":%d,\"d\":%d}", angle, distance);

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)json_string;
    ws_pkt.len = strlen(json_string);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;


    size_t clients = 10;
    int client_fds[10];
    if (httpd_get_client_list(server, &clients, client_fds) == ESP_OK) {
        for (size_t i = 0; i < clients; i++) {
            if (httpd_ws_get_fd_info(server, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                httpd_ws_send_frame_async(server, client_fds[i], &ws_pkt);
            }
        }
    }
}


static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, client connected");
        return ESP_OK;
    }
    return ESP_OK;
}

esp_err_t captive_portal_handler(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0); 
    return ESP_OK;
}

// index.html
esp_err_t index_get_handler(httpd_req_t *req) {
    extern const unsigned char index_html_start[] asm("_binary_index_html_start");
    extern const unsigned char index_html_end[]   asm("_binary_index_html_end");
    const size_t index_html_size = (index_html_end - index_html_start);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_size);
}

void start_webserver() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

   if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t ws_uri = { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true };
        httpd_register_uri_handler(server, &ws_uri);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_portal_handler);
    }
}

void dns_server_task(void *pvParameters) {
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(53);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    char rx_buffer[128];
    while (1) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &socklen);
        
        if (len > 0) {
            rx_buffer[2] |= 0x80;
            rx_buffer[3] |= 0x80;
            rx_buffer[7] = 1;
            
            // Хвостовик відповіді (вказівник на ім'я, тип A, клас IN, TTL, довжина 4, і наш IP 192.168.4.1)
            char answer[] = {0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x04, 192, 168, 4, 1};
            
            // Доклеюємо нашу відповідь до оригінального запиту і відправляємо назад
            memcpy(rx_buffer + len, answer, sizeof(answer));
            sendto(sock, rx_buffer, len + sizeof(answer), 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
        }
    }
}

void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "Radar",
            .ssid_len = strlen("Radar"),
            .channel = 1,
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    printf("Wi-Fi AP started. SSID: Radar, Password: 12345678\n");
}


void start_mdns_service(void) {
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("ultracoolradar"));
    ESP_ERROR_CHECK(mdns_instance_name_set("Radar System"));
    ESP_ERROR_CHECK(mdns_service_add("Web Server", "_http", "_tcp", 80, NULL, 0));
    
    printf("mDNS started! http://ultracoolradar.local\n");
}

void start_dns_server(void) {
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, NULL);
}