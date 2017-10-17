/* for json parse demo based on TCP by CW

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

static char *sock_recv_data_buffer[MAX_CLIENT_NUMBER];
static char *sock_send_data_buffer[MAX_CLIENT_NUMBER];
static uint32_t receive_cmd_length[MAX_CLIENT_NUMBER];
static uint32_t parse_cmd_length[MAX_CLIENT_NUMBER];

static int got_ip_flag = 0;

static const char *TAG = "tcp server";

static tcp_mutex mutex_conn_param;
struct conn_param m_conn_param;

e_sample_errno current_state = PROCESS_INIT;

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

// create a mutex
int mutex_new(tcp_mutex *p_mutex)
{
    int xReturn = -1;
    *p_mutex = xSemaphoreCreateMutex();
    if (*p_mutex != NULL) {
        xReturn = 0;
    }

    return xReturn;
}

// lock a mutex, immediate return if mutex is available, blocking if mutex is not available
void mutex_lock(tcp_mutex *p_mutex)
{
    while (xSemaphoreTake(*p_mutex, portMAX_DELAY) != pdPASS);
}

// unlock a mutex
void mutex_unlock(tcp_mutex *p_mutex)
{
    xSemaphoreGive(*p_mutex);
}

// destroy a mutex
void mutex_delete(tcp_mutex *p_mutex)
{
    vSemaphoreDelete(*p_mutex);
}

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

// tcp server response the client
// server also can response in json format, but we would not
// because it is complex for tcp client to adopt the json lib to parse
int response_to_client(uint32_t cmd_id, int32_t sample_errno, int32_t index)
{
    ESP_LOGI(TAG, "sample errno:%d\n", sample_errno);
    int ret = 0;
    if (m_conn_param.sock_fd[index] < 0) {
        ESP_LOGE(TAG, "response to gateway failed");
        return -1;
    }
    char *respond_buffer = sock_send_data_buffer[index];


    memset(respond_buffer, 0, MAX_JSON_LEN + 4);
    memset(respond_buffer, 0xFFFFFFFF, sizeof(int));
    ret = strlen(respond_buffer);
    sprintf(respond_buffer + ret, "\"direction\": \"%s\",", "ESP32ToClient");
    ret = strlen(respond_buffer);
    sprintf(respond_buffer + ret, "\"errno\": %04d,", sample_errno);
    ret = strlen(respond_buffer);
    sprintf(respond_buffer + ret, "\"cmd_id\": %04d", cmd_id);
    ret = strlen(respond_buffer);
    int32_t net_len = htonl(ret);
    memcpy(respond_buffer, &net_len, sizeof(int));

    print_debug(respond_buffer, ret, "respond to client");

    if (0 >= write(m_conn_param.sock_fd[index], respond_buffer, ret)) {
        ESP_LOGE(TAG, "upload message to cloud failed");
        data_destroy(index);
        return -1;
    }
    return 0;
}

// parse json and set config and save config
int parse_json_data(int index)
{
#if TCP_STREAM_HEAD_CHECK
    char *buffer = sock_recv_data_buffer[index] + 4;
#else
    char *buffer = sock_recv_data_buffer[index];
#endif

    cJSON *root, *item, *value_item;
    int value_int = 0;
    uint32_t cmd_id = 0xffffffff;
    char *value_str = NULL;
    e_sample_errno sample_errno =  SAMPLE_OK;
    ESP_LOGI(TAG, "parse data:%s", buffer);

    root = cJSON_Parse((char *)buffer);
    if (!root) {
        ESP_LOGE(TAG, "Error before: [%s]", cJSON_GetErrorPtr());
        response_to_client(cmd_id, JSON_PARSE_ROOT_FAILED, index); // Parse json root failed
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
            response_to_client(cmd_id, sample_errno, index);
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
            response_to_client(cmd_id, current_state, index);

        } else if (0 == strncmp(item->string, "sample_para", sizeof("sample_para") )) {
            value_item = cJSON_GetObjectItem(item, "value");
            if (value_item) {
                value_str = value_item->valuestring;
                ESP_LOGI(TAG, "set:%s", value_str);
            } else {
                response_to_client(cmd_id, JSON_PARSE_SAMPLE_PARAMETER_FAILED, index);
                sample_errno = JSON_PARSE_SAMPLE_PARAMETER_FAILED;
                break;
            }
            current_state = PROCESS_SAMPLE_PARA + (atoi(value_str) & 0xff);
            // TODO: Config the sample parameter...

        } else {
            ESP_LOGE(TAG, "cannot parse JSON Item:%d", i);
            sample_errno = JSON_PARSE_NO_ITEM;
            response_to_client(cmd_id, JSON_PARSE_NO_ITEM, index); // Error reponse
            break;

        }
    }

    if (sample_errno == SAMPLE_OK) {
        response_to_client(cmd_id, SAMPLE_OK, index);
    }
    // memory release
    cJSON_Delete(root);
    return sample_errno;
}

void fatal_error(int line)
{
    ESP_LOGE(TAG, "task pended due to fatal error happen, line:%d", line);
    // mutex_delete(&mutex_conn_param);
    while (1) {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

static void tcp_server_task(void *pvParameter)
{
    /* Wait for the callback to set the CONNECTED_BIT in the
       event group.
    */
    ESP_LOGI(TAG, "Wait for ESP32 Connect to WiFi!");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "ESP32 Connected to WiFi ! Start TCP Server....");

    ESP_LOGI(TAG, "date:%s time:%s", __DATE__, __TIME__);   // compile time

    while (1) {
        while (!got_ip_flag) {
            esp_wifi_connect();
            vTaskDelay(2000 / portTICK_RATE_MS);
            continue;
        }

        tcpip_adapter_ip_info_t esp32_sta_ip;
        memset(&esp32_sta_ip, 0, sizeof(tcpip_adapter_ip_info_t));
        if (tcpip_adapter_get_ip_info(ESP_IF_WIFI_STA, &esp32_sta_ip) == 0) {
            ESP_LOGI(TAG, "~~~~~~~~~~~");
            ESP_LOGI(TAG, "IP:"IPSTR, IP2STR(&esp32_sta_ip.ip));
            ESP_LOGI(TAG, "MASK:"IPSTR, IP2STR(&esp32_sta_ip.netmask));
            ESP_LOGI(TAG, "GW:"IPSTR, IP2STR(&esp32_sta_ip.gw));
            ESP_LOGI(TAG, "~~~~~~~~~~~");
        } else {
            ESP_LOGE(TAG, "get ip info failed");
            continue;
        }
        ESP_LOGI(TAG, "got ip: %s\n", inet_ntoa(esp32_sta_ip.ip) );

        int server_socket = -1;
        server_socket = socket(PF_INET, SOCK_STREAM, 0);
        ESP_LOGI(TAG, "server socket %d", server_socket);
        if (-1 == server_socket) {
            close(server_socket);
            server_socket = -1;
            ESP_LOGE(TAG, "socket fail!");
            continue;
        }

        struct sockaddr_in my_addr;
        bzero(&my_addr, sizeof(struct sockaddr_in));
        memcpy(&my_addr.sin_addr.s_addr, &esp32_sta_ip.ip, 4);
        my_addr.sin_family = AF_INET;
        my_addr.sin_len = sizeof(my_addr);
        my_addr.sin_port = htons(DEFAULT_TCP_SERVER_PORT);

        if (bind(server_socket, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) {
            ESP_LOGE(TAG, "bind server_socket failed...");
            close(server_socket);
            server_socket = -1;
            vTaskDelay(1000 / portTICK_RATE_MS);
            continue;
        }

        ESP_LOGI(TAG, "bind ok");

        if (listen(server_socket, MAX_CLIENT_NUMBER + 1) == -1) {
            ESP_LOGE(TAG, "listen server_socket failed...");
            close(server_socket);
            server_socket = -1;
            vTaskDelay(1000 / portTICK_RATE_MS);
            continue;
        }

        ESP_LOGI(TAG, "listen ok...");

        int keepAlive = 1;
        int keepIdle = 10;
        int keepInterval = 1;
        int keepCount = 5;
        int ret = 0;
        ret  = setsockopt(server_socket, SOL_SOCKET,  SO_KEEPALIVE, &keepAlive, sizeof(keepAlive));
        ret |= setsockopt(server_socket, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(keepIdle));
        ret |= setsockopt(server_socket, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(keepInterval));
        ret |= setsockopt(server_socket, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(keepCount));
        if (ret) {
            ESP_LOGE(TAG, "set socket option failed...");
            close(server_socket);
            server_socket = -1;
            continue;
        }

        ESP_LOGI(TAG, "set socket option ok...");

        int client_fd = 0;
        socklen_t  addr_len;
        addr_len = sizeof(struct sockaddr_in);
        struct sockaddr_in tcp_client_addr;

        while (1) {
            if (server_socket == -1) {
                ESP_LOGW(TAG, "server socket error happened...");
                break;
            }

            ESP_LOGI(TAG, "prepare to receive new client...");

            bzero(&tcp_client_addr, sizeof(struct sockaddr_in));
            if ((client_fd = accept(server_socket, (struct sockaddr *)&tcp_client_addr, &addr_len)) == -1) {
                ESP_LOGE(TAG, "accept error: return -1");
                close(server_socket);
                server_socket = -1;
                continue;
            }
            if (client_fd < 0) {
                ESP_LOGE(TAG, "accept error: return %d", client_fd);
                close(server_socket);
                server_socket = -1;
                continue;
            } else {
                ESP_LOGI(TAG, "received new tcp client OK, client fd:%d ...", client_fd);
                // do more control on tcp client
                // such as setsockopt on tcp client, such as filter ip and so on by client fd
                ESP_LOGI(TAG, "tcp client ip:%s port:%d\n", \
                         inet_ntoa(tcp_client_addr.sin_addr.s_addr), ntohs(tcp_client_addr.sin_port));

                mutex_lock(&mutex_conn_param);
                int32_t index = 0;
                for (index = 0; index < MAX_CLIENT_NUMBER; ++index) {
                    if (m_conn_param.sock_fd[index] < 0) {
                        break;
                    }
                }

                if (index < MAX_CLIENT_NUMBER) {
                    if (conn_prepare(index) != 0) {
                        ESP_LOGE(TAG, "ESP32 error happened!");
                        fatal_error(__LINE__);
                    }

                    m_conn_param.conn_num++;
                    m_conn_param.sock_fd[index] = client_fd;
                    ESP_LOGI(TAG, "tcp server accept index:%d, socket fd:%d\n", index, client_fd);
// set keep-alive for every tcp client
#if 1
                    int keepAlive = 1;
                    int keepIdle = 10;
                    int keepInterval = 1;
                    int keepCount = 3;
                    int ret = 0;
                    ret  = setsockopt(client_fd, SOL_SOCKET,  SO_KEEPALIVE, &keepAlive, sizeof(keepAlive));
                    ret |= setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(keepIdle));
                    ret |= setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(keepInterval));
                    ret |= setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(keepCount));
                    if (ret) {
                        ESP_LOGE(TAG, "set socket option failed...");
                        close(client_fd);
                        server_socket = -1;
                    }
                    ESP_LOGI(TAG, "set client socket option ok...");
#endif
                } else {
                    close(client_fd);
                    ESP_LOGW(TAG, "tcp server too much connection..\n");
                }
                mutex_unlock(&mutex_conn_param);
            }

        }   // end accept while
        ESP_LOGI(TAG, "restart tcp server...");
    }   // end main tcp while
    ESP_LOGW(TAG, "deleting ESP32 tcp server task...");
    vTaskDelete(NULL);
}

