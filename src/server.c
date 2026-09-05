#include "server.h"
#include "stress_control.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define MAX_EVENTS 64
#define CONTROL_RECV_BUF_SIZE 2048

static int g_current_count = 0;              // 当前传感器连接数量
static int g_epfd = 0;                      // epoll实例
static sensor_t **g_sensors_list = NULL;    // 传感器列表
static int server_sockfd = -1;              // 传感器监听socket
static int control_server_sockfd = -1;      // 压力测试控制监听socket
static int control_client_fd = -1;          // 当前控制连接
static uint8_t control_recv_buf[CONTROL_RECV_BUF_SIZE];
static uint32_t control_recv_len = 0;

// 压力测试期间，成功解包的完整协议包数量
static uint64_t g_test_recv_success = 0;
static int g_test_running = 0;  // 标记是否在运行测试

/**
 * @brief 将fd设置非阻塞
 *  */  
static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return;

    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * @brief 创建socket，返回fd 
 */
static int create_listener(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("socket创建失败");
        log_msg("ERROR", "socket创建失败: %s", strerror(errno));
        return -1;
    }

    //给服务器 socket 开“地址复用”选项，主要方便服务器重启后重新 bind 同一个地址/端口
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        log_msg("WARN", "setsockopt(SO_REUSEADDR)失败: %s", strerror(errno));

    set_nonblocking(fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind失败");
        log_msg("ERROR", "bind %d 失败: %s", port, strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, g_config.server_config.max_sensors) < 0)
    {
        perror("listen失败");
        log_msg("ERROR", "listen %d 失败: %s", port, strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

int ini_server()
{
    // 根据配置文件，分配内存
    g_sensors_list = calloc(g_config.server_config.max_sensors, sizeof(sensor_t *));
    if (g_sensors_list == NULL)
    {
        perror("为g_sensors_list分配内存失败");
        log_msg("ERROR", "为g_sensors_list分配内存失败: %s", strerror(errno));
        return -1;
    }

    server_sockfd = create_listener(g_config.server_config.port);
    if (server_sockfd < 0)
        return -1;

    // 压力测试控制端口 = 业务端口 + 1
    int control_port = g_config.server_config.port + STRESS_CONTROL_PORT_OFFSET;
    control_server_sockfd = create_listener(control_port);
    if (control_server_sockfd < 0)
    {
        close(server_sockfd);
        server_sockfd = -1;
        return -1;
    }

    g_epfd = epoll_create1(0);
    if (g_epfd < 0)
    {
        perror("epoll_create1失败");
        log_msg("ERROR", "epoll_create1失败: %s", strerror(errno));
        close(server_sockfd);
        close(control_server_sockfd);
        return -1;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server_sockfd;
    if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, server_sockfd, &ev) < 0)
    {
        log_msg("ERROR", "添加server_sockfd到epoll失败: %s", strerror(errno));
        return -1;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = control_server_sockfd;
    if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, control_server_sockfd, &ev) < 0)
    {
        log_msg("ERROR", "添加control_server_sockfd到epoll失败: %s", strerror(errno));
        return -1;
    }

    printf("服务器启动成功, 业务端口 %d, 压测控制端口 %d\n",
           g_config.server_config.port, control_port);
    log_msg("INFO", "服务器启动成功, 业务端口 %d, 压测控制端口 %d",
            g_config.server_config.port, control_port);
    return 0;
}

/**
 * @brief 获取当前sensorfd的信息赋值给data和它在g_sensors_list中的索引赋值给idx
 */
