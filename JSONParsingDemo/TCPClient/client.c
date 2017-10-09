#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
  
#define MAXLINE 4096  

void  debug_print_data(const char* data, const uint32_t len, const char* note){
#define COUNT_BYTE_AND_NEW_LINE 0
#define ALL_BINARY_SHOW 0
    printf("\n********** %s [len:%d] Start Addr:%p **********\n", note, len, data);
    int i = 0;
    for (i = 0; i < len; ++i){
#if !(ALL_BINARY_SHOW)
    if(data[i] < 33 || data[i] > 126)
     {
        if(i > 0 && (data[i-1] >= 33 && data[i-1] <= 126) )
                printf(" ");
        printf("%02x ",data[i]);
     }else{
        printf("%c", data[i]);
     }
#else
        printf("%02x ",data[i]);
#endif

#if COUNT_BYTE_AND_NEW_LINE
   if ((i + 1) % 32 == 0){
        printf("    | %d Bytes\n",i + 1);
    }
#endif
}
    printf("\n----------- %s Print End ----------\n", note);
}


int main(int argc, char* argv[])  
{  
    if( argc != 3){
        printf("Error: no two parameters!\n");
        return -1;
	}
    char jsonbuffer[2048] = { 0 };
    memset(jsonbuffer, 0 , 2048);
    printf("argv[1]=%s\nsizeof(argv[1])=%d\nargv[2]=%s\nsizeof(argv[2])=%d\n", argv[1], strlen(argv[1]),argv[2],strlen(argv[2]));
    memcpy(jsonbuffer + 4, argv[2], strlen(argv[2]));

    int sendlen = strlen(argv[2]) + 4;
    uint32_t sendnetlen = htonl(sendlen);
    memcpy(jsonbuffer, &sendnetlen, 4);

    setvbuf(stdout,NULL,_IONBF,0);
    int    sockfd = -1, n = 0, rec_len = 0;  
    char    recvline[4096] = {0}, sendline[4096] = {0};  
    char    buf[MAXLINE] = {0};  
    struct sockaddr_in    servaddr;  
  
  
    if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){  
        printf("create socket error: %s(errno: %d)\n", strerror(errno),errno);
        exit(0);
    }  
 	const char* ip = argv[1];
  
    memset(&servaddr, 0, sizeof(servaddr));  
    servaddr.sin_family = AF_INET;  
    servaddr.sin_port = htons(80);  
    if( inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0){  
    printf("inet_pton error for \n");  
    exit(0);  
    }  
  
  
    if( connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0){  
        printf("connect error: %s(errno: %d)\n",strerror(errno),errno);
        exit(0);
    }  
  
    printf("send msg to server: \n");  

    if( send(sockfd, jsonbuffer, sendlen, 0) < 0)  
    {  
        printf("send msg error: %s(errno: %d)\n", strerror(errno), errno);
        exit(0);
    }else{
        debug_print_data(jsonbuffer, sendlen, "send data");
	}

    while(1)
    {
        printf("start receive data\n");
        if((rec_len = recv(sockfd, buf, MAXLINE,0)) == -1) {
            printf("receive length under 0\n");
            perror("recv error");
            return -1;
            exit(1);
        }
        printf("received data length:%d\n", rec_len);
        debug_print_data(buf, rec_len, "receive data");
    }
    return 0;
} 
