#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sqlite3.h>
#include <inttypes.h>

#include "protocol.h"
#include "config.h"
#include "stress_control.h"

#define MAX_SENSOR_CNT 1000

static const char *ip = "172.20.89.244";
static int sensor_cnt = 0;
static int sleep_time = 0;
static int duration = 0;
static sqlite3 *g_db = NULL;

static int send_all(int sockfd, const uint8_t *buf, uint32_t len)
{
    uint32_t sent = 0;  //已发送字节数

    while (sent < len)
    {
        ssize_t n = send(sockfd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        sent += (uint32_t)n;
    }

    return 0;
}

/**
 * @brief 以协议包的方式send指令
 */
static int send_control_command(int sockfd, uint8_t type)
{
    uint32_t packet_len = 0;
    uint8_t *packet = pack_message(type, "", &packet_len);
    if (packet == NULL)
    {
        fprintf(stderr, "控制命令封包失败\n");
        return -1;
    }

    int ret = send_all(sockfd, packet, packet_len);
    free(packet);
    return ret;
}

/**
 * @brief 接收服务器的反馈，存入 char *response 中
 */
static int recv_control_response(int sockfd, char *response, size_t response_size)
{
    uint8_t recv_buf[2048];
    uint32_t recv_len = 0;

    while (1)
    {
        ssize_t n = recv(sockfd, recv_buf + recv_len,
                         sizeof(recv_buf) - recv_len, 0);
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
                continue;
            return -1;
        }

        recv_len += (uint32_t)n;

        while (1)
        {
            uint8_t type = 0;
            char *msg = NULL;
            int consumed = 0;

            int ret = unpack_message(recv_buf, recv_len,
                                     &type, &msg, &consumed);
            if (ret == INCOMPLETE_DATA)
                break;

            if (ret == MAGIC_FAILED)
                return -1;

            if (response_size > 0)
            {
                snprintf(response, response_size, "%s", msg ? msg : "");
            }

            free(msg);
            return 0;
        }

        if (recv_len == sizeof(recv_buf))
            return -1;
    }
}

static int connect_control_socket(void)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("控制socket创建失败");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_config.server_config.port + STRESS_CONTROL_PORT_OFFSET);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("连接压力测试控制端口失败");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

typedef struct
{
    uint64_t send_success;
    uint64_t send_fail;
    int connect_success;
} thread_stat_t;

void *thread_func(void *arg)
{
    thread_stat_t *stat = (thread_stat_t *)arg;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("传感器socket创建失败");
        return NULL;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_config.server_config.port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("传感器连接失败");
        close(sockfd);
        return NULL;
    }

    stat->connect_success = 1;

    time_t start = time(NULL);
    while (1)
    {
        time_t now = time(NULL);
        if (now - start >= duration * 60)
        {
            break;
        }

        char data[64];
        double temp = -20.0 + rand() * 1.0 / RAND_MAX * 80.0;
        double hum = rand() * 1.0 / RAND_MAX * 100.0;
        snprintf(data, sizeof(data), "Temp:%.2lf, Hum:%.2lf", temp, hum);

        uint32_t data_len = 0;
        uint8_t *packet = pack_message(SENSOR_DATA_TYPE, data, &data_len);
        if (packet == NULL)
        {
            fprintf(stderr, "packet内存分配失败\n");
            stat->send_fail++;
            break;
        }

        if (send_all(sockfd, packet, data_len) < 0)
        {
            perror("发送失败");
            stat->send_fail++;
            free(packet);
            break;
        }

        // send_all()成功：整个协议包已经发送完成
        stat->send_success++;

        free(packet);
        sleep(sleep_time);
    }

    close(sockfd);
    return NULL;
}

int to_num(char *str)
{
    int res = 0;
    int n = strlen(str);

    for (int i = 0; i < n; i++)
    {
        if (str[i] < '0' || str[i] > '9')
        {
            printf("[ERROR] 非法参数: %s\n", str);
            exit(EXIT_FAILURE);
        }

        res = res * 10 + str[i] - '0';
    }

    return res;
}

void handle_arguments(char **argv)
{
    sensor_cnt = to_num(argv[1]);
    if (sensor_cnt <= 0 || sensor_cnt > g_config.server_config.max_sensors)
    {
        printf("[ERROR] 传感器连接数量必须为 1~%d int型整数, 当前: argv[1] = %d\n",
               g_config.server_config.max_sensors, sensor_cnt);
        exit(EXIT_FAILURE);
    }

    sleep_time = to_num(argv[2]);

    duration = to_num(argv[3]);
    if (duration <= 0)
    {
        printf("[ERROR] 测试时间必须是 >0 的int型整数, 当前: argv[3] = %d\n", duration);
        exit(EXIT_FAILURE);
    }

    printf("%d个传感器连接，发送消息间隔%ds, 持续时间%dmin, 配置完成\n",
           sensor_cnt, sleep_time, duration);
}

void open_database(void)
{
    char *path = "./../../sensor_data.db";
    int rc = sqlite3_open(path, &g_db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "数据库无法打开: %s\n", sqlite3_errmsg(g_db));
        if (g_db != NULL)
            sqlite3_close(g_db);
        g_db = NULL;
        return;
    }

    printf("数据库成功打开\n");
}

