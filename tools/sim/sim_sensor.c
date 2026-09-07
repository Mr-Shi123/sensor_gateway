//  模拟传感器
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#include "utils.h"
#include "protocol.h"

int sockfd;

void* send_thread(void* arg)
{
    while(1)
    {
        char buf[128];
        fgets(buf, sizeof(buf), stdin);
        buf[strcspn(buf, "\n")] = '\0';
        
        uint8_t type = 0x00;
        if(is_useful_data(buf) > 0)
            type = SENSOR_DATA_TYPE;
        else if(strcmp(buf, "get_data") == 0)
            type = GET_DATA_TYPE;
        else if(strcmp(buf, "PING") == 0)
            type = PING_TYPE;
        else
            type = OTHER_TYPE;

        uint32_t packet_len;
        uint8_t* packet =  pack_message(type, buf, &packet_len);
        if(send(sockfd, packet, packet_len, 0) < 0)
        {
            perror("发送失败, 结束连接");
            free(packet);
            close(sockfd);
            break;
        }
        free(packet);
    }
    return NULL;
}

void* recv_thread(void* arg)
{
    uint8_t recv_buf[1024];
    int recv_len = 0;
    while(1)
    {
        int ret = recv(sockfd, recv_buf + recv_len, sizeof(recv_buf) - recv_len, 0);
        if(ret < 0)
        {
            perror("无法接收服务器数据");
            close(sockfd);
            break;
        }
        recv_len += ret;

        while(recv_len >= sizeof(protocol_header_t))
        {
            uint8_t type;
            char* feedback = NULL;
            int consumed = 0;
            int res = unpack_message(recv_buf, recv_len, &type, &feedback, &consumed);
            if(res == -1)
            {
                printf("魔数验证失败，断开连接\n");
                close(sockfd);
                return NULL;
            }
            else if(res == -2)
                break;

            printf("%s", feedback);
            free(feedback);
            recv_len -= consumed;
            memmove(recv_buf, recv_buf + consumed, recv_len);
        }
    }
    return NULL;
}

int main()
{
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9090);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if(connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("连接失败");
        close(sockfd);
        return -1;
    }

    //开启两个线程，1个发送，1个接收
    pthread_t t1, t2;
    pthread_create(&t1, NULL, send_thread, NULL);
    pthread_create(&t2, NULL, recv_thread, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    return 0;
}
