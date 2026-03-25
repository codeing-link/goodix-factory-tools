# ============================================================
# CMakeLists.txt — GH Protocol 纯C后端
# 构建方式：
#   mkdir -p build && cd build
#   cmake .. -DCMAKE_BUILD_TYPE=Debug
#   make -j4
#   ./gh_backend        # 模拟器模式
#   ./gh_backend --port /dev/ttyUSB0  # 真实设备
# ============================================================

cmake_minimum_required(VERSION 3.14)
project(gh_backend C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 默认 Debug 构建
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2)
else()
    add_compile_options(-g -O0)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unused-function)

# ============================================================
# 第一步：下载 mongoose（HTTP + WebSocket 单文件库）
# 官网: https://github.com/cesanta/mongoose
# 版本: 7.14（稳定版），下载到 third_party/mongoose/
# ============================================================

set(MONGOOSE_VERSION "7.14")
set(MONGOOSE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mongoose")

file(MAKE_DIRECTORY ${MONGOOSE_DIR})

# 自动下载 mongoose.c（若不存在）
if(NOT EXISTS "${MONGOOSE_DIR}/mongoose.c")
    message(STATUS "[CMake] 正在下载 mongoose ${MONGOOSE_VERSION}...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/cesanta/mongoose/${MONGOOSE_VERSION}/mongoose.c"
        "${MONGOOSE_DIR}/mongoose.c"
        SHOW_PROGRESS
        STATUS DL_STATUS_C
        TIMEOUT 30)
    list(GET DL_STATUS_C 0 DL_CODE)
    if(NOT DL_CODE EQUAL 0)
        message(FATAL_ERROR 
            "[CMake] 下载 mongoose.c 失败！\n"
            "请手动从以下地址下载并放到 third_party/mongoose/ 目录：\n"
            "  https://raw.githubusercontent.com/cesanta/mongoose/${MONGOOSE_VERSION}/mongoose.c\n"
            "  https://raw.githubusercontent.com/cesanta/mongoose/${MONGOOSE_VERSION}/mongoose.h")
    endif()
    message(STATUS "[CMake] mongoose.c 下载成功")
endif()

# 自动下载 mongoose.h（若不存在）
if(NOT EXISTS "${MONGOOSE_DIR}/mongoose.h")
    message(STATUS "[CMake] 正在下载 mongoose.h...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/cesanta/mongoose/${MONGOOSE_VERSION}/mongoose.h"
        "${MONGOOSE_DIR}/mongoose.h"
        SHOW_PROGRESS
        STATUS DL_STATUS_H
        TIMEOUT 30)
    list(GET DL_STATUS_H 0 DL_CODE_H)
    if(NOT DL_CODE_H EQUAL 0)
        message(FATAL_ERROR "[CMake] 下载 mongoose.h 失败")
    endif()
    message(STATUS "[CMake] mongoose.h 下载成功")
endif()

# ============================================================
# 第二步：定义各层源文件
# ============================================================

set(PROTOCOL_SRCS   src/protocol/gh_protocol.c)
set(TRANSPORT_SRCS  src/transport/gh_transport.c)
set(SERVICE_SRCS    src/service/gh_service.c)
set(API_SRCS        src/api/gh_http_server.c)
set(MONGOOSE_SRCS   ${MONGOOSE_DIR}/mongoose.c)

# ============================================================
# 第三步：构建可执行文件
# ============================================================

add_executable(gh_backend
    main.c
    ${PROTOCOL_SRCS}
    ${TRANSPORT_SRCS}
    ${SERVICE_SRCS}
    ${API_SRCS}
    ${MONGOOSE_SRCS}
)

# Include 路径
target_include_directories(gh_backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src   # 业务代码
    ${MONGOOSE_DIR}                   # mongoose.h
)

# mongoose 编译时选项
target_compile_definitions(gh_backend PRIVATE
    MG_TLS=0            # 禁用 TLS（无需 OpenSSL 依赖）
    MG_ENABLE_SSI=0     # 禁用 SSI（Server-Side Includes）
)

# ============================================================
# 第四步：平台链接库
# ============================================================

# pthread（接收线程、模拟器线程）
find_package(Threads REQUIRED)
target_link_libraries(gh_backend PRIVATE Threads::Threads)

# 数学库（模拟器 sin/cos）
if(NOT WIN32)
    target_link_libraries(gh_backend PRIVATE m)
endif()

# macOS 特定
if(APPLE)
    target_link_libraries(gh_backend PRIVATE
        "-framework CoreFoundation"
        "-framework Security"
    )
    message(STATUS "[CMake] macOS 检测到，链接 CoreFoundation + Security")
endif()

# Windows 特定
if(WIN32)
    target_link_libraries(gh_backend PRIVATE ws2_32)
    message(STATUS "[CMake] Windows 检测到，链接 ws2_32")
endif()

# ============================================================
# 第五步：输出信息
# ============================================================

message(STATUS "===================================")
message(STATUS "GH Protocol Backend (C)")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  mongoose   : ${MONGOOSE_DIR}")
message(STATUS "  Web root   : ${CMAKE_CURRENT_SOURCE_DIR}/web/frontend")
message(STATUS "===================================")
message(STATUS "构建命令:")
message(STATUS "  cd build && make -j4")
message(STATUS "运行命令:")
message(STATUS "  ./gh_backend                    # 模拟器模式")
message(STATUS "  ./gh_backend --port /dev/ttyUSB0 # 真实设备")
