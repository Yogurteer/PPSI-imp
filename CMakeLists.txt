cmake_minimum_required(VERSION 3.15)
project(LPSI_Implementation)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找必要的库
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 源文件
file(GLOB SOURCES "src/*.cpp")

# 创建可执行文件
add_executable(psiTest ${SOURCES})

# 链接库
target_link_libraries(psiTest 
    OpenSSL::SSL 
    OpenSSL::Crypto
    ${Boost_LIBRARIES}
)

# 编译选项
target_compile_options(psiTest PRIVATE 
    -Wall 
    -Wextra 
    -O3
    -march=native
)

# 如果是Debug模式，添加调试信息
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(psiTest PRIVATE -g -O0)
endif()

# 安装目标
# install(TARGETS psiTest DESTINATION bin)