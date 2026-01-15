#!/bin/bash

set -e  # 遇到错误立即退出

# ================= 参数配置 =================
# Sender Size: 2^8, 2^12, 2^16, 2^20
SENDER_SIZES=(256 4096 65536 1048576)

# Receiver Size: 1, 2^6, 2^7, 2^8, 2^9, 2^10, 2^11, 2^12
RECEIVER_SIZES=(1 64 128 256 512 1024 2048 4096)

# 固定参数
LABEL_SIZE=32
ITEM_SIZE=16
PIR_MODE=0
PRINT_MODE='SIMPLE'  # SIMPLE or DETAILED

# 路径配置
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
DATA_DIR="$SCRIPT_DIR/data"
BUILD_DIR="$SCRIPT_DIR/build"
RESULT_DIR="$SCRIPT_DIR/result/compare"

# 创建结果目录
mkdir -p "$RESULT_DIR"

# 生成时间戳作为结果文件名
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULT_FILE="$RESULT_DIR/batch_test_${TIMESTAMP}.csv"

# 计算总测试数 (只计算 sender_size >= receiver_size 的情况)
TOTAL_TESTS=0
for SENDER_SIZE in "${SENDER_SIZES[@]}"; do
    for RECEIVER_SIZE in "${RECEIVER_SIZES[@]}"; do
        if [ $SENDER_SIZE -ge $RECEIVER_SIZE ]; then
            TOTAL_TESTS=$((TOTAL_TESTS + 1))
        fi
    done
done

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
echo "Sender,Receiver,Inter,Label_byte,Item_byte,Sum_online,OPRF_on,Gen_idx_on,Query_on,OT_on,Dec_on,Sum_offline,OPRF_off,Gen_idx_off,PIR_off,OT_off,Com" > "$RESULT_FILE"
echo ""

# ================= 批量测试循环 =================
echo -e "\033[1;36m========================================\033[0m"
echo -e "\033[1;36m    Batch General PSI Performance Test\033[0m"
echo -e "\033[1;36m========================================\033[0m"
echo "Sender Sizes: ${SENDER_SIZES[@]}"
echo "Receiver Sizes: ${RECEIVER_SIZES[@]}"
echo "Label Size: $LABEL_SIZE bytes"
echo "Item Size: $ITEM_SIZE bytes"
echo "PIR Mode: $PIR_MODE"
echo "Intersection = Receiver (100% overlap)"
echo -e "\033[1;36m========================================\033[0m"
echo ""

CURRENT_TEST=0
FAILED_TESTS=0

