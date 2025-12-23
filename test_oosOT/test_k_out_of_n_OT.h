#ifndef TEST_K_OUT_OF_N_OT_H
#define TEST_K_OUT_OF_N_OT_H

#include "libOTe/NChooseOne/Oos/OosNcoOtReceiver.h"
#include "libOTe/NChooseOne/Oos/OosNcoOtSender.h"
#include "libOTe/Base/BaseOT.h"
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/Socket/LocalAsyncSock.h>
#include <vector>

using namespace osuCrypto;

// ==================== 配置结构 ====================
/**
 * @brief K-out-of-N OT 协议配置参数
 */
struct KOutOfNConfig {
    u64 N = 1 << 10;                    // Sender有N个数据
    u64 K = 1 << 8;                     // Receiver请求K个数据
    u64 dataBitSize = 64;               // 每个数据的比特大小
    bool malicious = true;              // 恶意对手模型
    u64 statSecParam = 40;              // 统计安全参数
    u64 inputBitCount = 7;              // 索引比特数 (2^7 = 128 >= 100)
};

// ==================== 函数声明 ====================

/**
 * @brief 设置Base OT
 * @param sender OT发送方
 * @param recv OT接收方
 * @param prng0 发送方随机数生成器
 * @param prng1 接收方随机数生成器
 * @param sock0 发送方socket
 * @param sock1 接收方socket
 */
void setBaseOts(OosNcoOtSender& sender, 
                OosNcoOtReceiver& recv, 
                PRNG& prng0, 
                PRNG& prng1, 
                coproto::LocalAsyncSocket& sock0, 
                coproto::LocalAsyncSocket& sock1);

/**
 * @brief Receiver任务 - 接收K个选定的值
 * @param receiver OT接收方对象
 * @param config 协议配置参数
 * @param prng 随机数生成器
 * @param sock 通信socket
 * @param requestedIndices 请求的索引列表
 * @param receivedValues 输出参数,接收到的值
 * @return 异步任务对象
 */
macoro::task<void> receiverTask(
    OosNcoOtReceiver& receiver,
    const KOutOfNConfig& config,
    PRNG& prng,
    coproto::LocalAsyncSocket sock,
    const std::vector<u32>& requestedIndices,
    std::vector<u32>& receivedValues);

/**
 * @brief Sender任务 - 发送N个数据
 * @param sender OT发送方对象
 * @param config 协议配置参数
 * @param prng 随机数生成器
 * @param sock 通信socket
 * @param senderData 发送方的N个数据
 * @return 异步任务对象
 */
macoro::task<void> senderTask(
    OosNcoOtSender& sender,
    const KOutOfNConfig& config,
    PRNG& prng,
    coproto::LocalAsyncSocket sock,
    const std::vector<u32>& senderData);

/**
 * @brief 运行单次测试
 * @param testId 测试编号
 * @param config 协议配置参数
 * @return 测试是否通过
 */
bool runSingleTest(int testId, const KOutOfNConfig& config);

/**
 * @brief OOS OT 测试主函数
 * @return 测试结果 (0表示成功)
 */
int oos_OT_test();

#endif // TEST_K_OUT_OF_N_OT_H
