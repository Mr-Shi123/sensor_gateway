# sensor_gateway

一个基于 **C / Linux / TCP / epoll / pthread / SQLite** 实现的传感器网关项目。

项目主要用于学习和实践：

- TCP 网络编程
- `epoll` 边缘触发（ET）
- TCP 粘包/拆包处理
- 自定义二进制协议
- 多线程生产者/消费者模型
- pthread 线程同步
- SQLite 数据库存储
- 模拟传感器与压力测试

---

## 1. 项目功能

网关程序主要完成以下工作：

1. 监听 TCP 端口，接受传感器连接。
2. 使用 `epoll` 管理多个传感器连接。
3. 接收传感器发送的协议数据，并保存到每个传感器自己的接收缓冲区。
4. 根据协议头中的长度字段进行完整协议包解析，处理 TCP 粘包和拆包。
5. 支持传感器数据上传、数据查询、心跳延长等消息类型。
6. 通过线程安全队列把接收到的传感器数据交给数据库工作线程。
7. 批量写入 SQLite 数据库。
8. 提供模拟传感器程序，用于功能测试。
9. 提供压力测试程序，用于测试并发连接、消息发送、服务器接收和数据库写入能力。

---

## 2. 系统架构

整体数据流如下：

```text
                         +----------------------+
                         |      stress_sensor   |
                         |    压力测试程序       |
                         +----------+-----------+
                                    |
                         多个 TCP 传感器连接
                                    |
                                    v
+----------------+        +----------------------+
|  sim_sensor    | -----> |      Gateway         |
|  模拟传感器     |  TCP   |                      |
+----------------+        |  epoll / TCP Server  |
                          +----------+-----------+
                                     |
                                     | unpack_message()
                                     v
                              +-------------+
                              | Thread-safe  |
                              |    Queue     |
                              +------+-------+
                                     |
                                     | pop()
                                     v
                              +-------------+
                              | DB Worker    |
                              |  pthread     |
                              +------+-------+
                                     |
                                     v
                              +-------------+
                              |   SQLite DB  |
                              +-------------+
```

### 网络线程

网关主线程负责：

```text
accept
  -> epoll
  -> recv
  -> 接收缓冲区
  -> unpack_message
  -> 根据消息类型处理
  -> push(queue)
```

### 数据库工作线程

数据库线程负责：

```text
pop(queue)
  -> 批量获取消息
  -> SQLite INSERT
```

项目当前采用生产者/消费者模型：网络线程负责生产数据，数据库工作线程负责消费数据。

---

## 3. 项目目录

```text
sensor_gateway/
├── config/
│   └── gateway.conf          # 网关配置文件
│
├── include/
│   ├── config.h
│   ├── database.h
│   ├── log.h
│   ├── protocol.h
│   ├── queue.h
│   ├── server.h
│   ├── thread.h
│   └── utils.h
│
├── src/
│   ├── config.c              # 配置文件解析
│   ├── database.c            # SQLite 数据库操作
│   ├── log.c                 # 日志功能
│   ├── main.c                # 程序入口
│   ├── queue.c               # 线程安全队列
│   ├── server.c              # TCP + epoll 服务器
│   ├── thread.c              # 数据库工作线程
│   └── utils.c               # 工具函数
│
├── build/                    # CMake 编译输出
│
├── tools/
│   ├── sim/
│   │   ├── gcc.sh            # 模拟传感器编译脚本
│   │   └── sim_sensor.c      # 单个模拟传感器
│   │
│   └── stress/
│       ├── gcc.sh            # 压力测试编译脚本
│       └── stress_sensor.c   # 多传感器压力测试程序
│
├── CMakeLists.txt
├── toolchain.cmake
└── README.md
```

> 如果实际工程中还有额外的测试文件或头文件，以当前源码目录为准。

---

## 4. 主要模块说明

### 4.1 `server.c`

负责服务器核心网络功能：

- 创建监听 socket
- `bind()` / `listen()`
- 设置非阻塞 socket
- 创建 `epoll`
- 接受传感器连接
- 使用 ET 模式处理 `recv()`
- 维护传感器列表
- 处理连接超时
- 解析传感器协议数据

