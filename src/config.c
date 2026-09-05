#include "config.h"

config_t g_config;

void config_print()
{
    printf("server_config.port = %d\n", g_config.server_config.port);
    printf("server_config.max_sensors = %d\n", g_config.server_config.max_sensors);
    printf("server_config.heartbeat_timeout = %d\n", g_config.server_config.heartbeat_timeout);
    printf("queue_config.max_size = %d\n", g_config.queue_config.max_size);
    printf("queue_config.batch_size = %d\n", g_config.queue_config.batch_size);
    printf("database_config.db_file = %s\n", g_config.database_config.db_file);
    printf("log_config.log_file = %s\n", g_config.log_config.log_file);
}

void load_config(const char* config_filename)
{
    //设置默认配置
    //  1.服务器
    g_config.server_config.port = 8080;
    g_config.server_config.max_sensors = 90;
    g_config.server_config.heartbeat_timeout = 50;
    //  2.消息队列
    g_config.queue_config.max_size = 900;
    g_config.queue_config.batch_size = 9;
    //  3.数据库
    strcpy(g_config.database_config.db_file, "sensor_data_tmp.db");
    //  5.日志
    strcpy(g_config.log_config.log_file, "./log/gateway_tmp.log");

    FILE *fp = fopen(config_filename, "r");
    if(fp == NULL)
    {
        perror("配置文件打开失败");
        printf("使用默认配置\n");
        config_print();
        return;
    }

    //解析文件内容
    char buf[128];
    while(fgets(buf, sizeof(buf), fp) != NULL)
    {
        char* str = trim(buf);
        if(str[0] == '#' || is_empty_str(str))
            continue;
        char key[32], val[32];
        sscanf(str, "%[^=]=%[^\n]", key, val);
        char *k = trim(key);
        char *v = trim(val);

        if(strcmp(k, "port") == 0)
            g_config.server_config.port = atoi(v);
        else if(strcmp(k, "max_sensors") == 0)
            g_config.server_config.max_sensors = atoi(v);
        else if(strcmp(k, "heartbeat_timeout") == 0)
            g_config.server_config.heartbeat_timeout = atoi(v);
        else if(strcmp(k, "max_size") == 0)
            g_config.queue_config.max_size = atoi(v);
        else if(strcmp(k, "batch_size") == 0)
            g_config.queue_config.batch_size = atoi(v);
        else if(strcmp(k, "db_file") == 0)
            strcpy(g_config.database_config.db_file, v);
        else if(strcmp(k, "log_file") == 0)
            strcpy(g_config.log_config.log_file, v);
    }

    printf("配置文件加载完成\n");
}