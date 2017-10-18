/* for json parse demo based on Simple TCP Client by CW

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

static int got_ip_flag = 0;

static const char *TAG = "tcp client";

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        got_ip_flag = 1;
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        got_ip_flag = 0;
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

// print pointer data
// para1: data -> start address of print
// para2: len -> print len
// para3: note -> note for print
void print_debug(const char *data, const int len, const char *note)
{
#define COUNT_BYTE_AND_NEW_LINE 0
#define ALL_BINARY_SHOW 0
    printf("\n********** %s [len:%d] start addr:%p **********\n", note, len, data);
    int i = 0;
    for (i = 0; i < len; ++i) {
#if !(ALL_BINARY_SHOW)
        if (data[i] < 33 || data[i] > 126) {
            if (i > 0 && (data[i - 1] >= 33 && data[i - 1] <= 126) ) {
                printf(" ");
            }
            printf("%02x ", data[i]);
        } else {
            printf("%c", data[i]);
        }
#else
        printf("%02x ", data[i]);
#endif

#if COUNT_BYTE_AND_NEW_LINE
        if ((i + 1) % 32 == 0) {
            printf("    | %d Bytes\n", i + 1);
        }
#endif
    }
    printf("\n---------- %s End ----------\n", note);
}


void fatal_error(int line)
{
    ESP_LOGE(TAG, "task pended due to fatal error happen, line:%d", line);
    // mutex_delete(&mutex_conn_param);
    while (1) {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

static void tcp_client_task(void *pvParameter)
{
    /* Wait for the callback to set the CONNECTED_BIT in the
       event group.
    */
    ESP_LOGI(TAG, "Wait for ESP32 Connect to WiFi!");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "ESP32 Connected to WiFi ! Start TCP Server....");

#define SERVER_IP "192.168.111.101"
#define SERVER_PORT 80
#define RECV_BUF_SIZE 1024
#define SEND_BUF_SIZE 1024

while(1){
    vTaskDelay(2000 / portTICK_RATE_MS);
    int  sockfd = 0, iResult = 0;

    struct  sockaddr_in serv_addr;
    fd_set read_set, write_set, error_set;

    sockfd  =  socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        ESP_LOGE(TAG, "create socket failed!");
        continue;
    }

    memset( &serv_addr, 0 , sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    serv_addr.sin_addr.s_addr  =  inet_addr(SERVER_IP);

    int conn_ret = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    if (conn_ret == -1) {
        ESP_LOGE(TAG, "connect to server failed! errno=%d", errno);
        close(sockfd);
        continue;
    } else {
        ESP_LOGI(TAG, "connected to tcp server OK, sockfd:%d...", sockfd);
    }
    // set keep-alive
#if 1
    int keepAlive = 1;
    int keepIdle = 10;
    int keepInterval = 1;
    int keepCount = 5;
    int ret = 0;
    ret  = setsockopt(sockfd, SOL_SOCKET,  SO_KEEPALIVE, &keepAlive, sizeof(keepAlive));
    ret |= setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(keepIdle));
    ret |= setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(keepInterval));
    ret |= setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(keepCount));
    if (ret) {
        ESP_LOGE(TAG, "set socket keep-alive option failed...");
        close(sockfd);
        continue;
    }
    ESP_LOGI(TAG, "set socket option ok...");
#endif

    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    char* send_buf = (char*)calloc(SEND_BUF_SIZE, 1);
    char* recv_buf = (char*)calloc(SEND_BUF_SIZE, 1);
    if(send_buf == NULL || recv_buf == NULL ){
        ESP_LOGE(TAG, "alloc failed, reset chip...");
        esp_restart();
    }

    while  (1) {
        FD_ZERO( &read_set);
        FD_SET(sockfd,  &read_set);
        write_set = read_set;
        error_set = read_set;
        iResult = select(sockfd  + 1, &read_set, &write_set, &error_set, &timeout);
        if(iResult == -1){
            ESP_LOGE(TAG, "TCP client select failed");
            break;  // reconnect
        }
        if(iResult == 0){
            ESP_LOGI(TAG, "TCP client A select timeout occurred");
            continue;
        }
        if(FD_ISSET(sockfd , &error_set)){    // error happen
            ESP_LOGE(TAG, "select error_happend");
            break;
        }
        if(FD_ISSET(sockfd , &read_set)){    // readable
            int recv_ret  =  recv(sockfd, recv_buf, RECV_BUF_SIZE, 0);
            if(recv_ret > 0){
                //do more chores
                print_debug(recv_buf, recv_ret, "receive data");
            }else{
                ESP_LOGW(TAG, "close tcp transmit, would close socket...");
                break;
            }
        }
        if(FD_ISSET(sockfd , &write_set)){    // writable
            sprintf(send_buf, "{\"cmd_id\":1}");
            // send client data to tcp server
            print_debug(send_buf, strlen(send_buf), "send data");
            int send_ret = send(sockfd, send_buf, strlen(send_buf), 0);
            if (send_ret == -1) {
                ESP_LOGE(TAG, "send data to tcp server failed");
                break;
            } else {
                ESP_LOGI(TAG, "send data to tcp server succeeded");
            }
            vTaskDelay(1000/portTICK_RATE_MS);
        }

    }
    if(sockfd > 0){
        close(sockfd);
        ESP_LOGW(TAG, "close socket , sockfd:%d", sockfd);
    }

    free(send_buf);
    send_buf = NULL;
    free(recv_buf);
    recv_buf = NULL;
    ESP_LOGW(TAG, "reset tcp client and reconnect to tcp server...");
} // end
    printf( "A stop nonblock...\n");
    vTaskDelete(NULL);
    return;


}

void app_main()
{
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    initialise_wifi();

    xTaskCreate(&tcp_client_task, "tcp_client_task", 4096, NULL, 5, NULL);

    // new more task to do other chores
    while (1) {
        vTaskDelay(3000 / portTICK_RATE_MS);
        ESP_LOGI(TAG, "current heap size:%d", esp_get_free_heap_size());

    }
}
