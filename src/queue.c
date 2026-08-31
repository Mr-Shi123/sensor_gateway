#include "queue.h"

msg_queue_t g_queue;

void ini_queue()
{
    g_queue.head = NULL;
    g_queue.tail = NULL;
    g_queue.cur_cnt = 0;
    g_queue.max_cnt = g_config.queue_config.max_size;
    pthread_mutex_init(&g_queue.lock, NULL);
    pthread_cond_init(&g_queue.empty, NULL);
    pthread_cond_init(&g_queue.full, NULL);
}

void push(char *msg)
{
    pthread_mutex_lock(&g_queue.lock);

    // log_msg("DEBUG", "数据 %s 进入push函数", msg);

    //如果队列满了,先等待队列中的消息处理完成
    while(g_queue.cur_cnt >= g_queue.max_cnt)
    {
        printf("消息队列已满, 等待处理中...\n");
        //让该线程解锁,睡眠;    被唤醒,再上锁
        pthread_cond_wait(&g_queue.full, &g_queue.lock);
    }

    msg_node_t *node = malloc(sizeof(msg_node_t));
    if(node == NULL)
    {
        fprintf(stderr, "为新消息入队分配内存失败: %s\n", strerror(errno));
        log_msg("ERROR", "为新消息入队分配内存失败: %s", strerror(errno));
        pthread_mutex_unlock(&g_queue.lock);
        free(msg);
        return;
    }
    strcpy(node->msg, msg);
    free(msg);
    get_time_str(node->time, sizeof(node->time));
    node->next = NULL;

    if(g_queue.head == NULL && g_queue.tail == NULL)
        g_queue.head = g_queue.tail = node;
    else
    {
        g_queue.tail->next = node;
        g_queue.tail = node;
    }
    g_queue.cur_cnt++;
    //唤醒出队线程
    pthread_cond_signal(&g_queue.empty);
    pthread_mutex_unlock(&g_queue.lock);

    // log_msg("DEBUG", "数据 %s 完成入队\n", msg);
}

msg_node_t* pop()
{
    pthread_mutex_lock(&g_queue.lock);

    // log_msg("DEBUG", "进行出队操作");

    //如果队列为空,先等待消息入队 
    while(g_queue.cur_cnt == 0)
    {
        printf("消息队列为空, 等待消息入队...\n");
        //让该线程解锁,睡眠;    被唤醒,再上锁
        pthread_cond_wait(&g_queue.empty, &g_queue.lock);
    }

    msg_node_t* node = g_queue.head;
    //取出最后一个元素,head和tail都变成NULL
    if(g_queue.head == g_queue.tail)    
        g_queue.head = g_queue.tail = NULL;
    else    //否则,只将head后移
        g_queue.head = node->next;
    node->next = NULL;
    // log_msg("DEBUG", "获取到数据: %s", node->msg);
    g_queue.cur_cnt--;
    //队列有空间了,唤醒入队线程
    pthread_cond_signal(&g_queue.full);
    pthread_mutex_unlock(&g_queue.lock);
    
    // log_msg("DEBUG", "数据 %s 完成出队", node->msg);

    return node;
}

int is_empty_queue()
{
    pthread_mutex_lock(&g_queue.lock);
    int flag = (g_queue.cur_cnt == 0);
    pthread_mutex_unlock(&g_queue.lock);
    return flag;
}
