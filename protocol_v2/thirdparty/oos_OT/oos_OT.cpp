#include "oos_OT.h"
#include <iostream>
#include <iomanip>
#include <chrono>

using namespace osuCrypto;
using namespace std::chrono;

// ==================== Base OT设置 ====================
void setBaseOts(OosNcoOtSender& sender, OosNcoOtReceiver& recv, 
                PRNG& prng0, PRNG& prng1, 
                coproto::LocalAsyncSocket& sock0, coproto::LocalAsyncSocket& sock1) {
    u64 baseCount = sender.getBaseOTCount();
    std::vector<block> baseRecv(baseCount);
    std::vector<std::array<block, 2>> baseSend(baseCount);
    BitVector baseChoice(baseCount);
    
    baseChoice.randomize(prng0);
    prng0.get((u8*)baseSend.data()->data(), sizeof(block) * 2 * baseSend.size());
    
    for (u64 i = 0; i < baseCount; ++i) {
        baseRecv[i] = baseSend[i][baseChoice[i]];
    }

    auto p0 = sender.setBaseOts(baseRecv, baseChoice, sock0);
    auto p1 = recv.setBaseOts(baseSend, prng1, sock1);
    
    macoro::sync_wait(macoro::when_all_ready(std::move(p0), std::move(p1)));
}

// ==================== Receiver任务 ====================
macoro::task<void> receiverTask(
    OosNcoOtReceiver& receiver,
    const KOutOfNConfig& config,
    PRNG& prng,
    coproto::LocalAsyncSocket sock,
    const std::vector<u32>& requestedIndices,
    std::vector<u32>& receivedValues) {
    
    try {
        u64 maxN = 1ULL << config.inputBitCount;
        
        // 初始化 - K个OT实例
        co_await receiver.init(config.K, prng, sock);
        
        // 为每个OT编码选择
        std::vector<std::array<u8, 4>> encodings(config.K);
        for (u64 i = 0; i < config.K; ++i) {
            u32 choice = requestedIndices[i];
            receiver.encode(i, &choice, encodings[i].data(), 4);
        }
        
        // 发送纠正信息
        co_await receiver.sendCorrection(sock, config.K);
        
        // 安全检查
        if (config.malicious) {
            block seed = prng.get<block>();
            co_await receiver.check(sock, seed);
        }
        
        // 接收掩码值并恢复原始数据
        receivedValues.resize(config.K);
        for (u64 i = 0; i < config.K; ++i) {
            u32 choice = requestedIndices[i];
            
            // 接收N个掩码值
            for (u64 w = 0; w < config.N; ++w) {
                std::array<u8, 4> maskedValue;
                co_await sock.recv(maskedValue);
                
                // 只有w == choice时才能正确恢复
                if (w == choice) {
                    // XOR操作恢复原始值
                    u32 recovered = 0;
                    for (int j = 0; j < 4; ++j) {
                        u8 byte = maskedValue[j] ^ encodings[i][j];
                        recovered |= (static_cast<u32>(byte) << (j * 8));
                    }
                    receivedValues[i] = recovered;
                }
            }
        }
        
    } catch (const std::exception& e) {
        std::cout << "[Receiver错误] " << e.what() << std::endl;
        throw;
    }
    
    co_return;
}

// ==================== Sender任务 ====================
macoro::task<void> senderTask(
    OosNcoOtSender& sender,
    const KOutOfNConfig& config,
    PRNG& prng,
    coproto::LocalAsyncSocket sock,
    const std::vector<u32>& senderData) {
    
    try {
        u64 maxN = 1ULL << config.inputBitCount;
        
        // 初始化 - K个OT实例
        co_await sender.init(config.K, prng, sock);
        
        // 接收纠正信息并为所有可能的选择编码
        co_await sender.recvCorrection(sock, config.K);
        
        std::vector<std::vector<std::array<u8, 4>>> encodings(config.K);
        for (u64 i = 0; i < config.K; ++i) {
            encodings[i].resize(config.N);
            for (u64 w = 0; w < config.N; ++w) {
                u32 choice = static_cast<u32>(w);
                sender.encode(i, &choice, encodings[i][w].data(), 4);
            }
        }
        
        // 安全检查
        if (config.malicious) {
            block seed = prng.get<block>();
            co_await sender.check(sock, seed);
        }
        
        // 发送掩码值
        for (u64 i = 0; i < config.K; ++i) {
            for (u64 w = 0; w < config.N; ++w) {
                std::array<u8, 4> maskedValue;
                
                // 将u32转换为字节数组并与编码XOR
                u32 value = senderData[w];
                for (int j = 0; j < 4; ++j) {
                    u8 valueByte = (value >> (j * 8)) & 0xFF;
                    maskedValue[j] = valueByte ^ encodings[i][w][j];
                }
                
                co_await sock.send(maskedValue);
            }
        }
        
    } catch (const std::exception& e) {
        std::cout << "[Sender错误] " << e.what() << std::endl;
        throw;
    }
    
    co_return;
}

