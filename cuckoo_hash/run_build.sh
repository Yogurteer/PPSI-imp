#!/bin/bash

# Cuckoo Hash项目构建脚本
echo "正在构建Cuckoo Hash项目..."

# 创建构建目录
if [ ! -d "build" ]; then
    mkdir build
fi

cd build

# 清理之前的构建（可选）
if [ "$1" = "clean" ]; then
    echo "清理之前的构建文件..."
    rm -rf *
fi

# 运行CMake配置
echo "运行CMake配置..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# 检查CMake是否成功
if [ $? -ne 0 ]; then
    echo "CMake配置失败！"
    exit 1
fi

# 编译项目
echo "编译项目..."
make -j$(nproc)

# 检查编译是否成功
if [ $? -ne 0 ]; then
    echo "编译失败！"
    exit 1
fi

echo "构建成功！"
echo "可执行文件位置: build/bin/test_cuckoo_hash"

# 如果传入test参数，则运行测试
if [ "$1" = "test" ] || [ "$2" = "test" ]; then
    echo ""
    echo "运行测试..."
    echo "=========================="
    ./test_cuckoo_hash
    
    # # 运行CTest
    # echo ""
    # echo "运行CTest..."
    # echo "=========================="
    # ctest --verbose
fi

echo ""
echo "构建运行脚本执行完毕"