void handle_read_event(int index)
{
    int ret = 0;
    ret = recv(m_conn_param.sock_fd[index], sock_recv_data_buffer[index], MAX_JSON_LEN + 4, 0);

    if (ret > 0) {
        print_debug(sock_recv_data_buffer[index], ret, "receive data");
        //
#if TCP_STREAM_HEAD_CHECK
        receive_cmd_length[index] += ret;
        if (receive_cmd_length[index] >= 4 && parse_cmd_length[index] == 0) {   // to parse head length information
            parse_cmd_length[index] = ntohl(*( (int32_t *)sock_recv_data_buffer[index] ));
            ESP_LOGI(TAG, "parse cmd length:%d", parse_cmd_length[index]);
            if (parse_cmd_length[index] > (MAX_JSON_LEN + 4)) {
                response_to_client(0xffffffff, TCP_REV_PARSE_CMD_BEYOND_MAX_JSON_LEN, index); // parse incorrect length
                receive_cmd_length[index] = 0;
                parse_cmd_length[index] = 0;
                memset(sock_recv_data_buffer[index] , 0, MAX_JSON_LEN + 4);
                memset(sock_send_data_buffer[index] , 0, MAX_JSON_LEN + 4);
            }
        }
        ESP_LOGI(TAG, "tcp server receive tcp packet len:%d[total receive len:%d]", ret, receive_cmd_length[index]);


        if ((receive_cmd_length[index] == parse_cmd_length[index]) && (parse_cmd_length[index] != 0) ) {      // one json command total received, ready to parse
            if ( parse_json_data(index) == SAMPLE_OK ) { // to parse cmd and respond to client
                receive_cmd_length[index] = 0;
                parse_cmd_length[index] = 0;
                memset(sock_recv_data_buffer[index] , 0, MAX_JSON_LEN + 4);
                memset(sock_send_data_buffer[index] , 0, MAX_JSON_LEN + 4);
            } else {
                ESP_LOGE(TAG, "had reset current socket resource...");
            }
        } else if (receive_cmd_length[index] > parse_cmd_length[index]) {   // incorrect length, restart to receive new tcp packet
            response_to_client(0xffffffff, TCP_REV_PARSE_CMD_LEN_ERROR, index);
            receive_cmd_length[index] = 0;
            parse_cmd_length[index] = 0;
            memset(sock_recv_data_buffer[index] , 0, MAX_JSON_LEN + 4);
            memset(sock_send_data_buffer[index] , 0, MAX_JSON_LEN + 4);

        } else {     // continue to receive rest tcp packet
            ESP_LOGI(TAG, "wait for more tcp packet for a completely JSON data");
        }
        //
#else

        if ( parse_json_data(index) == SAMPLE_OK ) {
            memset(sock_recv_data_buffer[index] , 0, MAX_JSON_LEN + 4);
            memset(sock_send_data_buffer[index] , 0, MAX_JSON_LEN + 4);
        } else {
            ESP_LOGE(TAG, "had reset current socket resource...");
        }
#endif
    } else {
        data_destroy(index);
    }
}

