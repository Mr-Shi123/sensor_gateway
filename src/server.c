#include "server.h"

#define MAX_EVENTS 64
static int g_current_count = 0; //当前连接数量
static int g_epfd = 0;  //epoll实例
static sensor_t **g_sensors_list = NULL; //传感器列表
static int server_sockfd;
 
static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int ini_server()
{
    //根据配置文件,分配内存
    g_sensors_list = malloc(g_config.server_config.max_sensors * sizeof(sensor_t*));
    if(g_sensors_list == NULL)
    {
        perror("为g_sensors_list分配内存失败 ");
        log_msg("ERROR", "为g_sensors_list分配内存失败: %s", strerror(errno));
        return -1;
    }


    //创建socket
    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_sockfd < 0)
    {
        perror("socket创建失败 ");
        log_msg("ERROR", "socket创建失败: %s", strerror(errno));
        return -1;
    }
    set_nonblocking(server_sockfd);

    //bind
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_config.server_config.port);
    if(bind(server_sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind 失败 ");
        log_msg("ERROR", "bind 失败: %s", strerror(errno));
        return -1;
    }

    //listen
    if(listen(server_sockfd, g_config.server_config.max_sensors) < 0)
    {
        perror("listen 失败 ");
        log_msg("ERROR", "listen 失败: %s", strerror(errno));
        return -1;
    }

    //创建epoll实例
    g_epfd = epoll_create1(0);
    //创建事件结构体对sockfd要处理的事件进行封装
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  //可读 + 边缘模式
    ev.data.fd = server_sockfd;
    // 将sockfd和对他操作的事件,添加进epoll实例
    epoll_ctl(g_epfd, EPOLL_CTL_ADD, server_sockfd, &ev);

    printf("服务器启动成功, 端口 %d\n", g_config.server_config.port);
    log_msg("INFO", "服务器启动成功, 端口 %d", g_config.server_config.port);
    return 0;
}

/**
 * @brief 获取当前sensorfd的信息赋值给data和它在g_sensors_list中的索引赋值给idx
 */
static void get_sensor_info(sensor_t *data, int *idx, int sensorfd)
{
    for (int i = 0; i < g_current_count; i++)
        if (sensorfd == g_sensors_list[i]->fd)
        {
            *idx = i;
            data->fd = g_sensors_list[i]->fd;
            strcpy(data->ip, g_sensors_list[i]->ip);
            break;
        }
}

static void remove_sensor(int idx)
{
    sensor_t *del = g_sensors_list[idx];
    g_sensors_list[idx] = g_sensors_list[g_current_count - 1];
    g_sensors_list[g_current_count - 1] = NULL;
    g_current_count--;
    epoll_ctl(g_epfd, EPOLL_CTL_DEL, del->fd, NULL);
    close(del->fd);
    free(del);
}

static void send_feedback(int sensorfd, char* feedback)
{
    uint32_t packet_len = 0;
    uint8_t* packet = pack_message(OTHER_TYPE, feedback, &packet_len);
    send(sensorfd, packet, packet_len, 0);
    free(packet);
}

static void timeout()
{
    time_t now = time(NULL);
    for(int i = 0; i < g_current_count; i++)
        if(now - g_sensors_list[i]->last_heartbeat > g_config.server_config.heartbeat_timeout)
        {
            printf("传感器 %s 长时间无操作, 已自动断开连接\n", g_sensors_list[i]->ip);
            log_msg("WARN", "传感器 %s 长时间无操作, 已自动断开连接", g_sensors_list[i]->ip);
            send_feedback(g_sensors_list[i]->fd, "长时间无操作, 已自动断开连接\n");
            remove_sensor(i);
            i--;    //最后一个覆盖当前位置,要对当前位置再进行检测一遍
        }
}


static void add_sensor(int sensorfd, char *ip)
{
    if(g_current_count >= g_config.server_config.max_sensors)
    {
        printf("超过服务器最大连接数量, 拒绝连接\n");
        log_msg("WARN", "超过服务器最大连接数量, 拒绝连接");
        close(sensorfd);
        return;
    }

    //ET模式将sensorfd设置成非阻塞
    set_nonblocking(sensorfd);

    //添加到传感器队列中
    sensor_t *new_sensor = malloc(sizeof(sensor_t));
    new_sensor->fd = sensorfd;
    strcpy(new_sensor->ip, ip);
    new_sensor->last_heartbeat = time(NULL);
    new_sensor->recv_len = 0;
    g_sensors_list[g_current_count++] = new_sensor;

    //将其与事件绑定,添加到epoll实例中
    struct epoll_event sev;
    sev.data.fd = sensorfd;
    sev.events = EPOLLIN | EPOLLET;     //可读 + 边缘触发
    epoll_ctl(g_epfd, EPOLL_CTL_ADD, sensorfd, &sev);

    log_msg("INFO", "连接到新传感器 %s", ip);
    printf("连接到新传感器 %s\n", ip);
    send_feedback(sensorfd, "您已成功与服务器连接!\n");
}

/**
 * @brief 处理服务器事物(accpet和添加客户端进入g_sensors_list)
 */
