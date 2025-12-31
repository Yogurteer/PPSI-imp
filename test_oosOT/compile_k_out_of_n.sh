#!/bin/bash

echo "=========================================="
echo "编译 K-out-of-N OT 测试程序"
echo "=========================================="

# 设置路径
LIBOTE_ROOT="../libOTe"
LIBOTE_BUILD_DIR="${LIBOTE_ROOT}/out/build/linux"
LIBOTE_INSTALL_DIR="${LIBOTE_ROOT}/out/install/linux"

# 编译命令
g++ -std=c++20 \
    -O3 -march=native \
    -Wall -Wextra -Wno-unused-parameter \
    -fcoroutines \
    -I./include \
    -I${LIBOTE_ROOT} \
    -I${LIBOTE_ROOT}/libOTe \
    -I${LIBOTE_ROOT}/cryptoTools \
    -I${LIBOTE_ROOT}/cryptoTools/cryptoTools \
    -I${LIBOTE_ROOT}/thirdparty \
    -I${LIBOTE_ROOT}/out/coproto \
    -I${LIBOTE_ROOT}/out/macoro \
    -I${LIBOTE_BUILD_DIR} \
    -I${LIBOTE_BUILD_DIR}/libOTe \
    -I${LIBOTE_BUILD_DIR}/cryptoTools \
    -I${LIBOTE_BUILD_DIR}/cryptoTools/cryptoTools \
    -I${LIBOTE_BUILD_DIR}/coproto \
    -I${LIBOTE_BUILD_DIR}/coproto/coproto \
    -I${LIBOTE_BUILD_DIR}/macoro \
    -I${LIBOTE_BUILD_DIR}/macoro/macoro \
    -I${LIBOTE_INSTALL_DIR}/include \
    test_k_out_of_n_OT.cpp \
    -o test_k_out_of_n_OT \
    -L${LIBOTE_BUILD_DIR}/libOTe \
    -L${LIBOTE_BUILD_DIR}/cryptoTools/cryptoTools \
    -L${LIBOTE_BUILD_DIR}/coproto/coproto \
    -L${LIBOTE_BUILD_DIR}/macoro/macoro \
    -L${LIBOTE_BUILD_DIR}/thirdparty/KyberOT \
    -L${LIBOTE_BUILD_DIR}/thirdparty/SimplestOT \
    -L${LIBOTE_INSTALL_DIR}/lib \
    ${LIBOTE_BUILD_DIR}/libOTe/liblibOTe.a \
    ${LIBOTE_BUILD_DIR}/cryptoTools/cryptoTools/libcryptoTools.a \
    ${LIBOTE_BUILD_DIR}/coproto/coproto/libcoproto.a \
    ${LIBOTE_BUILD_DIR}/macoro/macoro/libmacoro.a \
    ${LIBOTE_BUILD_DIR}/thirdparty/KyberOT/libKyberOT.a \
    ${LIBOTE_BUILD_DIR}/thirdparty/SimplestOT/libSimplestOT.a \
    ${LIBOTE_INSTALL_DIR}/lib/libsodium.a \
    -lssl -lcrypto -lpthread \
    -Wl,--no-as-needed -ldl -no-pie

if [ $? -eq 0 ]; then
    echo ""
    echo "✓ 编译成功!"
    echo "可执行文件: ./test_k_out_of_n_OT"
    echo ""
    echo "运行测试:"
    echo "  ./test_k_out_of_n_OT"
    echo ""
else
    echo ""
    echo "✗ 编译失败!"
    exit 1
fi
