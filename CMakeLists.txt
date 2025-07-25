cmake_minimum_required(VERSION 3.10)
project(dns_parse CXX)

# 指定使用 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 构建类型设置
set(CMAKE_BUILD_TYPE Debug)  # 可改为Release用于生产环境
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-Wall -Wextra -g -O0)
    message(STATUS "Building in DEBUG mode")
else()
    add_compile_options(-Wall -O2 -mtune=native)
    message(STATUS "Building in RELEASE mode")
endif()

# 目录定义
set(SRC_DIR "${PROJECT_SOURCE_DIR}/src")
set(INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")
set(CONF_DIR "${PROJECT_SOURCE_DIR}/conf")
set(LIB_DIR "${PROJECT_SOURCE_DIR}/lib")

# 安装前缀（可通过 -DCMAKE_INSTALL_PREFIX=xxx 覆盖）
if(NOT CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX /usr/local)
endif()

# 查找源文件
file(GLOB_RECURSE SOURCES "${SRC_DIR}/*.cpp")
include_directories(${INCLUDE_DIR})

# 构建目标
add_executable(dns_parse ${SOURCES})

find_package(nlohmann_json 3.2.0 REQUIRED)
# 链接依赖库（pthread, pcap, curl hiredis）
target_link_libraries(dns_parse pthread pcap curl hiredis)

# 安装目标二进制到 /usr/local/bin
install(TARGETS dns_parse DESTINATION bin)

# 安装配置文件到 openresty 目录
install(FILES ${CONF_DIR}/dns_parse.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/openresty/nginx/conf)

# 自定义安装后的 nginx 配置测试命令（可选）
add_custom_target(
    test-conf
    COMMAND sudo ${CMAKE_INSTALL_PREFIX}/openresty/bin/openresty -t -p ${CMAKE_INSTALL_PREFIX}/openresty/nginx
    COMMENT "Testing nginx configuration"
)

# 自定义清理目标（CMake 的 clean 已内建）
add_custom_target(
    uninstall-conf
    COMMAND sudo sed -i '/include dns_parse.conf;/d' ${CMAKE_INSTALL_PREFIX}/openresty/nginx/conf/nginx.conf
    COMMAND sudo rm -vf ${CMAKE_INSTALL_PREFIX}/openresty/nginx/conf/dns_parse.conf
    COMMENT "Uninstalled nginx configuration"
)
