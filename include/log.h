#ifndef __LOG_H_
#define __LOG_H_

#include <stdio.h>
#include <stdarg.h>

#include "config.h"
#include "utils.h"

/**
 * @brief 将信息写入日志文件
 * @param level 日志级别
 * @param fmt 信息格式
 * @param ... 可变参数
 */
void log_msg(char *level, char *fmt, ...);

/**
 * @brief 打开日志文件
 */
void open_logfile();

/**
 * @brief 关闭日志文件
 */
void close_logfile();

#endif