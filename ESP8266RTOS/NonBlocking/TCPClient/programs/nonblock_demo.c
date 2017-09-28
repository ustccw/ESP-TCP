#include <stddef.h>

#include "../include/nonblock_demo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "espressif/c_types.h"
#include "lwip/sockets.h"

#include <stddef.h>
#include "openssl/ssl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "espressif/c_types.h"

#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/apps/sntp.h"
#include "lwip/apps/sntp_time.h"
#include "json/cJSON.h"
#include "user_config.h"

#include "upgrade.h"

#define NONBLOCK_DEMO_THREAD_NAMEA "tcp_clientA_demo"
#define NONBLOCK_DEMO_THREAD_NAMEB "tcp_clientB_demo"

#define NONBLOCK_DEMO_THREAD_STACK_WORDS 2048
#define NONBLOCK_DEMO_THREAD_PRORIOTY 6

//#define NONBLOCK_DEMO_FRAGMENT_SIZE 8192

//#define NONBLOCK_DEMO_LOCAL_TCP_PORT 1000

//#define NONBLOCK_DEMO_TARGET_NAME "www.baidu.com"
//#define NONBLOCK_DEMO_TARGET_TCP_PORT 443

//#define NONBLOCK_DEMO_REQUEST "{\"path\": \"/v1/ping/\", \"method\": \"GET\"}\r\n"

#define NONBLOCK_DEMO_RECV_BUF_LEN 256
static const char* TAG = "nonblock";
LOCAL xTaskHandle NONBLOCK_handle;
// sprintf(http_request, "GET %s HTTP/1.1\r\nHost: %s:%s \r\n\r\n", EXAMPLE_FILENAME, EXAMPLE_SERVER_IP, EXAMPLE_SERVER_PORT);
LOCAL char send_data[] = "GET /hello.txt HTTP/1.1\r\nHost: 192.168.111.100:1111 \r\n\r\n";
LOCAL int send_bytes = sizeof(send_data);

LOCAL char recv_buf[NONBLOCK_DEMO_RECV_BUF_LEN];


static int lwip_net_errno(int fd)
{
    int sock_errno = 0;
    u32_t optlen = sizeof(sock_errno);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &sock_errno, &optlen);
    return sock_errno;
}

static void lwip_set_non_block(int fd)
{
    int flags = -1;
    int error = 0;

    while (1) {
        flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1) {
            error = lwip_net_errno(fd);
            printf("set nonblock failed,errno:%d\n", error);
            if (error != EINTR) {
                break;
            }
        } else {
            printf("set nonblock succeed,error:%d\n", error);
            break;
        }
    }

    while (1) {
        flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        if (flags == -1) {
            error = lwip_net_errno(fd);
            printf("set nonblock failed,error:%d\n", error);
            if (error != EINTR) {
                break;
            }
        } else {
            printf("set nonblock succeed,error:%d\n", error);
            break;
        }
    }

}

static void task_error_handle(int line){
  os_printf("error line:%d\n", line);
  vTaskDelete(NULL);
}

LOCAL void tcp_clientA_thread(void *p)
{
    printf("TCP Client A thread Start...\n");
# define SERVER_IP_A "192.168.111.100"
# define SERVER_PORT_A 8080
#define RECV_BUF_SIZE 1024

    int  sockfd, iResult;
    char  buf[RECV_BUF_SIZE];
    struct  sockaddr_in serv_addr;
    fd_set read_set, write_set, error_set;

    sockfd  =  socket(AF_INET, SOCK_STREAM,  0 );
    lwip_set_non_block(sockfd);

    memset( &serv_addr,  0 , sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT_A);
    serv_addr.sin_addr.s_addr  =  inet_addr(SERVER_IP_A);

    connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    while  (1) {
        FD_ZERO( &read_set);
        FD_SET(sockfd,  &read_set);
        write_set = read_set;
        error_set = read_set;
        iResult = select(sockfd  + 1, &read_set, &write_set, &error_set, &timeout);
        if(iResult == -1){
            printf("TCP client A select failed\n");
            break;
        }
        if(iResult == 0){
            printf("TCP client A select timeout occurred\n");
            continue;
        }
        if(FD_ISSET(sockfd , &read_set)){    // readable
            do{
                printf("A receive data:\n");
                iResult  =  recv(sockfd, buf, RECV_BUF_SIZE,  MSG_DONTWAIT);
                printf("%s\n", buf);
            }while(iResult > 0);
            printf("A read buffer over\n");
        }
        if(FD_ISSET(sockfd , &write_set)){    // writable
            printf("A writable\n");
            vTaskDelay(1000/portTICK_RATE_MS);
        }
        if(FD_ISSET(sockfd , &error_set)){    // error happen
            printf("A error_happend\n");
        }
    }

    printf( "A stop nonblock...\n");
    vTaskDelete(NULL);
    return;
}

