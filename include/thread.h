#ifndef __THREAD_H_
#define __THREAD_H_

#include <pthread.h>
#include <stdio.h>
#include "config.h"
#include "queue.h"
#include "database.h"

/**
 * @brief 工作线程
 */
void* work_thread(void *arg);

/**
 * @brief 开启线程
 */
void start_thread();
    

#endif