#!/bin/bash

# 颜色定义
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# ================= 配置区域 =================
# Sender 规模: 2^8(256), 2^12(4096), 2^16(65536) 2^20 2^24
SENDER_SIZES=(256 4096 65536 1048576 16777216)
# SENDER_SIZES=(16777216)

# Receiver 规模: 1, 2^6(64), 2^7(128), 2^8(256), 2^9(512), 2^10(1024)
RECEIVER_SIZES=(1 64 128 256 512 1024 2048 4096)

# Payload 大小
PAYLOAD_SIZE=1

# 结果保存文件
RESULT_FILE="../result/benchmark_summary.csv"
RESULT_LOG="../result/benchmark_detailed.log"
# ===========================================

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  LPSI 批量性能测试${NC}"
echo -e "${BLUE}========================================${NC}"

# 1. 编译阶段 (严格保持和你的一致)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

rm -rf build

if [ ! -d "build" ]; then
    echo -e "${GREEN}创建build目录...${NC}"
    mkdir -p build
fi

cd build

echo -e "${GREEN}运行CMake配置...${NC}"
# 【修改】去掉 Release，保持和你的一致
cmake .. > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake配置失败!${NC}"
    exit 1
fi

echo -e "${GREEN}开始编译 (make -j)...${NC}"
# 【修改】使用 make -j，保持和你的一致
make -j > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo -e "${RED}编译失败! 请检查代码错误。${NC}"
    exit 1
fi

# 检查二进制文件是否真的生成了
if [ ! -f "./bin/lpsi_test" ]; then
    echo -e "${RED}错误: 编译显示成功，但 ./bin/lpsi_test 文件不存在!${NC}"
    exit 1
fi

echo -e "${GREEN}编译成功!${NC}"
echo ""

# 2. 初始化 CSV (这里加入了清空逻辑)
# 【修改】显式删除旧文件，防止冲突
if [ -f "$RESULT_FILE" ]; then
    echo -e "${YELLOW}清空旧的结果文件: $RESULT_FILE${NC}"
    rm -f "$RESULT_FILE"
fi
# 写入新的表头 (使用 > 也会覆盖，但上面rm更保险)
echo "Sender,Receiver,Payload,Total_s,OPRF_s,Gen_Idx_s,Query_Idx_s,Get_Key_s,Dec_s" > "$RESULT_FILE"
echo -e "${GREEN}[日志] 结果将保存在: $RESULT_FILE${NC}\n"

# 3. 循环测试
for x in "${SENDER_SIZES[@]}"; do
    for y in "${RECEIVER_SIZES[@]}"; do
        
        # (可选) 跳过 Receiver > Sender 的情况
        if [ "$y" -gt "$x" ]; then continue; fi

        # 打印当前时间(精确到秒)
        echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC}"
        echo -ne "运行: Sender=$x Receiver=$y ... "

        # 运行程序，捕获 stdout 和 stderr (2>&1)
        # 这样即使是 Segmentation fault 也能被 OUTPUT 捕获到一部分信息
        OUTPUT=$(./bin/lpsi_test -x "$x" -y "$y" -p "$PAYLOAD_SIZE" 2>&1)
        RET_CODE=$?

        if [ $RET_CODE -ne 0 ]; then
            echo -e "${RED}失败 (Code: $RET_CODE)${NC}"
            
            # === 核心修改: 打印失败时的详细日志 (最后10行) ===
            echo -e "${YELLOW}--- 错误日志 (最后 10 行) ---${NC}"
            echo "$OUTPUT" | tail -n 10
            echo -e "${YELLOW}-----------------------------${NC}"
            
            # 记录失败到CSV
            echo "$x,$y,$PAYLOAD_SIZE,FAIL,FAIL,FAIL,FAIL,FAIL,FAIL" >> "$RESULT_FILE"
            
            # 遇到第一次错误直接退出脚本
            echo -e "${RED}检测到执行错误,中断测试!${NC}"
            exit 1
        else
            echo -e "${GREEN}成功${NC}"

            # 解析输出并打印最后几行
            RESULT_BLOCK=$(echo "$OUTPUT" | grep -A 3 "format online result")
            
            # 在终端显示简洁结果
            echo "$RESULT_BLOCK" | grep -v "format online result" | head -n 3

            echo -e "========================================"
            
            # 提取数据写入 CSV
            TOTAL_TIME=$(echo "$RESULT_BLOCK" | sed -n '3p')
            DETAIL_LINE=$(echo "$RESULT_BLOCK" | grep "(OPRF")
            
            OPRF=$(echo "$DETAIL_LINE" | awk '{print $2}')
            GEN=$(echo "$DETAIL_LINE" | awk '{print $4}')
            QUERY=$(echo "$DETAIL_LINE" | awk '{print $6}')
            KEY=$(echo "$DETAIL_LINE" | awk '{print $8}')
            DEC=$(echo "$DETAIL_LINE" | awk '{print $10}' | tr -d ')')

            echo "$x,$y,$PAYLOAD_SIZE,$TOTAL_TIME,$OPRF,$GEN,$QUERY,$KEY,$DEC" >> "$RESULT_FILE"
        fi
        
        # 内存缓冲
        sleep 1
    done
done

echo -e "\n${GREEN}所有测试结束!${NC}"