#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#define DEFAULT_TCP_SERVER_PORT 80    // tcp server port

#define MAX_JSON_LEN (1460 - 4)       // max json length allowed, 4-Bytes reserved for TCP stream header

#define MAX_CLIENT_NUMBER 2           //  max tcp client counts

// if set TCP_STREAM_HEAD_CHECK to 1 [safe && recommended], there would must have a 4 Bytes length information in front of TCP Stream
// if set TCP_STREAM_HEAD_CHECK to 0 [simple], there would a naked JSON data, without any length info
#define TCP_STREAM_HEAD_CHECK 1       // if MAX_JSON_LEN > 1460 [default TCP MTU : 1500], must set TCP_STREAM_HEAD_CHECK to 1


#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

typedef xSemaphoreHandle tcp_mutex;

struct conn_param {
    int32_t conn_num;
    int32_t sock_fd[MAX_CLIENT_NUMBER];
};


// e_sample_errno
// e_sample_errno < 0 means that some error happened
// e_sample_errno = 0 means that parse json OK
// e_sample_errno > 0 means that lookup some information

typedef enum {
    SAMPLE_OK = 0,
    TCP_RCV_RET_NEGATIVE = -1,          // connection error
    TCP_RCV_RET_ZERO = -2,              // close socket actively
    TCP_REV_PARSE_CMD_LEN_ERROR = -3,   // parse length from tcp stream big than max json length
    JSON_PARSE_ROOT_FAILED = -4,        // json parse failed, the same as the following
    JSON_PARSE_NO_ITEM = -5,

    // here would be more json parse error number
    JSON_PARSE_SSID_FAILED = -22,
    JSON_PARSE_PASSWD_FAILED = -23,
    JSON_PARSE_OTA_SERVER_ADDR_FAILED = -24,
    JSON_PARSE_OTA_SERVER_PORT_FAILED = -25,

    TCP_REV_PARSE_CMD_BEYOND_MAX_JSON_LEN = -27,
    JSON_PARSE_SAMPLE_PARAMETER_FAILED = -28,

    // lookup configurate state
    PROCESS_INIT = 10000,
    PROCESS_LOCAL_UPDATE = 11000,
    PROCESS_SAMPLE_PARA = 12000,
} e_sample_errno;


// create a mutex
int mutex_new(tcp_mutex *p_mutex);

// lock a mutex, immediate return if mutex is available, blocking if mutex is not available
void mutex_lock(tcp_mutex *p_mutex);

// unlock a mutex
void mutex_unlock(tcp_mutex *p_mutex);

// destroy a mutex
void mutex_delete(tcp_mutex *p_mutex);

void fatal_error(int line);

int conn_prepare(int index);

void data_destroy(int32_t index);

#endif

