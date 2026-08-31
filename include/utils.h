#ifndef __UTILS_H_
#define __UTILS_H_

#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/**
 * @brief 去除字符串首尾空格
 * @param str要处理的字符串
 * @return 处理后的字符串指针（与原指针相同）
 */
char* trim(char *str);


/**
 * @brief 获取时间字符串(%Y-%m-%d %H:%M:%S)
 * @param buf 输出缓冲区
 */
void get_time_str(char* buf, size_t size);


/**
 * @brief 判断字符串是否为空
 * @param str 要检验的字符串
 * @return 1为空, 0为非空
 */
int is_empty_str(char* str);


/**
 *@brief 判断是不是有用的数据 
 @param data 要判断的字符串数据
 @param temp 要获取的温度数据
 @param hum 要获取的湿度数据
 @return -1无效数据, 1有效数据
 */
int is_useful_data(char *data);


#endif