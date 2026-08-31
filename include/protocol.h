#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#define MAGIC 0xAA55
#define SENSOR_DATA_TYPE 0x01   //传感器数据类型
#define GET_DATA_TYPE 0x02  //获取数据类型
#define PING_TYPE 0x03  //更新心跳时间
#define OTHER_TYPE 0x04

typedef struct {
    uint16_t magic;     //魔数
    uint8_t type;       //类型
    uint32_t len;       //数据字节长度
}protocol_header_t;

/**
 * @brief 封包函数
 * @param type 数据类型
 * @param msg 数据信息
 * @param out_len 封包总字节长度
 * @return 封包指针
 */
uint8_t* pack_message(uint8_t type, const char* msg, uint32_t* out_len);

/**
 * @brief 解包函数，获取包中的数据类型和数据
 * @param packet 要解析的包
 * @param recv_len 接收的字节数
 * @param type 保存数据类型结果
 * @param out_msg 保存数据结果
 * @param consumed 已处理字节数
 * @return 1：解包成功, -1：魔数验证失败, -2:数据不完整
 */
int unpack_message(const uint8_t* packet, uint32_t recv_len, uint8_t* type, char** out_msg, int* consumed);

#endif