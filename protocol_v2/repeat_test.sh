#!/bin/bash

REPEAT=20  # 设置重复次数

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 获取脚本所在目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# 创建日志目录
LOG_DIR="$SCRIPT_DIR/test_logs"
mkdir -p "$LOG_DIR"

# 清空旧日志
rm -f "$LOG_DIR"/*.log

echo "========================================"
echo "  Payable PSI协议 - 重复测试"
echo "========================================"
echo "日志目录: $LOG_DIR"
echo ""

rm -rf build

# 创建build目录
if [ ! -d "build" ]; then
    echo -e "${GREEN}创建build目录...${NC}"
    mkdir -p build
fi

cd build

# 运行CMake
echo -e "${GREEN}运行CMake配置...${NC}"
cmake ..

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake配置失败!${NC}"
    exit 1
fi

# 编译
echo -e "${GREEN}开始编译...${NC}"
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo -e "${RED}编译失败!${NC}"
    exit 1
fi

echo -e "${GREEN}编译成功!${NC}"
echo ""

SUCCESS_COUNT=0
TOTAL_COUNT=$REPEAT

echo -e "${YELLOW}开始重复测试...${NC}"
echo "========================================"

for ((i=1; i<=REPEAT; i++)); do
    echo -e "${YELLOW}=== 运行 $i/$REPEAT ===${NC}"
    
    # 定义当前运行的日志文件
    CURRENT_LOG="$LOG_DIR/run_${i}.log"
    
    # 运行程序并保存完整输出
    ./bin/lpsi_test > "$CURRENT_LOG" 2>&1
    EXIT_CODE=$?
    
    # 读取输出
    OUTPUT=$(cat "$CURRENT_LOG")
    
    # 显示关键信息
    echo "$OUTPUT" | grep -E "\[PIR外部验证\]"
    echo "$OUTPUT" | grep -E "\[Final 正确性验证\]"
    echo "$OUTPUT" | grep -E "总运行时间"
    
    
    # 检查是否协议执行正确
    if echo "$OUTPUT" | grep -q "\[Final 正确性验证\]✓ 协议执行正确"; then
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
        echo -e "${GREEN}✓ 正确${NC}"
        # 成功的情况可以删除日志（可选）
        # rm -f "$CURRENT_LOG"
    else
        echo -e "${RED}✗ 错误 - 测试失败！${NC}"
        echo -e "${RED}完整输出已保存到: $CURRENT_LOG${NC}"
        echo ""
        echo "========================================"
        echo -e "${RED}检测到测试失败，停止执行${NC}"
        echo "========================================"
        echo ""
        echo -e "${YELLOW}失败的完整输出：${NC}"
        echo "----------------------------------------"
        cat "$CURRENT_LOG"
        echo "----------------------------------------"
        echo ""
        echo -e "${YELLOW}统计信息（截至失败）：${NC}"
        echo "已运行次数: $i"
        echo "成功次数: $SUCCESS_COUNT"
        echo "失败次数: $((i - SUCCESS_COUNT))"
        if [ $i -gt 0 ]; then
            PARTIAL_ACCURACY=$(awk "BEGIN {printf \"%.2f\", $SUCCESS_COUNT * 100 / $i}")
            echo "当前正确率: $PARTIAL_ACCURACY%"
        fi
        echo ""
        echo -e "${YELLOW}所有日志文件保存在: $LOG_DIR${NC}"
        echo "退出码: $EXIT_CODE"
        exit 1
    fi

    echo
done

echo "======================================"
echo -e "${GREEN}测试完成！所有测试通过！${NC}"
echo "======================================"
echo "总运行次数: $TOTAL_COUNT"
echo "成功次数: $SUCCESS_COUNT"
echo "失败次数: $((TOTAL_COUNT - SUCCESS_COUNT))"
if command -v bc &> /dev/null; then
    ACCURACY=$(echo "scale=2; $SUCCESS_COUNT * 100 / $TOTAL_COUNT" | bc)
    echo "正确率: $ACCURACY%"
else
    ACCURACY=$(awk "BEGIN {printf \"%.2f\", $SUCCESS_COUNT * 100 / $TOTAL_COUNT}")
    echo "正确率: $ACCURACY%"
fi
echo "======================================"
echo -e "${YELLOW}所有日志文件保存在: $LOG_DIR${NC}"
echo -e "${GREEN}🎉 全部测试通过！${NC}"
exit 0