服务器接收数据时，不依赖一次 `recv()` 对应一个完整协议包，而是先存入传感器自己的 `recv_buf`。

```text
recv()
  |
  v
recv_buf
  |
  v
unpack_message()
  |
  +---- -2：数据不完整 -> 等待下一次 recv
  |
  +---- -1：无效协议 -> 断开连接
  |
  +---- 成功 -> 处理完整协议包
```

### 4.2 `protocol.c`

负责协议封装和解析。

核心功能：

```c
pack_message()
unpack_message()
```

协议设计包含协议头和 payload，并通过长度字段确定一个完整协议包的边界，从而处理 TCP 粘包和拆包。

### 4.3 `queue.c`

实现线程安全消息队列。

使用：

- `pthread_mutex_t`
- 条件变量
- `push()`
- `pop()`

网络线程向队列生产消息，数据库线程从队列消费消息。

### 4.4 `thread.c`

负责数据库工作线程。

主要流程：

```text
pop()
  -> 收集 batch
  -> insert_into_db()
```

通过批量写入减少数据库操作次数。

### 4.5 `database.c`

负责 SQLite 数据库：

- 打开数据库
- 创建/使用数据表
- 插入传感器数据
- 查询数据
- 事务处理

### 4.6 `config.c`

读取 `config/gateway.conf`，初始化全局配置 `g_config`。

典型配置包括：

```text
server_config.port
server_config.max_sensors
server_config.heartbeat_timeout
queue_config.max_size
queue_config.batch_size
database_config.db_file
log_config.log_file
```

---

## 5. 编译

### 5.1 使用 CMake

进入项目目录：

```bash
cd sensor_gateway
```

创建并进入构建目录：

```bash
mkdir -p build
cd build
```

配置：

```bash
cmake ..
```

编译：

```bash
make -j$(nproc)
```

生成的网关程序通常位于：

```text
build/bin/gateway
```

### 5.2 编译前提

需要安装：

- GCC
- CMake
- pthread
- SQLite3 开发库

Ubuntu 示例：

```bash
sudo apt update
sudo apt install build-essential cmake libsqlite3-dev
```

---

## 6. 配置文件

配置文件：

```text
config/gateway.conf
```

示例：

```ini
port=9090
max_sensors=1000
heartbeat_timeout=60
queue_max_size=1000
batch_size=10
db_file=sensor_data.db
log_file=./log/gateway.log
```

实际键名以 `config.c` 中的配置解析代码和当前 `gateway.conf` 为准。

其中比较重要的参数：

### `port`

网关监听端口。

例如：

```text
9090
```

### `max_sensors`

允许的最大传感器连接数量。

### `heartbeat_timeout`

传感器超过该时间没有操作时，服务器会主动断开连接。

### `queue_max_size`

消息队列最大容量。

如果生产速度长期高于消费速度，队列可能逐渐增长并最终达到上限。

### `batch_size`

数据库线程一次批量处理的数据量。

---

## 7. 启动 Gateway

根据构建目录运行，例如：

```bash
./build/bin/gateway
```

启动后会监听配置文件中的端口。

例如：

```text
服务器启动成功, 端口 9090
```

---

## 8. 模拟传感器

模拟传感器位于：

```text
tools/sim/sim_sensor.c
```

它用于模拟一个真实传感器客户端，建立 TCP 连接后可以发送不同类型的协议消息。

典型流程：

```text
sim_sensor
    |
    +--> connect Gateway
    |
    +--> SENSOR_DATA_TYPE
    |
    +--> GET_DATA_TYPE
    |
    +--> PING_TYPE
    |
    +--> 接收服务器反馈
```

编译脚本：

```bash
cd tools/sim
./gcc.sh
```

然后按照程序支持的参数启动模拟传感器。

---

## 9. 压力测试

压力测试程序位于：

```text
tools/stress/stress_sensor.c
```

设计方式是：

```text
stress_sensor
├── sensor 0 socket
├── sensor 1 socket
├── sensor 2 socket
├── ...
├── sensor N socket
│
└── control socket
```

每一个传感器线程拥有自己的 TCP 连接，并按照指定时间间隔发送传感器数据。

控制连接用于：

- 开始测试
- 结束测试
- 查询服务器本次测试期间成功接收的协议包数量

