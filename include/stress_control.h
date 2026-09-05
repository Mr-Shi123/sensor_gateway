#ifndef __STRESS_CONTROL_H_
#define __STRESS_CONTROL_H_

#include <stdint.h>

/* 压力测试控制连接使用 gateway 业务端口 + 1 */
#define STRESS_CONTROL_PORT_OFFSET 1

/* 压力测试控制命令 */
#define STRESS_START_TYPE 100
#define STRESS_END_TYPE   101
#define STRESS_QUERY_TYPE 102

#endif
