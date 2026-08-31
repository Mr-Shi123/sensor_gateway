#ifndef __QUEUE_H_
#define __QUEUE_H_

#include <pthread.h>
#include <stdlib.h>
#include <errno.h>

#include "log.h"
#include "config.h"


//消息节点
typedef struct msg_node {
    char msg[128];  //存放用户发送的消息
    char time[32];    //消息发送时间
    struct msg_node* next;  //指向下一个节点
} msg_node_t;

//消息队列
typedef struct{
    msg_node_t* head;
    msg_node_t* tail;
    int cur_cnt;
    int max_cnt;
    pthread_mutex_t lock;
    pthread_cond_t empty;   //队列空了,消费者等待    
    pthread_cond_t full; //队列满了,生存者等待
} msg_queue_t;

extern msg_queue_t g_queue;

/**
 * @brief 初始化消息队列
 */
void ini_queue();

/**
 * @brief 消息入队, push()接管msg的所有权,调用者使用push()后不需要free(msg)
 * @param msg 消息
 */
void push(char *msg);


/**
 * @brief 取出队头消息
 */
msg_node_t* pop();

/**
 * @brief 检查队列是否为空
 * @return 1为空, 0为非空
 */
int is_empty_queue();

#endif