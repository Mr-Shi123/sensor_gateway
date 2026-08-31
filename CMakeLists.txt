cmake_minimum_required(VERSION 3.10)
project(sensor_gateway1 C)
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -g -O0 -fsanitize=address -fno-omit-frame-pointer -pthread")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -g -O0 -pthread")

include_directories(${CMAKE_SOURCE_DIR}/include)

# 查找依赖库
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# 收集c文件
file(GLOB SOURCES "src/*.c")

# 生成可执行文件
add_executable(gateway ${SOURCES})

# 链接库
target_link_libraries(gateway
     SQLite::SQLite3
    ${CMAKE_THREAD_LIBS_INIT}
)

# 设置输出路径,在build/bin/
set_target_properties(gateway PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
