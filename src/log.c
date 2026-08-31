#include "log.h"

static FILE* fp = NULL;

void log_msg(char *level, char *fmt, ...)
{
    if(fp == NULL)
    {
        printf("日志文件没有打开, 无法写入信息\n");
        return;
    }

    char time_info[128];
    get_time_str(time_info, sizeof(time_info));
    fprintf(fp, "[%s] [%s] ", time_info, level);

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fprintf(fp, "\n");

    fflush(fp);
}

void open_logfile()
{
    fp = fopen(g_config.log_config.log_file, "a");
     if(fp == NULL)
    {
        perror("日志文件打开失败");
        return;
    }
    printf("日志文件打开成功 fp = %p\n", (void*)fp);
}

void close_logfile()
{
    fclose(fp);
}