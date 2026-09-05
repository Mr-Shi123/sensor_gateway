#include "protocol.h"

uint8_t* pack_message(uint8_t type, const char* msg, uint32_t* out_len)
{
    uint32_t msg_len = strlen(msg);
    *out_len = sizeof(protocol_header_t) + msg_len;

    //给包分配空间
    uint8_t* packet = calloc(1, *out_len);
    //给协议头赋值
    protocol_header_t* ptr = (protocol_header_t*)packet;
    ptr->magic = htons(MAGIC);
    ptr->type = type;
    ptr->len = htonl(msg_len);
    //写入数据
    memcpy(packet + sizeof(protocol_header_t), msg, msg_len);

    return packet;
}

/**
 * @brief 封包验证函数(魔数是否正确，数据是否完整)
 * @param packet 要验证的封包
 * @param recv_len 接收的字节数
 * @return 1：验证成功, MAGIC_FAILED：验证失败, INCOMPLETE_DATA:数据不完整
 */
static int is_valid_packet(const uint8_t* packet, uint32_t recv_len)
{
    //是否有协议头
    if(recv_len < sizeof(protocol_header_t))
        return INCOMPLETE_DATA;

    //校验魔数
    protocol_header_t* ptr = (protocol_header_t*)packet;
    if(ntohs(ptr->magic) != MAGIC)
        return MAGIC_FAILED;

    //数据是否完整
    uint32_t msg_len = ntohl(ptr->len);
    if(recv_len < sizeof(protocol_header_t) + msg_len)
        return INCOMPLETE_DATA;

    return 1;
}

int unpack_message(const uint8_t* packet, uint32_t recv_len, uint8_t* type, char** out_msg, int* consumed)
{
    *consumed = 0;
    int res = is_valid_packet(packet, recv_len);
    if(res < 0)
    {
        *out_msg = NULL;
        return res;
    }

    //提取类型和数据
    protocol_header_t* ptr = (protocol_header_t*)packet;
    *type = ptr->type;
    uint32_t data_len = ntohl(ptr->len);
    *out_msg = malloc(data_len + 1);
    memcpy(*out_msg, packet + sizeof(protocol_header_t), data_len);
    (*out_msg)[data_len] = '\0';

    *consumed = sizeof(protocol_header_t) + data_len;
    return UNPACK_SUCCESS;
}