static int get_sensor_info(sensor_t *data, int *idx, int sensorfd)
{
    for (int i = 0; i < g_current_count; i++)
    {
        if (sensorfd == g_sensors_list[i]->fd)
        {
            *idx = i;
            data->fd = g_sensors_list[i]->fd;
            strcpy(data->ip, g_sensors_list[i]->ip);
            return 0;
        }
    }

    return -1;
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

/**
 * @brief 给连接服务器的设备发送反馈
 */
static void send_feedback(int sensorfd, const char *feedback)
{
    uint32_t packet_len = 0;
    uint8_t *packet = pack_message(OTHER_TYPE, feedback, &packet_len);
    if (packet == NULL)
        return;

    size_t sent = 0;
    while (sent < packet_len)
    {
        ssize_t n = send(sensorfd, packet + sent, packet_len - sent, MSG_NOSIGNAL);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        sent += (size_t)n;
    }

    free(packet);
}

/**
 * @brief 检查心跳时间，长时间无操作，自动断开
 */
static void timeout()
{
    time_t now = time(NULL);
    for (int i = 0; i < g_current_count; i++)
    {
        if (now - g_sensors_list[i]->last_heartbeat > g_config.server_config.heartbeat_timeout)
        {
            printf("传感器 %s 长时间无操作, 已自动断开连接\n", g_sensors_list[i]->ip);
            log_msg("WARN", "传感器 %s 长时间无操作, 已自动断开连接", g_sensors_list[i]->ip);
            send_feedback(g_sensors_list[i]->fd, "长时间无操作, 已自动断开连接\n");
            remove_sensor(i);
            i--;
        }
    }
}

static void add_sensor(int sensorfd, char *ip)
{
    if (g_current_count >= g_config.server_config.max_sensors)
    {
        printf("超过服务器最大连接数量, 拒绝连接\n");
        log_msg("WARN", "超过服务器最大连接数量, 拒绝连接");
        close(sensorfd);
        return;
    }

    set_nonblocking(sensorfd);

    sensor_t *new_sensor = calloc(1, sizeof(sensor_t));
    if (new_sensor == NULL)
    {
        log_msg("ERROR", "为sensor分配内存失败: %s", strerror(errno));
        close(sensorfd);
        return;
    }

    new_sensor->fd = sensorfd;
    strncpy(new_sensor->ip, ip, sizeof(new_sensor->ip) - 1);
    new_sensor->last_heartbeat = time(NULL);
    new_sensor->recv_len = 0;
    g_sensors_list[g_current_count++] = new_sensor;

    struct epoll_event sev;
    memset(&sev, 0, sizeof(sev));
    sev.data.fd = sensorfd;
    sev.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, sensorfd, &sev) < 0)
    {
        log_msg("ERROR", "添加传感器fd到epoll失败: %s", strerror(errno));
        g_sensors_list[--g_current_count] = NULL;
        free(new_sensor);
        close(sensorfd);
        return;
    }

    log_msg("INFO", "连接到新传感器 %s", ip);
    printf("连接到新传感器 %s\n", ip);
    send_feedback(sensorfd, "您已成功与服务器连接!\n");
}

/**
 * @brief 处理服务器事务(accept和添加客户端进入g_sensors_list)
 */
static void handle_server_event(int serverfd)
{
    while (1)
    {
        struct sockaddr_in sensor_addr;
        socklen_t len = sizeof(sensor_addr);
        int sensorfd = accept(serverfd, (struct sockaddr *)&sensor_addr, &len);
        if (sensorfd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;

            perror("accept失败");
            log_msg("ERROR", "accept失败: %s", strerror(errno));
            break;
        }

        add_sensor(sensorfd, inet_ntoa(sensor_addr.sin_addr));
    }
}

