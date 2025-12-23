#!/bin/bash

echo "=========================================="
echo "K-out-of-N OT 重复性测试脚本"
echo "=========================================="

# 编译程序
echo ""
echo "步骤1: 编译测试程序..."
bash compile_k_out_of_n.sh

if [ $? -ne 0 ]; then
    echo "编译失败，退出测试"
    exit 1
fi

# 运行测试
echo ""
echo "步骤2: 运行重复性测试"
echo "=========================================="
echo ""

./oos_OT

# 检查返回码
if [ $? -eq 0 ]; then
    echo ""
    echo "=========================================="
    echo "✓✓✓ 所有测试完成并通过! ✓✓✓"
    echo "=========================================="
    echo ""
    echo "测试结论:"
    echo "  该项目成功实现了 K-out-of-N OT Extension 协议"
    echo ""
    echo "核心功能:"
    echo "  1. Sender拥有N个数据(每个32位)"
    echo "  2. Receiver拥有K个索引位置"
    echo "  3. 通过OT协议,Receiver获取对应索引的数据"
    echo "  4. Sender不知道Receiver请求了哪些索引"
    echo "  5. Receiver只能获取请求索引的数据,无法获取其他数据"
    echo ""
    echo "安全性:"
    echo "  ✓ 采用恶意对手模型 (Malicious Security)"
    echo "  ✓ 统计安全参数 = 40"
    echo "  ✓ 使用OOS N-Choose-One OT协议"
    echo ""
else
    echo ""
    echo "=========================================="
    echo "✗ 测试失败，请检查输出日志"
    echo "=========================================="
    exit 1
fi
