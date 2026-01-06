#!/bin/bash

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
echo -e "${GREEN}运行CMake配置...${NC}"
cmake ..
echo -e "${GREEN}开始编译...${NC}"
make -j
if [ $? -ne 0 ]; then
    echo -e "${RED}编译失败!${NC}"
    exit 1
fi

echo -e "${GREEN}编译成功!${NC}"
echo ""
./bin/lpsi_test -x 4096 -y 1024 -p 1 -m 0
# ./bin/lpsi_test -x 256 -y 1 -p 1