压力测试结果重点观察三个指标：

```text
实际发送数
服务器接收数
数据库插入数
```

这三个数字分别对应：

```text
stress_sensor
      |
      | send_all() 成功
      v
实际发送数
      |
      | TCP
      v
Gateway unpack_message() 成功
      |
      v
服务器接收数
      |
      | Queue -> DB worker
      v
数据库插入数
```

### 压力测试示例

例如：

```bash
./stress_sensor 100 10 1
```

参数含义：

```text
100   -> 100 个传感器连接
10    -> 每 10 秒发送一条消息
1     -> 持续 1 分钟
```

测试完成后，重点观察：

```text
===============测试总结===============
实际发送数: xxx
服务器接收数: xxx
数据库插入数: xxx
====================================
```

### 压力测试建议

建议逐渐增加压力，而不是一开始就直接拉到极限：

```text
10 个连接
  ↓
30 个连接
  ↓
60 个连接
  ↓
90 个连接
  ↓
100 个连接
```

然后逐渐提高消息频率：

```text
10 秒/条
  ↓
5 秒/条
  ↓
1 秒/条
  ↓
更高发送频率
```

重点观察：

- 发送失败数
- 服务器接收数
- 数据库插入数
- Queue 长度
- CPU 使用率
- 内存使用量
- 文件描述符数量
- 是否出现连接断开
- 是否出现协议解析错误
- 是否出现数据库错误

---

## 10. 压力测试结果的判断

### 正常情况

例如：

```text
实际发送数: 600
服务器接收数: 600
数据库插入数: 600
```

表示从客户端发送到服务器解析，再到数据库落盘，数量一致。

### 服务器接收数小于实际发送数

例如：

```text
实际发送数: 600
服务器接收数: 590
数据库插入数: 590
```

优先检查：

- TCP 连接是否异常断开
- `recv()` / `epoll` 处理
- 接收缓冲区是否溢出
- `unpack_message()` 是否解析失败
- 客户端发送是否真正完成整个协议包

### 数据库插入数小于服务器接收数

例如：

```text
实际发送数: 600
服务器接收数: 600
数据库插入数: 580
```

优先检查：

- Queue 是否积压
- 数据库 worker 是否处理不及时
- SQLite 插入错误
- 事务提交情况
- 测试结束时数据库线程是否还有未消费的数据

由于项目使用生产者/消费者模型，服务器接收完成并不代表数据库已经立即完成写入。

---

## 11. TCP 粘包和拆包处理

TCP 是字节流协议，`send()` 的次数和 `recv()` 的次数没有一一对应关系。

例如一个客户端连续发送：

```text
A
B
C
```

服务器可能一次 `recv()` 收到：

```text
ABC
```

也可能分多次收到：

```text
A
BC
```

项目通过以下方式解决：

```text
TCP recv()
    ↓
传感器 recv_buf
    ↓
unpack_message()
    ↓
根据协议长度判断完整包
    ↓
memmove() 保留剩余字节
```

因此：

- 一个 `recv()` 可以包含多个协议包
- 一个协议包也可以跨多个 `recv()`

统计服务器“收到多少条消息”时，应统计**成功解包的完整协议包数量**，而不是 `recv()` 调用次数。

---

## 12. 调试与内存检查

### 12.1 GDB

建议使用带调试信息的构建：

```bash
gcc -g -O0 ...
```

常用命令：

```gdb
break main
run
next
step
continue
bt
frame
info locals
print variable
info threads
thread apply all bt
```

### 12.2 Core Dump

允许生成 Core：

```bash
ulimit -c unlimited
```

检查：

```bash
cat /proc/sys/kernel/core_pattern
```

如果程序崩溃并生成 core，可以使用：

```bash
gdb ./gateway core
```

进入后：

```gdb
bt
```

查看崩溃调用栈。

### 12.3 Valgrind

运行：

```bash
valgrind --leak-check=full --show-leak-kinds=all ./build/bin/gateway
```

重点关注：

```text
definitely lost
indirectly lost
possibly lost
still reachable
```

其中 `definitely lost` 是优先级最高的内存泄漏问题。

### 12.4 AddressSanitizer

可以在 CMake 中加入：