static void send_data(int fd)
{
    int cnt = 0;
    char **data = get_sensor_data(&cnt);
    if (data == NULL || cnt == 0)
    {
        send_feedback(fd, "暂无数据\n");
        return;
    }

    char *title = "---------- sensor_data ----------\n";
    send_feedback(fd, title);
    for (int i = 0; i < cnt; i++)
    {
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
    sensor_t current_sensor;
    int idx = -1;
    if (get_sensor_info(&current_sensor, &idx, sensorfd) < 0)
    {
        epoll_ctl(g_epfd, EPOLL_CTL_DEL, sensorfd, NULL);
        close(sensorfd);
        return;
    }

    g_sensors_list[idx]->last_heartbeat = time(NULL);

    while (1)
    {
        char temp_buf[1024];
        int ret = recv(sensorfd, temp_buf, sizeof(temp_buf), 0);
        if (ret > 0)
        {
            if (g_sensors_list[idx]->recv_len + (uint32_t)ret < sizeof(g_sensors_list[idx]->recv_buf))
            {
                memcpy(g_sensors_list[idx]->recv_buf + g_sensors_list[idx]->recv_len,
                       temp_buf, (size_t)ret);
                g_sensors_list[idx]->recv_len += (uint32_t)ret;
            }
            else
            {
                log_msg("ERROR", "传感器 %s 缓冲区溢出，断开", current_sensor.ip);
                remove_sensor(idx);
                return;
            }
        }
        else if (ret == 0)
        {
            printf("传感器 %s 正常断开连接\n", current_sensor.ip);
            log_msg("INFO", "传感器 %s 正常断开连接", current_sensor.ip);
            remove_sensor(idx);
            return;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;

            fprintf(stderr, "接收传感器 %s 的信息失败: %s\n", current_sensor.ip, strerror(errno));
            log_msg("ERROR", "接收传感器 %s 的信息失败: %s", current_sensor.ip, strerror(errno));
            remove_sensor(idx);
            return;
        }
    }

    while (1)
    {
        char *msg = NULL;
        uint8_t type;
        int consumed = 0;
        int res = unpack_message(g_sensors_list[idx]->recv_buf,
                                 g_sensors_list[idx]->recv_len,
                                 &type, &msg, &consumed);
        if (res == MAGIC_FAILED)
        {
            log_msg("WARN", "传感器 %s 发送无效包，断开", current_sensor.ip);
            printf("传感器 %s 发送无效包，断开\n", current_sensor.ip);
            remove_sensor(idx);
            return;
        }
        else if (res == INCOMPLETE_DATA)
        {
            break;
        }

        // 一个完整协议包成功解包
        if (g_test_running)
            g_test_recv_success++;

        if (type == GET_DATA_TYPE)
        {
            send_data(sensorfd);
            free(msg);
        }
        else if (type == SENSOR_DATA_TYPE)
        {
            push(msg);
        }
        else if (type == PING_TYPE)
        {
            g_sensors_list[idx]->last_heartbeat += 60;
            char feedback[128];
            snprintf(feedback, sizeof(feedback),
                     "已为您延长心跳时间, 目前剩余:%lds\n",
                     g_sensors_list[idx]->last_heartbeat - time(NULL) +
                         g_config.server_config.heartbeat_timeout);
            send_feedback(sensorfd, feedback);
            free(msg);
        }
        else
        {
            log_msg("DEBUG", "收到信息: \"%s\"", msg);
            free(msg);
        }

        memmove(g_sensors_list[idx]->recv_buf,
                g_sensors_list[idx]->recv_buf + consumed,
                g_sensors_list[idx]->recv_len - consumed);
        g_sensors_list[idx]->recv_len -= consumed;
    }
}

/**
 * @brief 给控制测试程序的socket反馈
 */
static int send_control_text(int fd, const char *text)
{
    send_feedback(fd, text);
    return 0;
}

static void handle_control_client_event(int fd)
{
    while (1)
    {
        uint8_t temp_buf[1024];
        ssize_t ret = recv(fd, temp_buf, sizeof(temp_buf), 0);
        if (ret > 0)
        {
            if (control_recv_len + (uint32_t)ret > sizeof(control_recv_buf))
            {
                log_msg("WARN", "压力测试控制连接缓冲区溢出，关闭连接");
                epoll_ctl(g_epfd, EPOLL_CTL_DEL, fd, NULL);
                close(fd);
                control_client_fd = -1;
                control_recv_len = 0;
                return;
            }

            memcpy(control_recv_buf + control_recv_len, temp_buf, (size_t)ret);
            control_recv_len += (uint32_t)ret;
        }
        else if (ret == 0)
        {
            epoll_ctl(g_epfd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            control_client_fd = -1;
            control_recv_len = 0;
            return;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;

            epoll_ctl(g_epfd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            control_client_fd = -1;
            control_recv_len = 0;
            return;
        }
    }

    while (1)
    {
        char *msg = NULL;
        uint8_t type = 0;
        int consumed = 0;
        int res = unpack_message(control_recv_buf, control_recv_len,
                                 &type, &msg, &consumed);
        if (res == INCOMPLETE_DATA)
            break;

        if (res == MAGIC_FAILED)
        {
            send_control_text(control_client_fd, "控制命令无效\n");
            epoll_ctl(g_epfd, EPOLL_CTL_DEL, control_client_fd, NULL);
            close(control_client_fd);
            control_client_fd = -1;
            control_recv_len = 0;
            return;
        }

        if (type == STRESS_START_TYPE)
        {
            g_test_recv_success = 0;
            g_test_running = 1;
            send_control_text(control_client_fd, "STRESS_START_OK\n");
        }
        else if (type == STRESS_QUERY_TYPE)
        {
            char response[128];
            snprintf(response, sizeof(response),
                     "STRESS_RECV_SUCCESS=%" PRIu64 "\n", g_test_recv_success);
            send_control_text(control_client_fd, response);
        }
        else if (type == STRESS_END_TYPE)
        {
            g_test_running = 0;
            char response[128];
            snprintf(response, sizeof(response),
                     "STRESS_END_OK, RECV_SUCCESS=%" PRIu64 "\n", g_test_recv_success);
            send_control_text(control_client_fd, response);
        }
        else
        {
            send_control_text(control_client_fd, "未知压力测试控制命令\n");
        }

        free(msg);
        memmove(control_recv_buf,
                control_recv_buf + consumed,
                control_recv_len - consumed);
        control_recv_len -= consumed;
    }
}

static void handle_control_server_event(void)
{
    while (1)
    {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int fd = accept(control_server_sockfd, (struct sockaddr *)&addr, &len);
        if (fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;

            log_msg("ERROR", "接受压力测试控制连接失败: %s", strerror(errno));
            break;
        }

        if (control_client_fd != -1)
        {
            // 一次只允许一个压力测试控制客户端
            send_control_text(fd, "已有压力测试控制连接\n");
            close(fd);
            continue;
        }

        set_nonblocking(fd);
        control_client_fd = fd;
        control_recv_len = 0;

        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = fd;
        if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
        {
            log_msg("ERROR", "添加压力测试控制连接到epoll失败: %s", strerror(errno));
            close(fd);
            control_client_fd = -1;
            continue;
        }

        printf("压力测试控制连接已建立: %s\n", inet_ntoa(addr.sin_addr));
        log_msg("INFO", "压力测试控制连接已建立: %s", inet_ntoa(addr.sin_addr));
    }
}

void handle_events()
{
    struct epoll_event events[MAX_EVENTS];

    while (1)
    {
        int nfds = epoll_wait(g_epfd, events, MAX_EVENTS, 0);
        if (nfds < 0)
        {
            if (errno == EINTR)
                continue;

            perror("epoll_wait错误");
            log_msg("ERROR", "epoll_wait错误: %s", strerror(errno));
            break;
        }

        timeout();

        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;
            if (fd == server_sockfd)
                handle_server_event(fd);    //处理服务器事件
            else if (fd == control_server_sockfd)
                handle_control_server_event();  //处理服务器压力测试事件
            else if (fd == control_client_fd)
                handle_control_client_event(fd);    //处理压力测试客户端事件
            else
                handle_sensor_event(fd);    //处理传感器事件
        }
    }
}