static void handle_server_event(int server_sockfd)
{
    struct sockaddr_in sensor_addr;
    socklen_t len = sizeof(sensor_addr);

    //ET模式下，循环accpet
    while(1)
    {
        int sensorfd = accept(server_sockfd, (struct sockaddr*)&sensor_addr, &len);
        if (sensorfd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;

            perror("accept 失败");
            log_msg("ERROR", "accept 失败: %s", strerror(errno));
            break;
        }
        add_sensor(sensorfd, inet_ntoa(sensor_addr.sin_addr));
    }
}

static void send_data(int fd)
{
    int cnt = 0;
    char **data = get_sensor_data(&cnt);
    if(data == NULL || cnt == 0)
    {
        send_feedback(fd, "暂无数据\n");
        return;
    }

    char *title = "---------- sensor_data ----------\n";
    send_feedback(fd, title);
    for(int i = 0; i < cnt; i++){
        send_feedback(fd, data[i]);
        free(data[i]);
    }
    char end[128];
    snprintf(end, sizeof(end), "---------- 已为您查询到 %d 条数据 ----------\n", cnt);
    send_feedback(fd, end);
    free(data);
}

static void handle_sensor_event(int sensorfd)
{
    //获取当前传感器的信息
    sensor_t current_sensor;
    int idx;
    get_sensor_info(&current_sensor, &idx, sensorfd);

    //更新心跳时间
    g_sensors_list[idx]->last_heartbeat = time(NULL);

    //循环recv，一次性把传感器发送数据的缓冲区内的数据给读完
    while(1)
    {
        char temp_buf[1024];
        int ret = recv(sensorfd, temp_buf, sizeof(temp_buf), 0);
        if(ret > 0)
        {
            //防溢出
            if(g_sensors_list[idx]->recv_len + ret < sizeof(g_sensors_list[idx]->recv_buf))
            {
                memcpy(g_sensors_list[idx]->recv_buf + g_sensors_list[idx]->recv_len, temp_buf, ret);
                g_sensors_list[idx]->recv_len += ret;
            }
            else
            {
                log_msg("ERROE", "传感器 %s 缓冲区溢出， 端开", current_sensor.ip);
                remove_sensor(idx);
                return;
            }
        }
        else if(ret == 0)
        {
            printf("传感器 %s 正常断开连接\n", current_sensor.ip);
            log_msg("INFO", "传感器 %s 正常断开连接", current_sensor.ip);
            remove_sensor(idx);
            return;
        }
        else    //  ret < 0的情况
        {
            //  缓冲区数据读完，正常退出，返回到epoll_wait
            if(errno == EAGAIN)
                break;
            else
            {
                //  处理真正的错误
                fprintf(stderr, "接收传感器 %s 的信息失败: %s\n", current_sensor.ip, strerror(errno));
                log_msg("ERROR", "接收传感器 %s 的信息失败: %s", current_sensor.ip, strerror(errno));
                remove_sensor(idx);
                return;
            }
        }
    }

    //处理接收缓冲区的完整包
    while(1)
    {
        char* msg = NULL;
        uint8_t type;
        int consumed = 0;
        int res = unpack_message(g_sensors_list[idx]->recv_buf, g_sensors_list[idx]->recv_len, &type, &msg, &consumed);
        if(res == -1)
        {
            log_msg("WARN", "传感器 %s 发送无效包， 断开", current_sensor.ip);
            printf("传感器 %s 发送无效包， 断开\n",current_sensor.ip);
            remove_sensor(idx);
            return;
        }
        else if(res == -2)  //数据没有齐全
            break;
        
        if(type == GET_DATA_TYPE)    //获取数据指令
        {
            send_data(sensorfd);
            free(msg);
        }
        else if(type == SENSOR_DATA_TYPE)   //上传传感器数据
            push(msg);
        else if(type == PING_TYPE)   //延长心跳时间
        {
            g_sensors_list[idx]->last_heartbeat += 60;
            char feedback[128];
            snprintf(feedback, sizeof(feedback), "已为您延长心跳时间, 目前剩余:%lds\n", 
                g_sensors_list[idx]->last_heartbeat - time(NULL) + g_config.server_config.heartbeat_timeout);
            send_feedback(sensorfd, feedback);
            free(msg);
        }
        else
        {
            log_msg("DEBUG", "收到信息: \"%s\"", msg);
            free(msg);
        }
        
        memmove(g_sensors_list[idx]->recv_buf, g_sensors_list[idx]->recv_buf + consumed, g_sensors_list[idx]->recv_len - consumed);
        g_sensors_list[idx]->recv_len -= consumed;
    }
}

void handle_events()
{
    struct epoll_event events[MAX_EVENTS];  //存储就绪事件
    while(1)
    {
        //不阻塞立即返回,要检查心跳时间
        int nfds = epoll_wait(g_epfd, events, MAX_EVENTS, 0);
        if(nfds < 0)
        {
            perror("epoll_wait错误 ");
            log_msg("ERROR", "epoll_wait错误: %s", strerror(errno));
            break;
        }

        //检测心跳时间
        timeout();

        //遍历所以就绪事件
        for(int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;
            if(fd == server_sockfd)
                handle_server_event(fd);
            else   
                handle_sensor_event(fd);
        }
    }

}