```text
-fsanitize=address
-fno-omit-frame-pointer
```

例如：

```cmake
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -g -O0 -fsanitize=address -fno-omit-frame-pointer -pthread")
```

ASan 适合检查：

- heap-buffer-overflow
- stack-buffer-overflow
- use-after-free
- double-free 等

### 12.5 ThreadSanitizer

检查线程数据竞争：

```text
-fsanitize=thread
```

适合检查：

- data race
- 多线程共享变量未同步

---

## 13. 典型问题记录

### 13.1 `%[^:]` 缓冲区溢出

类似：

```c
char name1[32];
sscanf(data, "%[^:]:%lf", name1, &temp);
```

`%[^:]` 没有限制输入长度，可能写爆 `name1`。

更安全的写法：

```c
sscanf(data, "%31[^:]:%lf", name1, &temp);
```

原因：

```text
数组大小 = 32
最大写入字符 = 31
最后 1 个字节留给 '\0'
```

### 13.2 `accept()` 返回 `EAGAIN`

服务器使用非阻塞 socket + ET 模式时，循环 `accept()` 直到没有连接可取是正常做法。

推荐判断：

```c
if (sensorfd < 0)
{
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;

    // 真正的 accept 错误
}
```

`EAGAIN` 在这里不是异常，而是说明当前监听 socket 已经没有更多连接可接受。

### 13.3 `SO_REUSEADDR`

服务器可以在 `bind()` 前设置：

```c
int reuse = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
```

主要用于服务器重启时更方便重新绑定相同的本地地址和端口。

---

## 14. Git 使用

初始化仓库：

```bash
git init
```

查看状态：

```bash
git status
```

提交：

```bash
git add .
git commit -m "commit message"
```

查看日志：

```bash
git log --oneline --decorate
```

创建并切换分支：

```bash
git switch -c debug
```

切换分支：

```bash
git switch master
```

合并：

```bash
git merge debug
```

撤销工作区修改：

```bash
git restore <file>
```

撤销暂存：

```bash
git restore --staged <file>
```

安全撤销一个已经提交的 commit：

```bash
git revert <commit>
```

查看 HEAD 移动历史：

```bash
git reflog
```

---

## 15. 开发与测试建议

建议按下面的顺序进行：

```text
功能正确性
    ↓
GDB 调试
    ↓
Core Dump
    ↓
Valgrind / ASan
    ↓
多线程检查
    ↓
基础压力测试
    ↓
提高并发连接数
    ↓
提高消息发送速率
    ↓
观察 Queue / CPU / 内存 / DB
    ↓
定位性能瓶颈
```

做压力测试时，不建议一开始就使用 Valgrind 进行极限压力测试，因为 Valgrind 会明显降低程序运行速度。更适合先用普通构建建立性能基线，再使用 ASan、Valgrind、TSan 对特定问题进行定位。

---

## 16. 当前压力测试的核心指标

对于本项目，最重要的三个结果是：

```text
实际发送数
服务器接收数
数据库插入数
```

理想情况下：

```text
实际发送数 = 服务器接收数 = 数据库插入数
```

随着压力提高，重点观察第一个开始出现差异的位置。

同时建议观察：

```text
CPU
内存
Queue 当前长度
Queue 最大长度
数据库写入速度
连接成功/失败数量
socket FD 数量
错误日志
```

---

## 17. 后续可扩展方向

可以继续增加：

- 每个传感器的 `sensor_id`
- 每个传感器的消息序号 `sequence`
- 严格的消息丢失检测
- P50 / P95 / P99 延迟统计
- Queue 最大长度统计
- Queue 阻塞次数统计
- 数据库成功/失败计数
- 控制连接查询更多运行时指标
- 更高并发连接压力测试
- 更高消息速率压力测试
- 优雅退出与线程回收
- 更完善的日志等级和日志轮转

---

## 18. 项目定位

这个项目既可以作为一个简单的传感器网关练习，也可以作为 Linux 网络编程、并发编程、故障排查、性能分析和压力测试的综合实践项目。

核心技术栈：

```text
C
Linux
TCP/IP
socket
epoll
pthread
mutex / condition variable
SQLite
CMake
GDB
Valgrind
ASan
TSan
Git
```
