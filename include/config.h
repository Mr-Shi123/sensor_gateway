#ifndef __CONFIG_H_ 
#define __CONFIG_H_

#include <stdio.h>
#include <stdlib.h>

#include "utils.h"

typedef struct {
    int port;
    int max_sensors;
    int heartbeat_timeout;
} server_config_t;

typedef struct 
{
    int max_size;
    int batch_size;
} queue_config_t;

typedef struct 
{
    char db_file[64];
} database_config_t;


typedef struct {
    char log_file[64];
} log_config_t;

//整个配置文件结构体
typedef struct {
    server_config_t server_config;
    queue_config_t queue_config;
    database_config_t database_config;
    log_config_t log_config;
} config_t;

extern config_t g_config;

/** 
 * @brief 加载配置文件
*/
void load_config(const char* config_filename);

/**
 * @brief 打印配置文件信息
 */
void config_print();

#endif