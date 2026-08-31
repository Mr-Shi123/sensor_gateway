# 传感器数据采集网关

> 一个基于 **epoll + 多线程 + SQLite** 的轻量级物联网数据采集网关，支持高并发传感器数据接入。

---

## ✨ 特性

- **epoll 事件驱动**：单线程处理数千并发连接，CPU 占用极低
- **生产者-消费者队列**：异步解耦，主线程只负责收包，后台批量写入数据库
- **SQLite 持久化**：支持事务批量写入，性能提升 10 倍+
- **心跳保活机制**：自动踢掉超时客户端，防止僵尸连接
- **模块化设计**：配置、日志、数据库、队列、网络完全解耦
- **配置文件驱动**：端口、超时断开时间、消息队列大小、日志级别全部可配置

---

## 📁 项目结构

```
sensor_gateway/
├── config/
│   └── gateway.conf      # 配置文件
├── include/              # 头文件
│   └── config.h
│   └── database.h
│   └── log.h
│   └── protocol.h
│   └── queue.h
│   └── server.h
│   └── thread.h
│   └── utils.h
├── src/                  # 源文件
│   ├── config.c
│   ├── database.c
│   ├── log.c
│   └── protocol.h
│   ├── main.c
│   ├── queue.c
│   ├── server.c
│   ├── thread.c
│   └── utils.c
├── build/                # 编译输出
├── CMakeLists.txt        # cmake文件
├── toolchain.cmake       # 交叉编译链接文件
├── tools                 # 工具包
|   └── sim_sensor.c      # 模拟传感器
└── README.md
```

---

## 🚀 快速开始

### 编译
**1.本地编译**
```bash
# 创建进入build文件夹
mkdir -p build && cd build
# 执行CMake
cmake ..
# 执行Makefile
make
```
**2.交叉编译到开发版**
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake
make
```

### 运行
```bash
./launch
```

### 测试

```bash
# 连接网关
nc localhost 9090
#或者
nc localhost 8080

# 发送传感器数据
Temp:25.5, Hum:60.2

# 查询历史数据
get_data
```

---

## 📊 数据库表结构

```sql
CREATE TABLE sensor_data (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    time TEXT NOT NULL,
    temp REAL,
    hum REAL
);
```

---

## ⚙️ 配置说明

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `port` | 监听端口 | 8080 |
| `max_clients` | 最大客户端数 | 90 |
| `heartbeat_timeout` | 心跳超时（秒） | 50 |
| `queue_max_size` | 消息队列最大容量 | 900 |
| `batch_size` | 批量写入大小 | 9 |
| `db_file` | 数据库文件路径 | ./sensor_data_tmp.db |
| `log.file` | 日志文件路径 | ./log/gateway_tmp.log |

---

## 🏗️ 架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                        传感器客户端                               │
│                  (TCP 发送 Temp:xx, Hum:xx)                      │
│                  (TCP 发送 get_data)                             │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                     epoll 主线程（接收层）                         │
│  • 非阻塞 accept / recv                                          │
│  • 心跳超时检查                                                   │
│  • 命令解析（get_data / 传感器数据）                                │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                      消息队列（生产者-消费者）                      │
│                    线程安全阻塞队列                                │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                    工作线程池（消费者层）                           │
│  • 批量取出消息                                                   │
│  • 开启事务，批量写入 SQLite                                       │
└─────────────────────────────────────────────────────────────────┘
```

---