static void tcp_recv_task(void *pvParameter)
{
    int32_t result = 0;
    int32_t index = 0;
    int32_t max_fd = 0;
    fd_set read_set;
    fd_set error_set;
    struct timeval timeout;

    while (1) {

        while (m_conn_param.conn_num == 0) {
            vTaskDelay(1000 / portTICK_RATE_MS);
        }
        mutex_lock(&mutex_conn_param);
        ESP_LOGI(TAG, "conn num:%d, select on connected socket...", m_conn_param.conn_num);
        FD_ZERO(&read_set);
        FD_ZERO(&error_set);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        for (index = 0; index < MAX_CLIENT_NUMBER; index++) {
            if (m_conn_param.sock_fd[index] >= 0) {
                FD_SET(m_conn_param.sock_fd[index], &read_set);
                FD_SET(m_conn_param.sock_fd[index], &error_set);
                max_fd = max(m_conn_param.sock_fd[index], max_fd);
            }
        }

        result = select(max_fd + 1, &read_set, NULL, &error_set, &timeout);

        // lock before every possible modify

        if (result == -1) {
            ESP_LOGE(TAG, "select error, return -1, errno:%d", errno);
        } else if (result == 0) {
            ESP_LOGI(TAG, "select timeout");
        } else if (result > 0) {
            for (index = 0; index < MAX_CLIENT_NUMBER; ++index) {
                if (FD_ISSET(m_conn_param.sock_fd[index], &error_set)) { // keep-alive timeout or lose connect and so on
                    ESP_LOGE(TAG, "error set found, socketfd:%d, errno:%d", m_conn_param.sock_fd[index], errno);
                    data_destroy(index);
                } else if (FD_ISSET(m_conn_param.sock_fd[index], &read_set)) {
                    handle_read_event(index);
                }
            }
        } else {
            ESP_LOGE(TAG, "WTF");
        }
        mutex_unlock(&mutex_conn_param);
    }


    vTaskDelete(NULL);
}