static int callback(void *arg, int argc, char **argv, char **col_name)
{
    uint64_t *count = (uint64_t *)arg;
    if (argc > 0 && argv[0] != NULL)
        *count = strtoull(argv[0], NULL, 10);
    return 0;
}

uint64_t get_database_cnt(void)
{
    if (g_db == NULL)
    {
        printf("数据库数据查询失败: 数据库未成功打开\n");
        return UINT64_MAX;
    }

    char *sql = "SELECT COUNT(*) FROM sensor_data";
    char *err_msg = NULL;
    uint64_t count = 0;

    int rc = sqlite3_exec(g_db, sql, callback, &count, &err_msg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "查询数据库数量失败: %s\n", err_msg ? err_msg : "unknown error");
        sqlite3_free(err_msg);
        return UINT64_MAX;
    }

    return count;
}

void summary(uint64_t actually_send, uint64_t server_recv, uint64_t database_insert_cnt)
{
    printf("===============测试总结===============\n");
    printf("实际发送数: %" PRIu64 "\n", actually_send);

    if (server_recv == UINT64_MAX)
        printf("服务器接收数: 查询失败\n");
    else
        printf("服务器接收数: %" PRIu64 "\n", server_recv);

    if (database_insert_cnt == UINT64_MAX)
        printf("数据库插入数: 查询失败\n");
    else
        printf("数据库插入数: %" PRIu64 "\n", database_insert_cnt);

    printf("====================================\n");
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        printf("[ERROR] 错误命令, 正确的: \"./stress_sensor [数量] [间隔时间] [测试时间]\"\n");
        return -1;
    }

    load_config("./../../config/gateway.conf");
    handle_arguments(argv);

    // 先建立压力测试控制连接
    int control_fd = connect_control_socket();
    if (control_fd < 0)
        return -1;

    char response[256] = {0};
    if (send_control_command(control_fd, STRESS_START_TYPE) < 0 ||
        recv_control_response(control_fd, response, sizeof(response)) < 0)
    {
        fprintf(stderr, "压力测试START命令失败\n");
        close(control_fd);
        return -1;
    }
    printf("服务器: %s", response);

    open_database();
    uint64_t database_cnt1 = get_database_cnt();    //开始测试前数据库里存的数量

    pthread_t threads[MAX_SENSOR_CNT];  //线程数组
    thread_stat_t stats[MAX_SENSOR_CNT];    //每个线程的状态
    int thread_created[MAX_SENSOR_CNT] = {0};   //标记该线程是否被成功创建

    memset(stats, 0, sizeof(stats));

    int failed_thread_cnt = 0;
    for (int i = 0; i < sensor_cnt; i++)
    {
        int ret = pthread_create(&threads[i], NULL, thread_func, &stats[i]);
        if (ret != 0)
        {
            fprintf(stderr, "传感器%d线程创建失败: %s\n", i, strerror(ret));
            failed_thread_cnt++;
            continue;
        }

        thread_created[i] = 1;
    }

    printf("%d个传感器线程已成功创建，压力测试中...\n",
           sensor_cnt - failed_thread_cnt);

    // 必须先join，保证所有线程结束后再读取统计数据
    uint64_t actually_send = 0;
    uint64_t send_fail = 0;
    int connect_success = 0;

    for (int i = 0; i < sensor_cnt; i++)
    {
        if (thread_created[i])
        {
            pthread_join(threads[i], NULL);

            actually_send += stats[i].send_success;
            send_fail += stats[i].send_fail;
            connect_success += stats[i].connect_success;
        }
    }

    printf("压力测试结束，TCP连接成功: %d, send失败: %" PRIu64 "\n",
           connect_success, send_fail);

    // 查询服务器在本次START~END测试区间内成功解包的完整协议包数量
    memset(response, 0, sizeof(response));
    if (send_control_command(control_fd, STRESS_QUERY_TYPE) < 0 ||
        recv_control_response(control_fd, response, sizeof(response)) < 0)
    {
        fprintf(stderr, "查询服务器接收数失败\n");
        close(control_fd);
        return -1;
    }

    uint64_t server_recv = UINT64_MAX;
    if (sscanf(response, "STRESS_RECV_SUCCESS=%" SCNu64, &server_recv) != 1)
    {
        fprintf(stderr, "服务器接收数回复格式错误: %s", response);
        server_recv = UINT64_MAX;
    }

    // 结束本次压力测试，让服务器不再继续把普通传感器消息计入测试统计
    memset(response, 0, sizeof(response));
    if (send_control_command(control_fd, STRESS_END_TYPE) < 0 ||
        recv_control_response(control_fd, response, sizeof(response)) < 0)
    {
        fprintf(stderr, "压力测试END命令失败\n");
    }
    else
    {
        printf("服务器: %s", response);
    }

    close(control_fd);

    uint64_t database_cnt2 = get_database_cnt();    //压力测试结束后，数据库
    uint64_t database_insert_cnt = UINT64_MAX;
    if (database_cnt1 != UINT64_MAX && database_cnt2 != UINT64_MAX &&
        database_cnt2 >= database_cnt1)
    {
        database_insert_cnt = database_cnt2 - database_cnt1;
    }

    summary(actually_send, server_recv, database_insert_cnt);

    if (g_db != NULL)
        sqlite3_close(g_db);

    return 0;
}
