#ifndef __SERVER_H_
#define __SERVER_H_

#include <pthread.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include "log.h"
#include "config.h"
#include "queue.h"
#include "database.h"
#include "protocol.h"

//每一个传感器信息
typedef struct {
    int fd; 
    char ip[32];
    time_t last_heartbeat;
    uint8_t recv_buf[2048];    
    uint32_t recv_len;   
} sensor_t;


/**
 * @brief 初始化服务器
 */
int ini_server();

/**
 * @brief 等待与处理事件
 */
void handle_events();

#endif