// ==================== 单次测试 ====================
bool runSingleTest(int testId, const KOutOfNConfig& config) {
    std::cout << "\n========== 测试 #" << testId << " ==========\n";
    
    try {
        // 初始化
        OosNcoOtSender sender;
        OosNcoOtReceiver receiver;
        
        sender.configure(config.malicious, config.statSecParam, config.inputBitCount);
        receiver.configure(config.malicious, config.statSecParam, config.inputBitCount);
        
        auto socks = cp::LocalAsyncSocket::makePair();
        
        PRNG prng0(block(4253465 + testId, 334565));
        PRNG prng1(block(42532335 + testId, 334565));
        
        // Base OT
        setBaseOts(sender, receiver, prng0, prng1, socks[0], socks[1]);
        
        // 生成Sender的N个数据
        std::vector<u32> senderData(config.N);
        PRNG dataPrng(block(12345 + testId, 67890));
        for (u64 i = 0; i < config.N; ++i) {
            senderData[i] = dataPrng.get<u32>() & ((1u << config.dataBitSize) - 1);
        }
        
        // 生成Receiver的K个索引
        std::vector<u32> requestedIndices(config.K);
        PRNG indexPrng(block(98765 + testId, 43210));
        for (u64 i = 0; i < config.K; ++i) {
            requestedIndices[i] = indexPrng.get<u32>() % config.N;
        }
        
        // 打印测试信息
        std::cout << "Sender有 " << config.N << " 个数据 (每个" << config.dataBitSize << "位)\n";
        std::cout << "Receiver请求 " << config.K << " 个索引: [";
        for (u64 i = 0; i < std::min<u64>(5, config.K); ++i) {
            std::cout << requestedIndices[i];
            if (i < std::min<u64>(5, config.K) - 1) std::cout << ", ";
        }
        if (config.K > 5) std::cout << ", ...";
        std::cout << "]\n";
        
        // 执行OT协议
        std::vector<u32> receivedValues;
        
        auto startTime = high_resolution_clock::now();
        
        auto p0 = receiverTask(receiver, config, prng0, socks[1], 
                              requestedIndices, receivedValues);
        auto p1 = senderTask(sender, config, prng1, socks[0], senderData);
        
        macoro::sync_wait(macoro::when_all_ready(std::move(p0), std::move(p1)));
        
        auto endTime = high_resolution_clock::now();
        double elapsedMs = duration<double, std::milli>(endTime - startTime).count();
        
        // 验证正确性
        bool allCorrect = true;
        u64 correctCount = 0;
        
        std::cout << "\n验证结果:\n";
        for (u64 i = 0; i < config.K; ++i) {
            u32 expectedValue = senderData[requestedIndices[i]];
            u32 actualValue = receivedValues[i];
            bool correct = (expectedValue == actualValue);
            
            if (correct) {
                correctCount++;
            } else {
                allCorrect = false;
            }
            
            // 只打印前5个结果
            if (i < 5) {
                std::cout << "  索引[" << std::setw(3) << requestedIndices[i] << "]: "
                         << "期望=" << std::setw(10) << expectedValue << ", "
                         << "实际=" << std::setw(10) << actualValue << " ";
                if (correct) {
                    std::cout << "✓\n";
                } else {
                    std::cout << "✗\n";
                }
            }
        }
        
        if (config.K > 5) {
            std::cout << "  ... (省略" << (config.K - 5) << "个结果)\n";
        }
        
        std::cout << "\n正确率: " << correctCount << "/" << config.K 
                 << " (" << (100.0 * correctCount / config.K) << "%)\n";
        std::cout << "耗时: " << elapsedMs << " ms\n";
        
        if (allCorrect) {
            std::cout << "✓ 测试通过!\n";
        } else {
            std::cout << "✗ 测试失败!\n";
        }
        
        return allCorrect;
        
    } catch (const std::exception& e) {
        std::cout << "✗ 测试异常: " << e.what() << "\n";
        return false;
    }
}

int oos_OT_test() {
    std::cout << "╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║     K-out-of-N OT Extension 正确性测试                ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n";
    
    KOutOfNConfig config;
    config.N = 1 << 16;              // Sender有100个数据
    config.K = 1 << 12;               // Receiver请求10个数据
    config.dataBitSize = 32;     // 每个数据32位
    config.malicious = true;
    config.statSecParam = 40;
    config.inputBitCount = 7;    // 2^7 = 128 >= 100

    const int NUM_TESTS = 1; // 20
    
    std::cout << "\n测试配置:\n";
    std::cout << "  Sender数据数量 (N): " << config.N << "\n";
    std::cout << "  Receiver请求数量 (K): " << config.K << "\n";
    std::cout << "  数据大小: " << config.dataBitSize << " 位\n";
    std::cout << "  安全模型: " << (config.malicious ? "恶意对手" : "半诚实") << "\n";
    std::cout << "  重复次数: " << NUM_TESTS << "\n";
    
    int passCount = 0;
    
    auto totalStart = high_resolution_clock::now();
    
    for (int i = 1; i <= NUM_TESTS; ++i) {
        bool passed = runSingleTest(i, config);
        if (passed) {
            passCount++;
        }
    }
    
    auto totalEnd = high_resolution_clock::now();
    double totalTime = duration<double, std::milli>(totalEnd - totalStart).count();
    
    // 汇总结果
    std::cout << "\n╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║                    测试汇总结果                       ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n";
    std::cout << "通过测试: " << passCount << "/" << NUM_TESTS 
             << " (" << (100.0 * passCount / NUM_TESTS) << "%)\n";
    std::cout << "总耗时: " << totalTime << " ms\n";
    std::cout << "平均耗时: " << (totalTime / NUM_TESTS) << " ms/测试\n";
    
    if (passCount == NUM_TESTS) {
        std::cout << "\n✓✓✓ 所有测试通过! K-out-of-N OT协议实现正确! ✓✓✓\n";
        // std::cout << "\n功能验证:\n";
        // std::cout << "  ✓ Sender有N个数据,不知道Receiver请求哪些索引\n";
        // std::cout << "  ✓ Receiver有K个索引,只能获取对应位置的数据\n";
        // std::cout << "  ✓ Receiver无法获取其他位置的数据\n";
        // std::cout << "  ✓ Sender不知道Receiver请求了哪些索引\n";
        return 0;
    } else {
        std::cout << "\n✗ 部分测试失败,请检查实现!\n";
        return 1;
    }
}
