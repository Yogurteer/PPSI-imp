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

echo "========================================"
echo "  PIRANA PIR 重复测试"
echo "========================================"
echo ""

# 清理并重新编译
echo -e "${GREEN}清理build目录...${NC}"
rm -rf build

# 创建build目录
if [ ! -d "build" ]; then
    echo -e "${GREEN}创建build目录...${NC}"
    mkdir -p build
fi

cd build

# 运行CMake
echo -e "${GREEN}运行CMake配置...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake配置失败!${NC}"
    exit 1
fi

# 编译
echo -e "${GREEN}开始编译...${NC}"
make -j$(nproc) > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo -e "${RED}编译失败!${NC}"
    exit 1
fi

echo -e "${GREEN}编译成功!${NC}"
echo ""

SUCCESS_COUNT=0
TOTAL_COUNT=$REPEAT

echo "开始重复测试 (共 $REPEAT 次)..."
echo "========================================"
echo ""

for ((i=1; i<=REPEAT; i++)); do
    echo -e "${YELLOW}=== 运行 $i/$REPEAT ===${NC}"

    # 运行程序并捕获输出
    OUTPUT=$(./bin/pirana_test 2>&1)
    
    # 检查是否所有查询都正确
    if echo "$OUTPUT" | grep -q "✓ 所有查询结果正确！"; then
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
        echo -e "${GREEN}✓ 正确${NC}"
    else
        echo -e "${RED}✗ 错误${NC}"
        # 显示错误详情
        echo "$OUTPUT" | grep -E "查询索引|✗ 错误|期望|实际"
    fi

    echo ""
done

echo "========================================"
echo -e "${YELLOW}测试完成！${NC}"
echo "========================================"
echo "总运行次数: $TOTAL_COUNT"
echo -e "${GREEN}成功次数: $SUCCESS_COUNT${NC}"
echo -e "${RED}失败次数: $((TOTAL_COUNT - SUCCESS_COUNT))${NC}"

# 计算正确率
if command -v bc &> /dev/null; then
    ACCURACY=$(echo "scale=2; $SUCCESS_COUNT * 100 / $TOTAL_COUNT" | bc)
    echo -e "${YELLOW}正确率: $ACCURACY%${NC}"
else
    # 如果没有bc，使用awk
    ACCURACY=$(awk "BEGIN {printf \"%.2f\", $SUCCESS_COUNT * 100 / $TOTAL_COUNT}")
    echo -e "${YELLOW}正确率: $ACCURACY%${NC}"
fi

echo "========================================"

# 根据结果返回退出码
if [ $SUCCESS_COUNT -eq $TOTAL_COUNT ]; then
    echo -e "${GREEN}🎉 所有测试通过！${NC}"
    exit 0
else
    echo -e "${RED}⚠️  部分测试失败${NC}"
    exit 1
fi
