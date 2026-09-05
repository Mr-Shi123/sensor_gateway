#include "thread.h"

void* work_thread(void *arg)
{
    int cur_cnt = 0;
    int batch_size = g_config.queue_config.batch_size;
    msg_node_t **batch = malloc(g_config.queue_config.batch_size * sizeof(msg_node_t*)); //缓冲区
    if(batch == NULL)
    {
        perror("批量缓存区分配内存失败");
        log_msg("ERROR", "批量缓存区分配内存失败: %s", strerror(errno));
        return NULL;
    }

    while(1)
    {
        msg_node_t *node = pop();
        batch[cur_cnt++] = node;

        if(cur_cnt >= batch_size)
        {
            insert_into_db(batch, cur_cnt);
            cur_cnt = 0;
        }

        if(cur_cnt > 0 && is_empty_queue())
        {
            insert_into_db(batch, cur_cnt);
            cur_cnt = 0;
        }
    }
    return NULL;
}

void start_thread()
{
    //开启数据库线程:
    int thread_cnt = 1;
    for(int i = 0; i < thread_cnt; i++)
    {
        pthread_t t;
        pthread_create(&t, NULL, work_thread, NULL);
        pthread_detach(t);
    }
    printf("线程启动完成\n");
}