void data_init()
{

    mutex_new(&mutex_conn_param);

    m_conn_param.conn_num = 0;
    int32_t index = 0;
    for (index = 0; index < MAX_CLIENT_NUMBER; ++index) {
        m_conn_param.sock_fd[index] = -1;
        sock_recv_data_buffer[index] = NULL;
        sock_send_data_buffer[index] = NULL;
    }

#if TCP_STREAM_HEAD_CHECK
    for (index = 0; index < MAX_CLIENT_NUMBER; ++index) {
        receive_cmd_length[index] = 0;
        parse_cmd_length[index] = 0;
    }

#endif
}

void data_destroy(int32_t index)
{
    close(m_conn_param.sock_fd[index]);
    m_conn_param.sock_fd[index] = -1;
    m_conn_param.conn_num--;
    free(sock_recv_data_buffer[index]);
    free(sock_send_data_buffer[index]);
    sock_recv_data_buffer[index] = NULL;
    sock_send_data_buffer[index] = NULL;
    ESP_LOGI(TAG, "destroy current socket resource..");
}

int conn_prepare(int index)
{

    if (!(sock_recv_data_buffer[index] == NULL && sock_send_data_buffer[index] == NULL)) {
        ESP_LOGE(TAG, "memory leak...");
        return -1;
    }
    sock_recv_data_buffer[index] = (char *)malloc(MAX_JSON_LEN + 4);
    sock_send_data_buffer[index] = (char *)malloc(MAX_JSON_LEN + 4);

    if (sock_recv_data_buffer[index] == NULL || sock_send_data_buffer[index] == NULL) {
        ESP_LOGE(TAG, "malloc failed...");
        return -1;
    } else {
        memset(sock_recv_data_buffer[index] , 0, MAX_JSON_LEN + 4);
        memset(sock_send_data_buffer[index] , 0, MAX_JSON_LEN + 4);
    }
    return 0;
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

    data_init();
    // tcp server task will start tcp server and receive new tcp client
    xTaskCreate(&tcp_server_task, "tcp_server_task", 4096, NULL, 5, NULL);
    // tcp recv task will handle every tcp client event
    xTaskCreate(&tcp_recv_task, "tcp_recv_task", 4096, NULL, 5, NULL);

    // new more task to do other chores
    while (1) {
        vTaskDelay(3000 / portTICK_RATE_MS);
        ESP_LOGI(TAG, "current heap size:%d", esp_get_free_heap_size());

    }
}
