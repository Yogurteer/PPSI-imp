#!/bin/bash

set -e  # 遇到错误立即退出

# 颜色定义
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  可计费LPSI协议 - 编译和运行${NC}"
echo -e "${BLUE}========================================${NC}"

# 获取脚本所在目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

rm -rf build

# 创建build目录
if [ ! -d "build" ]; then
    echo -e "${GREEN}创建build目录...${NC}"
    mkdir -p build
fi

cd build

# 运行CMake (静默模式)
echo -e "${GREEN}运行CMake配置...${NC}"
cmake .. > /dev/null 2>&1
# cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake配置失败!${NC}"
    exit 1
fi

# 编译 (静默模式)
echo -e "${GREEN}开始编译...${NC}"
make -j$(nproc) > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo -e "${RED}编译失败!${NC}"
    echo -e "${RED}重新运行以查看详细错误信息: make -j$(nproc)${NC}"
    exit 1
fi

echo -e "${GREEN}编译成功!${NC}"
echo ""

# 运行程序
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  run${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# m=0 direct mode m=1 default mode
./bin/lpsi_test -x 1048576 -y 1024 -p 1 -m 0

if [ $? -ne 0 ]; then
    echo -e "${RED}程序运行失败!${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}测试完成!${NC}"
