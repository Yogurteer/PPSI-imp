#!/bin/bash

set -e  # 遇到错误立即退出

# ================= 固定参数配置 =================
SENDER_SIZE=1048576 
LABEL_SIZE=1             
ITEM_SIZE=16               
PIR_MODE=0               
PRINT_MODE='SIMPLE'  # SIMPLE or DETAILED

# Receiver Size测试范围: 1, 2^6, 2^7, 2^8, 2^9, 2^10, 2^11, 2^12
# Intersection Size与Receiver Size一致
RECEIVER_SIZES=(1 64 128 256 512 1024 2048 4096)

# 路径配置
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
DATA_DIR="$SCRIPT_DIR/data"
BUILD_DIR="$SCRIPT_DIR/build"
RESULT_DIR="$SCRIPT_DIR/result/psi"

# 创建结果目录
mkdir -p "$RESULT_DIR"

# 生成时间戳作为结果文件名
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULT_FILE="$RESULT_DIR/pure_psi_${TIMESTAMP}.csv"

# 计算总测试数
TOTAL_TESTS=${#RECEIVER_SIZES[@]}

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
echo ""

# ================= 初始化结果文件 =================
echo "Writing results to: $RESULT_FILE"
echo "Sender_Size,Receiver_Size,Intersection_Size,Label_Size,Item_Size,PIR_Mode,Status,Total_Online(s),Total_Offline(s),Communication(MB)" > "$RESULT_FILE"
echo ""

# ================= 批量测试循环 =================
echo -e "\033[1;36m========================================\033[0m"
echo -e "\033[1;36m  Batch Test: Pure PSI Performance\033[0m"
echo -e "\033[1;36m========================================\033[0m"
echo "Sender Size: $SENDER_SIZE (2^20)"
echo "Label Size: $LABEL_SIZE bytes"
echo "Item Size: $ITEM_SIZE bytes"
echo "PIR Mode: $PIR_MODE"
echo "Intersection Size = Receiver Size (100% overlap)"
echo -e "\033[1;36m========================================\033[0m"
echo ""

CURRENT_TEST=0

for RECEIVER_SIZE in "${RECEIVER_SIZES[@]}"; do
    CURRENT_TEST=$((CURRENT_TEST + 1))
    
    # 交集大小等于Receiver Size
    INTERSECTION_SIZE=$RECEIVER_SIZE
    
    # 计算Receiver Size对应的2的幂次
    if [ $RECEIVER_SIZE -eq 1 ]; then
        SIZE_DESC="1"
    else
        POWER=$(echo "l($RECEIVER_SIZE)/l(2)" | bc -l | xargs printf "%.0f")
        SIZE_DESC="2^$POWER"
    fi
    
    echo -e "\033[1;33m[Test $CURRENT_TEST/$TOTAL_TESTS]\033[0m Receiver Size = $RECEIVER_SIZE ($SIZE_DESC)"
    echo "----------------------------------------"
    
    # 数据集文件名生成
    DATASET_NAME="dataset_${SENDER_SIZE}_${RECEIVER_SIZE}_${INTERSECTION_SIZE}_${LABEL_SIZE}_${ITEM_SIZE}.csv"
    DATASET_PATH="$DATA_DIR/$DATASET_NAME"
    
    # ================= 数据准备 =================
    echo -e "\033[0;34m[Data Check]\033[0m Checking dataset: $DATASET_NAME"
    
    if [ ! -f "$DATASET_PATH" ]; then
        echo -e "\033[1;33mDataset not found. Generating...\033[0m"
        mkdir -p "$DATA_DIR"
        
        python3 "$SCRIPT_DIR/test_data_creator.py" \
            $SENDER_SIZE $RECEIVER_SIZE $INTERSECTION_SIZE $LABEL_SIZE $ITEM_SIZE
            
        if [ $? -ne 0 ]; then
            echo -e "\033[0;31m❌ Error generating dataset!\033[0m"
            echo "$SENDER_SIZE,$RECEIVER_SIZE,$INTERSECTION_SIZE,$LABEL_SIZE,$ITEM_SIZE,$PIR_MODE,FAILED,-,-,-" >> "$RESULT_FILE"
            echo -e "\033[0;31m\n🛑 Test aborted due to data generation failure.\033[0m"
            exit 1
        fi
    else
        echo -e "\033[0;32mDataset exists.\033[0m"
    fi
    
    # ================= 运行测试 =================
    echo -e "\033[0;34m[Run]\033[0m Executing protocol..."
    
    # 临时关闭 set -e 以捕获程序返回值
    set +e
    
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
        echo -e "\033[0;31m❌ Execution Failed (Code: $RET_CODE)\033[0m"
        echo -e "\033[1;33m--- Error Log (Last 15 lines) ---\033[0m"
        echo "$OUTPUT" | tail -n 15
        
        # 记录失败信息
        echo "$SENDER_SIZE,$RECEIVER_SIZE,$INTERSECTION_SIZE,$LABEL_SIZE,$ITEM_SIZE,$PIR_MODE,FAILED,-,-,-" >> "$RESULT_FILE"
        
        echo -e "\033[0;31m\n🛑 Test aborted due to execution failure.\033[0m"
        echo -e "Results saved to: $RESULT_FILE"
        exit 1
    else
        echo -e "\033[0;32m✓ Execution Success.\033[0m"
        
        # 提取性能指标
        ONLINE_TIME=$(echo "$OUTPUT" | grep "Total_online:" | awk '{print $2}')
        OFFLINE_TIME=$(echo "$OUTPUT" | grep "Total_offline:" | awk '{print $2}')
        COMMUNICATION=$(echo "$OUTPUT" | grep "Com(MB):" | awk '{print $2}')
        
        # 如果提取失败,使用 - 占位
        [ -z "$ONLINE_TIME" ] && ONLINE_TIME="-"
        [ -z "$OFFLINE_TIME" ] && OFFLINE_TIME="-"
        [ -z "$COMMUNICATION" ] && COMMUNICATION="-"
        
        # 记录成功及性能数据
        echo "$SENDER_SIZE,$RECEIVER_SIZE,$INTERSECTION_SIZE,$LABEL_SIZE,$ITEM_SIZE,$PIR_MODE,SUCCESS,$ONLINE_TIME,$OFFLINE_TIME,$COMMUNICATION" >> "$RESULT_FILE"
        
        # 根据模式打印输出
        if [ "$PRINT_MODE" == "DETAILED" ]; then
            echo "$OUTPUT"
        else
            # SIMPLE 模式：显示性能数据
            echo "  Online: $ONLINE_TIME s | Offline: $OFFLINE_TIME s | Comm: $COMMUNICATION MB"
        fi
    fi
    
    echo ""
    
done

# ================= 测试完成总结 =================
echo -e "\033[1;32m========================================\033[0m"
echo -e "\033[1;32m  ✓ All Tests Completed Successfully!\033[0m"
echo -e "\033[1;32m========================================\033[0m"
echo "Total tests executed: $TOTAL_TESTS"
echo "Results saved to: $RESULT_FILE"
