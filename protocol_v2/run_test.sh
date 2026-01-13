#!/bin/bash

set -e  # 遇到错误立即退出

# 设置参数
SENDER_SIZE=4096        # 2^20 items
RECEIVER_SIZE=256       
INTERSECTION_SIZE=1024
LABEL_SIZE=1            
ITEM_SIZE=8               # 16 bytes
PIR_MODE=0               # 1 default 0 direct
THREAD_COUNT=1
TESTID="1"
Label="${SENDER_SIZE}_${RECEIVER_SIZE}_${INTERSECTION_SIZE}_${LABEL_SIZE}_${ITEM_SIZE}"

DATASET_FILE="data/dataset_${Label}.csv"

# 颜色定义
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}Run our scheme PPSI${NC}"

# 获取脚本所在目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

rm -rf build

# 创建build目录
if [ ! -d "build" ]; then
    # echo -e "${GREEN}创建build目录...${NC}"
    mkdir -p build
fi

cd build

# 运行CMake (静默模式)
echo -e "${GREEN}Build...${NC}"
cmake .. > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed!${NC}"
    exit 1
fi

# 编译 (静默模式)
echo -e "${GREEN}Make...${NC}"
make -j$(nproc) > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo -e "${RED}Compilation failed!${NC}"
    echo -e "${RED}Re-run to see detailed error messages: make -j$(nproc)${NC}"
    exit 1
fi

echo -e "${GREEN}Make success${NC}"
echo ""

# 运行程序
echo -e "${BLUE}Start Run${NC}"

echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC}"
echo -e "Scale: Sender=$SENDER_SIZE Receiver=$RECEIVER_SIZE"
echo ""

# 临时关闭 set -e，防止程序失败时脚本立即退出
set +e
# 运行程序，捕获 stdout 和 stderr (2>&1)
OUTPUT=$(./bin/lpsi_test -x "$SENDER_SIZE" -y "$RECEIVER_SIZE" -p "$LABEL_SIZE" -m "$PIR_MODE" 2>&1)
RET_CODE=$?
set -e

if [ $RET_CODE -ne 0 ]; then
    echo -e "${RED}Failed (Exit Code: $RET_CODE)${NC}"
    echo ""
    
    # 打印最后15行错误输出
    echo -e "${YELLOW}========== Output Info (last 15 lines) ==========${NC}"
    echo "$OUTPUT" | tail -n 15
    echo -e "${YELLOW}=========================================${NC}"
    echo ""
    
    # 遇到第一次错误直接退出脚本
    echo -e "${RED}Detected execution error, aborting test!${NC}"
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC}"
    exit 1
else
    echo -e "${GREEN}success${NC}"

    # 使用grep匹配从"CSV Format Output"开始的3行
    RESULT_BLOCK=$(echo "$OUTPUT" | grep -A 2 "CSV Format Output")
    echo "$RESULT_BLOCK"
fi

echo ""
echo -e "${BLUE}Finish Run${NC}"
echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC}"