LOCAL void tcp_clientB_thread(void *p)
{
    printf("TCP Client B thread Start...\n");
#define SERVER_IP_B "192.168.111.100"
#define SERVER_PORT_B 8080
#define RECV_BUF_SIZE 1024

    int  sockfd, iResult;
    char  buf[RECV_BUF_SIZE];
    struct  sockaddr_in serv_addr;
    fd_set read_set, write_set, error_set;

    sockfd  =  socket(AF_INET, SOCK_STREAM,  0 );
    lwip_set_non_block(sockfd);

    memset( &serv_addr,  0 , sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT_B);
    serv_addr.sin_addr.s_addr  =  inet_addr(SERVER_IP_B);

    connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    while  (1) {
        FD_ZERO( &read_set);
        FD_SET(sockfd,  &read_set);
        write_set = read_set;
        error_set = read_set;
        iResult = select(sockfd  + 1, &read_set, &write_set, &error_set, &timeout);
        if(iResult == -1){
            printf("TCP client B select failed\n");
            break;
        }
        if(iResult == 0){
            printf("TCP client B select timeout occurred\n");
            continue;
        }
        if(FD_ISSET(sockfd , &read_set)){    // readable
            do{
                printf("B receive data:\n");
                iResult  =  recv(sockfd, buf, RECV_BUF_SIZE,  MSG_DONTWAIT);
                printf("%s\n", buf);
            }while(iResult > 0);
            printf("B read buffer over, close socket now\n");
            close(sockfd);
            break;
        }
        if(FD_ISSET(sockfd , &write_set)){    // writable
            printf("B writable\n");
            vTaskDelay(1000/portTICK_RATE_MS);
        }
        if(FD_ISSET(sockfd , &error_set)){    // error happen
            printf("B error_happend\n");
        }
    }

    printf( "B stop nonblock...\n");
    vTaskDelete(NULL);
    return ;
}

void user_conn_init(void)
{
    int ret;
    printf("start create thread...\n");
#if 1
    ret = xTaskCreate(tcp_clientA_thread,
                      NONBLOCK_DEMO_THREAD_NAMEA,
                      NONBLOCK_DEMO_THREAD_STACK_WORDS,
                      NULL,
                      NONBLOCK_DEMO_THREAD_PRORIOTY,
                      &NONBLOCK_handle);
    if (ret != pdPASS)  {
        os_printf("create thread %s failed\n", NONBLOCK_DEMO_THREAD_NAMEA);
        return ;
    }
#endif

#if 1
    ret = xTaskCreate(tcp_clientB_thread,
                      NONBLOCK_DEMO_THREAD_NAMEB,
                      NONBLOCK_DEMO_THREAD_STACK_WORDS,
                      NULL,
                      NONBLOCK_DEMO_THREAD_PRORIOTY,
                      &NONBLOCK_handle);
    if (ret != pdPASS)  {
        os_printf("create thread %s failed\n", NONBLOCK_DEMO_THREAD_NAMEB);
        return ;
    }
#endif


    vTaskDelete(NULL);
}

