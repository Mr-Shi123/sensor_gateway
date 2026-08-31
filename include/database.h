#ifndef __DATABASE_H_
#define __DATABASE_H_

#include <sqlite3.h>
#include <errno.h>
#include <stdio.h>

#include "config.h"
#include "log.h"
#include "queue.h"

extern sqlite3 *g_db;

/**
 * @brief 初始化数据库
 */
void ini_database();

/**
 * @brief 批量插入数据库
 * @param batch 消息数组
 * @param cnt 消息数量
 */
void insert_into_db(msg_node_t **batch, int cnt);

/**
 * @brief 获取数据库中的数据
 * @param cnt 记录查到多少条数据
 * @return 返回数据数组
 */
char** get_sensor_data(int *cnt);

#endif