for SENDER_SIZE in "${SENDER_SIZES[@]}"; do
    # 计算Sender Size对应的2的幂次
    SENDER_POWER=$(echo "l($SENDER_SIZE)/l(2)" | bc -l | xargs printf "%.0f")
    
    for RECEIVER_SIZE in "${RECEIVER_SIZES[@]}"; do
        # 跳过 sender size < receiver size 的情况
        if [ $SENDER_SIZE -lt $RECEIVER_SIZE ]; then
            continue
        fi
        
        CURRENT_TEST=$((CURRENT_TEST + 1))
        
        # 交集大小等于Receiver Size
        INTERSECTION_SIZE=$RECEIVER_SIZE
        
        # 计算Receiver Size对应的2的幂次
        if [ $RECEIVER_SIZE -eq 1 ]; then
            RECEIVER_DESC="1"
        else
            RECEIVER_POWER=$(echo "l($RECEIVER_SIZE)/l(2)" | bc -l | xargs printf "%.0f")
            RECEIVER_DESC="2^$RECEIVER_POWER"
        fi
        
        echo -e "\033[1;33m[Test $CURRENT_TEST/$TOTAL_TESTS]\033[0m Sender=2^$SENDER_POWER ($SENDER_SIZE) | Receiver=$RECEIVER_DESC ($RECEIVER_SIZE)"
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
                echo "$SENDER_SIZE,$RECEIVER_SIZE,$INTERSECTION_SIZE,$LABEL_SIZE,$ITEM_SIZE,-,-,-,-,-,-,-,-,-,-,-,-" >> "$RESULT_FILE"
                FAILED_TESTS=$((FAILED_TESTS + 1))
                echo -e "\033[0;31mSkipping this test...\033[0m"
                echo ""
                continue
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
            echo -e "\033[1;33m--- Error Log (Last 10 lines) ---\033[0m"
            echo "$OUTPUT" | tail -n 10
            
            # 记录失败信息
            echo "$SENDER_SIZE,$RECEIVER_SIZE,$INTERSECTION_SIZE,$LABEL_SIZE,$ITEM_SIZE,-,-,-,-,-,-,-,-,-,-,-,-" >> "$RESULT_FILE"
            FAILED_TESTS=$((FAILED_TESTS + 1))
            
            echo -e "\033[0;31mSkipping this test...\033[0m"
            echo ""
            continue
        else
            echo -e "\033[0;32m✓ Execution Success.\033[0m"
            
            # 提取CSV格式的性能指标
            CSV_LINE=$(echo "$OUTPUT" | grep -A 1 "S_size,R_size,I_size,Payload_bytes" | tail -n 1)
            
            if [ -z "$CSV_LINE" ]; then
                echo -e "\033[0;33m⚠ Warning: Could not extract performance metrics\033[0m"
                echo "$SENDER_SIZE,$RECEIVER_SIZE,$INTERSECTION_SIZE,$LABEL_SIZE,$ITEM_SIZE,-,-,-,-,-,-,-,-,-,-,-,-" >> "$RESULT_FILE"
            else
                # CSV_LINE格式: S_size,R_size,I_size,Payload_bytes,Total_on_time,OPRF_on_s,Genidx_r,PIR_query,OT_online,Decrypt,Total_off_time,OPRF_off,Gen_idx_s,PIR_prep,OT_off,Total_com_MB
                # 提取各字段
                TOTAL_ONLINE=$(echo "$CSV_LINE" | cut -d',' -f5)
                OPRF_ON=$(echo "$CSV_LINE" | cut -d',' -f6)
                GEN_IDX_ON=$(echo "$CSV_LINE" | cut -d',' -f7)
                QUERY_ON=$(echo "$CSV_LINE" | cut -d',' -f8)
                OT_ON=$(echo "$CSV_LINE" | cut -d',' -f9)
                DEC_ON=$(echo "$CSV_LINE" | cut -d',' -f10)
                TOTAL_OFFLINE=$(echo "$CSV_LINE" | cut -d',' -f11)
                OPRF_OFF=$(echo "$CSV_LINE" | cut -d',' -f12)
                GEN_IDX_OFF=$(echo "$CSV_LINE" | cut -d',' -f13)
                PIR_OFF=$(echo "$CSV_LINE" | cut -d',' -f14)
                OT_OFF=$(echo "$CSV_LINE" | cut -d',' -f15)
                COM_MB=$(echo "$CSV_LINE" | cut -d',' -f16)
                
                # 写入结果文件
                echo "$SENDER_SIZE,$RECEIVER_SIZE,$INTERSECTION_SIZE,$LABEL_SIZE,$ITEM_SIZE,$TOTAL_ONLINE,$OPRF_ON,$GEN_IDX_ON,$QUERY_ON,$OT_ON,$DEC_ON,$TOTAL_OFFLINE,$OPRF_OFF,$GEN_IDX_OFF,$PIR_OFF,$OT_OFF,$COM_MB" >> "$RESULT_FILE"
                
                # 根据模式打印输出
                if [ "$PRINT_MODE" == "DETAILED" ]; then
                    echo "$OUTPUT"
                else
                    # SIMPLE 模式：显示性能数据
                    echo "  Online: ${TOTAL_ONLINE}s | Offline: ${TOTAL_OFFLINE}s | Comm: ${COM_MB}MB"
                fi
            fi
        fi
        
        echo ""
        
    done
done

# ================= 测试完成总结 =================
echo -e "\033[1;32m========================================\033[0m"
echo -e "\033[1;32m      Batch Test Completed!\033[0m"
echo -e "\033[1;32m========================================\033[0m"
echo "Total tests: $TOTAL_TESTS"
echo "Successful: $((TOTAL_TESTS - FAILED_TESTS))"
echo "Failed: $FAILED_TESTS"
echo "Results saved to: $RESULT_FILE"
echo ""

# 显示结果文件预览
echo -e "\033[1;36m========== Result Preview ==========\033[0m"
head -n 5 "$RESULT_FILE"
echo "..."
echo -e "\033[1;36m====================================\033[0m"
