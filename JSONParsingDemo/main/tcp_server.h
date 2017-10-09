#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#define DEFAULT_GWSERVER_PORT 80    // tcp server port
#define MAX_JSON_LEN 2920           // max json length allowed

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

#endif
