/* for json parse sample by CW

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
#include "cJSON.h"
#include "tcp_server.h"

#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

static int client_fd = -1;
static int old_client_fd = -1;
static uint8_t *pusrdata = NULL;
static int server_socket = -1;
static int ret = 0;
static int got_ip_flag = 0;
e_sample_errno current_state = PROCESS_INIT;
static const char *TAG = "tcp server";

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
#define COUNT_BYTE_AND_NEW_LINE 1
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

// tcp server response the client
// server also can response in json format, but we would not
// because it is complex for tcp client to adopt the json lib to parse
void response_to_client(uint32_t cmd_id, int32_t sample_errno)
{
    ESP_LOGI(TAG, "sample errno:%d\n", sample_errno);
    int ret = 0;
    if (old_client_fd < 0) {
        ESP_LOGE(TAG, "response to gateway failed");
        return;
    }
    static char *respond_buffer = NULL;
    static int malloc_flag = 0;
    if (!malloc_flag) {
        respond_buffer = (char *)malloc(MAX_JSON_LEN);      // avoid malloc/free frequency
        if (respond_buffer == NULL) {
            ESP_LOGE(TAG, "lack of memory,malloc error!");
            return;
        }
        malloc_flag = 1;
    }

    memset(respond_buffer, 0, MAX_JSON_LEN);
    memset(respond_buffer, 0xFFFFFFFF, sizeof(int));
    ret = strlen(respond_buffer);
    sprintf(respond_buffer + ret, "\"direction\": \"%s\",", "ESP32ToClient");
    ret = strlen(respond_buffer);
    sprintf(respond_buffer + ret, "\"errno\": %04d,", sample_errno);
    ret = strlen(respond_buffer);
    sprintf(respond_buffer + ret, "\"cmd_id\": %04d}", cmd_id);
    ret = strlen(respond_buffer);
    int32_t net_len = htonl(ret);
    memcpy(respond_buffer, &net_len, sizeof(int));

    print_debug(respond_buffer, ret, "respond to client");
    if (old_client_fd < 0) {
        ESP_LOGE(TAG, "old_client_fd < 0");
        return;
    }
    if (0 >= write(old_client_fd, respond_buffer, ret)) {
        ESP_LOGE(TAG, "upload message to cloud failed");
        close(old_client_fd);
        old_client_fd = -1;
        return;
    }
    // free(respond_buffer);
    // respond_buffer = NULL;
}

// parse json and set config and save config
int parse_gw_json_normal_data(uint8_t *buffer)
{
    cJSON *root, *item, *value_item;
    int value_int = 0, ret = 0;
    uint32_t cmd_id = 0xffffffff;
    char *value_str = NULL;
    e_sample_errno sample_errno =  SAMPLE_OK;

    ESP_LOGI(TAG, "parse data:%s", buffer);
    root = cJSON_Parse((char *)buffer);
    if (!root) {
        ESP_LOGE(TAG, "Error before: [%s]", cJSON_GetErrorPtr());
        response_to_client(cmd_id, JSON_PARSE_ROOT_FAILED); // Parse json root failed
        return -1;
    }
    int json_item_num = cJSON_GetArraySize(root);
    ESP_LOGI(TAG, "Total JSON Items:%d", json_item_num);

    int32_t i = 0;
    for (i = 0; i < json_item_num; ++i) {
        ESP_LOGI(TAG, "Start Parse JSON Items:%d", i);
        item = cJSON_GetArrayItem(root, i);
        if (!item) {                            // parse item failed, reponse error code : -i * 100
            sample_errno = -i * 100;
            response_to_client(cmd_id, sample_errno);
            break;
        }
        ESP_LOGI(TAG, "parse JSON Items:%d found", i);

        // set every item config
        ESP_LOGI(TAG, "item<%s>", item->string);

        if (0 == strncmp(item->string, "cmd_id", sizeof("cmd_id") )) {
            cmd_id = item->valueint;
            ESP_LOGI(TAG, "parsed cmd_id:%d", cmd_id);
            // TODO: config cmd_id para

        } else if (0 == strncmp(item->string, "lookup_state", sizeof("lookup_state") )) { // lookup config by current_state
            response_to_client(cmd_id, current_state);

        } else if (0 == strncmp(item->string, "sample_para", sizeof("sample_para") )) {
            value_item = cJSON_GetObjectItem(item, "value");
            if (value_item) {
                value_str = value_item->valuestring;
                ESP_LOGI(TAG, "set:%s", value_str);
            } else {
                response_to_client(cmd_id, JSON_PARSE_SAMPLE_PARAMETER_FAILED);
                return -1;
            }
            current_state = PROCESS_SAMPLE_PARA + (atoi(value_str) & 0xff);
            // TODO: Config the sample parameter...

        } else if (0 == strncmp(item->string, "local_update", sizeof("local_update") )) {
            value_item = cJSON_GetObjectItem(item, "ssid");
            if (value_item) {
                value_str = value_item->valuestring;
                ESP_LOGI(TAG, "local ssid:%s", value_str);
            } else {
                response_to_client(cmd_id, JSON_PARSE_SSID_FAILED);
                return -1;
            }
            value_item = cJSON_GetObjectItem(item, "password");
            if (value_item) {
                value_str = value_item->valuestring;
                ESP_LOGI(TAG, "local password:%s", value_str);
            } else {
                response_to_client(cmd_id, JSON_PARSE_PASSWD_FAILED);
                return -1;
            }
            value_item = cJSON_GetObjectItem(item, "ota_server_addr");
            if (value_item) {
                value_str = value_item->valuestring;
                ESP_LOGI(TAG, "local server addr:%s", value_str);
            } else {
                response_to_client(cmd_id, JSON_PARSE_OTA_SERVER_ADDR_FAILED);
                return -1;
            }
            value_item = cJSON_GetObjectItem(item, "ota_server_port");
            if (value_item) {
                value_int = value_item->valueint;
                ESP_LOGI(TAG, "local port:%d", value_int);

            } else {
                response_to_client(cmd_id, JSON_PARSE_OTA_SERVER_PORT_FAILED);
                return -1;
            }
            // do OTA
            current_state = PROCESS_LOCAL_UPDATE;
        } else {
            ESP_LOGE(TAG, "cannot parse JSON Item:%d", i);
            response_to_client(cmd_id, JSON_PARSE_NO_ITEM); // Error reponse
            return -1;
        }
    }
    response_to_client(old_client_fd, SAMPLE_OK);
    return SAMPLE_OK;
}


void tcp_client_handle(void *pvParameters)
{
    ESP_LOGI(TAG, "accept new client,client fd: %d", old_client_fd);
    while (1) {
        memset(pusrdata, 0, MAX_JSON_LEN);
        ret = 0;
        uint32_t receive_cmd_length = 0;   // current total receive length
        uint32_t parse_cmd_length = 0;     // parse length from first 4 bytes
        e_sample_errno sample_errno = SAMPLE_OK;
        while (1) {
            ESP_LOGI(TAG, "prepare to receive next packet!");
            ESP_LOGI(TAG, "old_client_fd:%d receive_cmd_length:%d", old_client_fd, receive_cmd_length);
            ret = recv(old_client_fd, pusrdata + receive_cmd_length, MAX_JSON_LEN - receive_cmd_length, 0);
            ESP_LOGI(TAG, "TCP server received the packet!");
            if (ret < 0) { // lose connection
                sample_errno = TCP_RCV_RET_NEGATIVE;
                ESP_LOGE(TAG, "ret < 0");
                break;
            } else if (ret == 0) {       // Client close socket actively
                sample_errno = TCP_RCV_RET_ZERO;
                ESP_LOGW(TAG, "ret = 0");
                break;
            } else {
                receive_cmd_length += ret;
                if (receive_cmd_length >= 4 && parse_cmd_length == 0) {
                    parse_cmd_length = ntohl(*( (int32_t *)pusrdata ));
                    ESP_LOGI(TAG, "parse cmd length:%d", parse_cmd_length);
                    if (parse_cmd_length > MAX_JSON_LEN) {
                        response_to_client(0xffffffff, TCP_REV_PARSE_CMD_BEYOND_MAX_JSON_LEN); // parse incorrect length
                        receive_cmd_length = 0;
                        parse_cmd_length = 0;
                        sample_errno = SAMPLE_OK;
                        continue;
                    }
                }
                ESP_LOGI(TAG, "tcp server receive tcp packet len:%d[total receive len:%d]", ret, receive_cmd_length);
            }

            if ((receive_cmd_length == parse_cmd_length) && (parse_cmd_length != 0) ) {      // one json command total received, ready to parse
                break;
            } else if (receive_cmd_length > parse_cmd_length) {
                response_to_client(0xffffffff, TCP_REV_PARSE_CMD_LEN_ERROR); // incorrect length, restart to receive new tcp packet
                receive_cmd_length = 0;
                parse_cmd_length = 0;
                sample_errno = SAMPLE_OK;
                continue;
            } else {     // continue to receive rest tcp packet
                continue;
            }
        }

        if ( ret <= 0) {
            ESP_LOGW(TAG, "tcp receive data over, close client");
            response_to_client(0xffffffff, sample_errno);
            close(old_client_fd);
            old_client_fd = -1;
            break;
        } else {
            ESP_LOGI(TAG, "receive JSON command, start to parse data");
            parse_gw_json_normal_data(pusrdata + 4);

        }
        ESP_LOGI(TAG, "normal parse over");
    }
    while (1) {
        ESP_LOGI(TAG, "wait for task delete...");
        vTaskDelay(1000);
    }
}

static void tcp_server_task(void *pvParameter)
{
    /* Wait for the callback to set the CONNECTED_BIT in the
       event group.
    */
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "ESP32 Connected to WiFi ! Start TCP Server....");
    struct sockaddr_in my_addr;
    tcpip_adapter_ip_info_t client_ip;
    memset(&client_ip, 0, sizeof(tcpip_adapter_ip_info_t));

    pusrdata = (uint8_t *)malloc(MAX_JSON_LEN + 4);
    if (!pusrdata) {
        ESP_LOGE(TAG, "tcp_server_task:%d malloc memory failed", __LINE__);
        return;
    }

    ESP_LOGI(TAG, "date-time:%s %s", __DATE__, __TIME__);

    while (1) {
        while (!got_ip_flag) {
            esp_wifi_connect();
            vTaskDelay(2000 / portTICK_RATE_MS);
            break;
        }
        server_socket = socket(PF_INET, SOCK_STREAM, 0);
        ESP_LOGI(TAG, "server socket %d", server_socket);
        if (-1 == server_socket) {
            close(server_socket);
            server_socket = -1;
            ESP_LOGE(TAG, "socket fail!");
            continue;
        }

        if (tcpip_adapter_get_ip_info(ESP_IF_WIFI_STA, &client_ip) == 0) {
            ESP_LOGI(TAG, "~~~~~~~~~~~");
            ESP_LOGI(TAG, "IP:"IPSTR, IP2STR(&client_ip.ip));
            ESP_LOGI(TAG, "MASK:"IPSTR, IP2STR(&client_ip.netmask));
            ESP_LOGI(TAG, "GW:"IPSTR, IP2STR(&client_ip.gw));
            ESP_LOGI(TAG, "~~~~~~~~~~~");
        } else {
            ESP_LOGE(TAG, "get ip info failed");
        }
        ESP_LOGI(TAG, "got ip: %s\n", inet_ntoa(client_ip.ip) );
        bzero(&my_addr, sizeof(struct sockaddr_in));
        memcpy(&my_addr.sin_addr.s_addr, &client_ip.ip, 4);
        my_addr.sin_family = AF_INET;
        my_addr.sin_len = sizeof(my_addr);
        my_addr.sin_port = htons(DEFAULT_GWSERVER_PORT);

        if (bind(server_socket, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) {
            ESP_LOGE(TAG, "bind fail!");
            close(server_socket);
            server_socket = -1;
            vTaskDelay(1000 / portTICK_RATE_MS);
            continue;
        }
        ESP_LOGI(TAG, "bind ok");
        if (listen(server_socket, 5) == -1) {
            ESP_LOGI(TAG, "listen fail!");
            close(server_socket);
            server_socket = -1;
            vTaskDelay(1000 / portTICK_RATE_MS);
            continue;
        }
        ESP_LOGI(TAG, "listen ok");

        int keepAlive = 1;
        int keepIdle = 5;
        int keepInterval = 1;
        int keepCount = 3;
        ret  = setsockopt(server_socket, SOL_SOCKET,  SO_KEEPALIVE, &keepAlive, sizeof(keepAlive));
        ret |= setsockopt(server_socket, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(keepIdle));
        ret |= setsockopt(server_socket, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(keepInterval));
        ret |= setsockopt(server_socket, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(keepCount));
        if (ret) {
            close(server_socket);
            server_socket = -1;
            continue;
        }
        ESP_LOGI(TAG, "set socket option ok");

        while (1) {
            socklen_t  addrlen;
            addrlen = sizeof(struct sockaddr_in);
            ESP_LOGI(TAG, "prepare to receive new client");
            while (1) {
                if ((client_fd = accept(server_socket, (struct sockaddr *)&my_addr, &addrlen)) == -1) {
                    ESP_LOGI(TAG, "accept error client");
                    close(server_socket);
                    server_socket = -1;
                    break;
                }
                static xTaskHandle client_handle = NULL;
                if (old_client_fd >= 0) {
                    vTaskDelay(50);
                    close(old_client_fd);
                    old_client_fd = -1;
                    if (NULL != client_handle) {
                        ESP_LOGI(TAG, "Old Task Deleting");
                        vTaskDelete(client_handle);
                        client_handle = NULL;
                        ESP_LOGI(TAG, "Old Task Deleted");
                    }
                }
                old_client_fd = client_fd;
                xTaskCreate(tcp_client_handle, "tcp_client_handle", 8192, NULL, 1, &client_handle);
            }
        }
    }
    free(pusrdata);
    pusrdata = NULL;
    vTaskDelete(NULL);
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
    xTaskCreate(&tcp_server_task, "tcp_server_task", 8192, NULL, 5, NULL);

    while (1) {
        vTaskDelay(2000 / portTICK_RATE_MS);
    }
}
