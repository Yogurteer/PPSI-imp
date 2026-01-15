#!/bin/bash

set -e  # 遇到错误立即退出

# 设置参数
SENDER_SIZE=4096        
RECEIVER_SIZE=4096       
INTERSECTION_SIZE=2048
LABEL_SIZE=32            
ITEM_SIZE=8               # 4 enough 2^20 sender size
PIR_MODE=1               # 1 default 0 direct
THREAD_COUNT=1
TESTID="1"
PRINT_MODE='DETAILED'  # SIMPLE or DETAILED

# 路径配置
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
DATA_DIR="$SCRIPT_DIR/data"
BUILD_DIR="$SCRIPT_DIR/build"

# 数据集文件名生成 (与 Python 脚本逻辑一致)
DATASET_NAME="dataset_${SENDER_SIZE}_${RECEIVER_SIZE}_${INTERSECTION_SIZE}_${LABEL_SIZE}_${ITEM_SIZE}.csv"
DATASET_PATH="$DATA_DIR/$DATASET_NAME"

# ================= 数据准备阶段 =================
echo -e "\033[0;34m[Data Check]\033[0m Checking dataset: $DATASET_NAME"

if [ ! -f "$DATASET_PATH" ]; then
    echo -e "\033[1;33mDataset not found. Generating...\033[0m"
    # 确保 data 目录存在
    mkdir -p "$DATA_DIR"
    
    # 调用 Python 脚本生成数据
    # 参数顺序: sender_size receiver_size intersection_size label_byte_count item_byte_count
    python3 "$SCRIPT_DIR/test_data_creator.py" \
        $SENDER_SIZE $RECEIVER_SIZE $INTERSECTION_SIZE $LABEL_SIZE $ITEM_SIZE
        
    if [ $? -ne 0 ]; then
        echo -e "\033[0;31mError generating dataset!\033[0m"
        exit 1
    fi
else
    echo -e "\033[0;32mDataset exists.\033[0m"
fi

# ================= 编译阶段 =================
echo -e "\033[0;34m[Build]\033[0m Compiling..."

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo -e "\033[0;31mCMake failed!\033[0m"
    exit 1
fi

make -j$(nproc) > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo -e "\033[0;31mMake failed!\033[0m"
    exit 1
fi
echo -e "\033[0;32mBuild Success.\033[0m"

# ================= 运行阶段 =================
echo -e "\033[0;34m[Run]\033[0m executing protocol..."
echo "Scale: Sender=$SENDER_SIZE Receiver=$RECEIVER_SIZE Intersection=$INTERSECTION_SIZE"

# 临时关闭 set -e 以捕获 C++ 程序的返回值
set +e

# 注意：增加了 -f 参数传递文件路径
OUTPUT=$(./bin/lpsi_test \
    -x "$SENDER_SIZE" \
    -y "$RECEIVER_SIZE" \
    -i "$INTERSECTION_SIZE" \
    -p "$LABEL_SIZE" \
    -m "$PIR_MODE" \
    -f "$DATASET_PATH" 2>&1)

RET_CODE=$?
set -e

if [ $RET_CODE -ne 0 ]; then
    echo -e "\033[0;31mExecution Failed (Code: $RET_CODE)\033[0m"
    echo -e "\033[1;33m--- Error Log (Last 20 lines) ---\033[0m"
    echo "$OUTPUT" | tail -n 20
    exit 1
else
    echo -e "\033[0;32mExecution Success.\033[0m"
    # 提取 CSV 输出部分
    if [ "$PRINT_MODE" == "DETAILED" ]; then
        echo "$OUTPUT"
    elif [ "$PRINT_MODE" == "SIMPLE" ]; then
        echo "$OUTPUT" | grep -A 4 "Protocol Output"
        echo "$OUTPUT" | grep -A 2 "CSV Format Output"
    fi
fi
