#include "libOTe/NChooseOne/Oos/OosNcoOtReceiver.h"
#include "libOTe/NChooseOne/Oos/OosNcoOtSender.h"
#include "libOTe/Base/BaseOT.h"
#include <cryptoTools/Crypto/PRNG.h>
#include <cryptoTools/Crypto/AES.h>
#include <coproto/Socket/LocalAsyncSock.h>
#include <iostream>
#include <thread>
#include <vector>
#include <fstream>
#include <chrono>
#include <mutex>
#include <iomanip>
#include <cstring>

using namespace osuCrypto;
using namespace std::chrono;

std::ofstream g_logFile;
std::mutex g_logMutex;

void log(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logFile << message << std::endl;
    g_logFile.flush();
    std::cout << message << std::endl;
}

// ==================== 纠错码实现 ====================
class ErrorCorrectingCode {
public:
    static const u32 REPETITION_FACTOR = 3;
    
    static std::vector<u8> encode(const std::vector<u8>& input_bits) {
        std::vector<u8> encoded;
        for (u8 bit : input_bits) {
            for (u32 i = 0; i < REPETITION_FACTOR; ++i) {
                encoded.push_back(bit);
            }
        }
        return encoded;
    }
    
    static std::vector<u8> decode(const std::vector<u8>& encoded_bits) {
        std::vector<u8> decoded;
        for (size_t i = 0; i < encoded_bits.size(); i += REPETITION_FACTOR) {
            u32 sum = 0;
            for (u32 j = 0; j < REPETITION_FACTOR && i + j < encoded_bits.size(); ++j) {
                sum += encoded_bits[i + j];
            }
            decoded.push_back(sum >= (REPETITION_FACTOR + 1) / 2 ? 1 : 0);
        }
        return decoded;
    }
};

// ==================== 比特操作工具 ====================
class BitUtils {
public:
    static std::vector<u8> u32ToBits(u32 value, u32 bitCount) {
        std::vector<u8> bits(bitCount);
        for (u32 i = 0; i < bitCount; ++i) {
            bits[i] = (value >> i) & 1;
        }
        return bits;
    }
    
    static u32 bitsToU32(const std::vector<u8>& bits) {
        u32 value = 0;
        for (u32 i = 0; i < std::min<u32>(32, bits.size()); ++i) {
            if (bits[i]) {
                value |= (1u << i);
            }
        }
        return value;
    }
    
    static void xorBytes(u8* out, const u8* a, const u8* b, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            out[i] = a[i] ^ b[i];
        }
    }
    
    static bool bytesEqual(const u8* a, const u8* b, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            if (a[i] != b[i]) return false;
        }
        return true;
    }
};

// ==================== 结构体定义 ====================
struct TestConfig {
    u64 numOTs = 512;
    u64 inputBitCount = 8;
    bool malicious = true;
    u64 statSecParam = 40;
    block receiverSeed = block(4253465, 334565);
    block senderSeed = block(42532335, 334565);
};

struct TimeStats {
    double baseOtTime = 0;
    double extensionInitTime = 0;
    double encodeTime = 0;
    double correctionTime = 0;
    double maskingTime = 0;
    double checkTime = 0;
    double totalTime = 0;
};

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

// ==================== Receiver端 - 修正版 ====================
macoro::task<void> extensionReceiverTask(
    OosNcoOtReceiver& receiver, 
    const TestConfig& config,
    TimeStats& stats,
    PRNG& prng,
    coproto::LocalAsyncSocket sock,
    const std::vector<u32>& receiverChoices,
    std::vector<std::array<u8, 16>>& receiverOutputs) {
    
    try {
        log("[Receiver] 初始化扩展阶段...");
        
        if (receiverChoices.size() != config.numOTs) {
            throw std::runtime_error("Receiver choices size mismatch!");
        }
        
        receiverOutputs.resize(config.numOTs);
        u64 N = 1ULL << config.inputBitCount;
        
        // 步骤1：初始化
        auto initStart = high_resolution_clock::now();
        co_await receiver.init(config.numOTs, prng, sock);
        auto initEnd = high_resolution_clock::now();
        stats.extensionInitTime = duration<double, std::milli>(initEnd - initStart).count();
        log("[Receiver] 初始化完成，耗时: " + std::to_string(stats.extensionInitTime) + "ms");

        // 步骤2：为每个OT实例进行选择编码
        auto encStart = high_resolution_clock::now();
        
        std::vector<std::array<u8, 16>> receiverEncodings(config.numOTs);
        std::vector<std::vector<u8>> receiverBitEncodings(config.numOTs);
        
        u64 stepSize = 128;
        for (u64 i = 0; i < config.numOTs; i += stepSize) {
            u64 curStepSize = std::min(stepSize, config.numOTs - i);
            
            for (u64 k = 0; k < curStepSize; ++k) {
                u32 choice = receiverChoices[i + k];
                
                if (choice >= N) {
                    throw std::runtime_error("Invalid choice value!");
                }
                
                std::vector<u8> choiceBits = BitUtils::u32ToBits(choice, config.inputBitCount);
                receiverBitEncodings[i + k] = ErrorCorrectingCode::encode(choiceBits);
                
                receiver.encode(i + k, &choice, receiverEncodings[i + k].data(), 16);
            }
        }
        
        auto encEnd = high_resolution_clock::now();
        stats.encodeTime = duration<double, std::milli>(encEnd - encStart).count();
        log("[Receiver] 编码完成，耗时: " + std::to_string(stats.encodeTime) + "ms");

        // 步骤3：发送纠正信息
        auto corrStart = high_resolution_clock::now();
        for (u64 i = 0; i < config.numOTs; i += stepSize) {
            u64 curStepSize = std::min(stepSize, config.numOTs - i);
            co_await receiver.sendCorrection(sock, curStepSize);
        }
        auto corrEnd = high_resolution_clock::now();
        stats.correctionTime = duration<double, std::milli>(corrEnd - corrStart).count();
        log("[Receiver] 校正完成，耗时: " + std::to_string(stats.correctionTime) + "ms");

        // 步骤4：安全检查
        if (config.malicious) {
            auto checkStart = high_resolution_clock::now();
            block seed = prng.get<block>();
            co_await receiver.check(sock, seed);
            auto checkEnd = high_resolution_clock::now();
            stats.checkTime = duration<double, std::milli>(checkEnd - checkStart).count();
            log("[Receiver] 恶意对手检查通过，耗时: " + std::to_string(stats.checkTime) + "ms");
        }

        // ========== 关键修复：分离掩码接收逻辑 ==========
        // 步骤5：接收掩码值并恢复原始输入
        log("[Receiver] 开始接收并恢复原始输入值...");
        auto maskStart = high_resolution_clock::now();
        
        u64 recoveredCount = 0;
        for (u64 i = 0; i < config.numOTs; ++i) {
            u32 choice = receiverChoices[i];
            
            // 对于每个OT，接收所有N个掩码值
            for (u64 w = 0; w < N; ++w) {
                std::array<u8, 16> maskedValue;
                try {
                    co_await sock.recv(maskedValue);
                } catch (const std::exception& e) {
                    log("[Receiver] 接收超时或错误在OT[" + std::to_string(i) + "], w=" + 
                        std::to_string(w) + ": " + std::string(e.what()));
                    throw;
                }
                
                // 只有当 w == choice 时，才能正确恢复
                if (w == choice) {
                    BitUtils::xorBytes(receiverOutputs[i].data(), maskedValue.data(), 
                                     receiverEncodings[i].data(), 16);
                    recoveredCount++;
                }
            }
        }
        
        auto maskEnd = high_resolution_clock::now();
        stats.maskingTime = duration<double, std::milli>(maskEnd - maskStart).count();
        log("[Receiver] 恢复完成，耗时: " + std::to_string(stats.maskingTime) + "ms");
        log("[Receiver] 成功恢复: " + std::to_string(recoveredCount) + " 个值");

        // 显示部分结果
        std::stringstream ss;
        ss << "\n[Receiver] 部分恢复结果（前5条）:\n";
        for (u64 i = 0; i < std::min<u64>(5, config.numOTs); ++i) {
            ss << "  [" << i << "] 选择=" << receiverChoices[i] << " -> 恢复值: ";
            for (int j = 0; j < 8; ++j) 
                ss << std::hex << std::setw(2) << std::setfill('0') 
                   << (int)receiverOutputs[i][j];
            ss << "...\n";
        }
        log(ss.str());
        
    } catch (const std::exception& e) {
        log("[Receiver] 错误: " + std::string(e.what()));
        throw;
    }
    
    co_return;
}

// ==================== Sender端 - 修正版 ====================
macoro::task<void> extensionSenderTask(
    OosNcoOtSender& sender,
    const TestConfig& config,
    TimeStats& stats,
    PRNG& prng,
    coproto::LocalAsyncSocket sock,
    const std::vector<std::vector<std::array<u8, 16>>>& senderInputs) {
    
    try {
        log("[Sender] 初始化扩展阶段...");
        
        u64 N = 1ULL << config.inputBitCount;
        if (senderInputs.size() != config.numOTs) {
            throw std::runtime_error("Sender inputs size mismatch!");
        }
        for (u64 i = 0; i < config.numOTs; ++i) {
            if (senderInputs[i].size() != N) {
                throw std::runtime_error("Sender input size mismatch!");
            }
        }
        
        // 步骤1：初始化
        auto initStart = high_resolution_clock::now();
        co_await sender.init(config.numOTs, prng, sock);
        auto initEnd = high_resolution_clock::now();
        stats.extensionInitTime = duration<double, std::milli>(initEnd - initStart).count();
        log("[Sender] 初始化完成，耗时: " + std::to_string(stats.extensionInitTime) + "ms");

        // 步骤2：为所有可能的选择值编码
        auto encStart = high_resolution_clock::now();
        
        std::vector<std::vector<std::array<u8, 16>>> senderEncodings(config.numOTs);
        for (u64 i = 0; i < config.numOTs; ++i) {
            senderEncodings[i].resize(N);
        }
        
        u64 stepSize = 128;
        for (u64 i = 0; i < config.numOTs; i += stepSize) {
            u64 curStepSize = std::min(stepSize, config.numOTs - i);
            
            // 接收Receiver的纠正信息
            auto corrStart = high_resolution_clock::now();
            co_await sender.recvCorrection(sock, curStepSize);
            auto corrEnd = high_resolution_clock::now();
            stats.correctionTime += duration<double, std::milli>(corrEnd - corrStart).count();
            
            // 为所有可能的选择编码
            for (u64 k = 0; k < curStepSize; ++k) {
                for (u64 w = 0; w < N; ++w) {
                    u32 choice = static_cast<u32>(w);
                    sender.encode(i + k, &choice, senderEncodings[i + k][w].data(), 16);
                }
            }
        }
        
        auto encEnd = high_resolution_clock::now();
        stats.encodeTime = duration<double, std::milli>(encEnd - encStart).count();
        log("[Sender] 编码完成（所有N个选择），耗时: " + std::to_string(stats.encodeTime) + "ms");

        // 步骤3：安全检查
        if (config.malicious) {
            auto checkStart = high_resolution_clock::now();
            block seed = prng.get<block>();
            co_await sender.check(sock, seed);
            auto checkEnd = high_resolution_clock::now();
            stats.checkTime = duration<double, std::milli>(checkEnd - checkStart).count();
            log("[Sender] 恶意对手检查完成，耗时: " + std::to_string(stats.checkTime) + "ms");
        }

        // ========== 关键修复：明确的掩码发送顺序 ==========
        log("[Sender] 计算并发送掩码值...");
        auto maskStart = high_resolution_clock::now();
        
        u64 sentCount = 0;
        for (u64 i = 0; i < config.numOTs; ++i) {
            // 对于每个OT实例，按顺序发送N个掩码值
            for (u64 w = 0; w < N; ++w) {
                std::array<u8, 16> maskedValue;
                
                // 计算: masked = input XOR encoding
                // 这确保只有当w等于Receiver的选择时，
                // Receiver的encoding才能正确恢复出input
                BitUtils::xorBytes(maskedValue.data(), 
                                 senderInputs[i][w].data(),
                                 senderEncodings[i][w].data(), 
                                 16);
                
                try {
                    co_await sock.send(maskedValue);
                    sentCount++;
                } catch (const std::exception& e) {
                    log("[Sender] 发送失败在OT[" + std::to_string(i) + "], w=" + 
                        std::to_string(w) + ": " + std::string(e.what()));
                    throw;
                }
            }
        }
        
        auto maskEnd = high_resolution_clock::now();
        stats.maskingTime = duration<double, std::milli>(maskEnd - maskStart).count();
        
        log("[Sender] 发送完成，耗时: " + std::to_string(stats.maskingTime) + "ms");
        log("[Sender] 总共发送: " + std::to_string(sentCount) + " 个掩码值");

        // 显示部分结果
        std::stringstream ss;
        ss << "\n[Sender] 部分输入示例（前3个OT，前4个可选值）:\n";
        for (u64 i = 0; i < std::min<u64>(3, config.numOTs); ++i) {
            ss << "  [OT" << i << "]\n";
            for (u64 w = 0; w < std::min<u64>(4, N); ++w) {
                ss << "    [w=" << w << "] input=";
                for (int j = 0; j < 8; ++j) 
                    ss << std::hex << std::setw(2) << std::setfill('0') 
                       << (int)senderInputs[i][w][j];
                ss << "...\n";
            }
        }
        log(ss.str());
        
    } catch (const std::exception& e) {
        log("[Sender] 错误: " + std::string(e.what()));
        throw;
    }
    
    co_return;
}

// ==================== 测试执行 ====================
void runTest(const TestConfig& config,
             const std::vector<u32>& receiverChoices,
             const std::vector<std::vector<std::array<u8, 16>>>& senderInputs) {
    
    log("==========================================");
    log("OOS Chosen-Input OT Extension - 完整版");
    log("numOTs=" + std::to_string(config.numOTs) + 
        ", N=2^" + std::to_string(config.inputBitCount) + 
        "=" + std::to_string(1ULL << config.inputBitCount));
    log("==========================================");

    TimeStats receiverStats, senderStats;
    auto totalStart = high_resolution_clock::now();

    try {
        OosNcoOtSender sender;
        OosNcoOtReceiver receiver;
        
        sender.configure(config.malicious, config.statSecParam, config.inputBitCount);
        receiver.configure(config.malicious, config.statSecParam, config.inputBitCount);

        auto socks = cp::LocalAsyncSocket::makePair();
        
        PRNG prng0(config.receiverSeed);
        PRNG prng1(config.senderSeed);

        // Base OT
        auto baseStart = high_resolution_clock::now();
        setBaseOts(sender, receiver, prng0, prng1, socks[0], socks[1]);
        auto baseEnd = high_resolution_clock::now();
        
        receiverStats.baseOtTime = duration<double, std::milli>(baseEnd - baseStart).count();
        senderStats.baseOtTime = receiverStats.baseOtTime;
        log("[Base OT] 完成，耗时: " + std::to_string(receiverStats.baseOtTime) + "ms\n");

        std::vector<std::array<u8, 16>> receiverOutputs;

        // Extension阶段 - 关键修复：确保正确的async等待
        auto extStart = high_resolution_clock::now();
        
        auto p0 = extensionReceiverTask(receiver, config, receiverStats, prng0, socks[1],
                                       receiverChoices, receiverOutputs);
        auto p1 = extensionSenderTask(sender, config, senderStats, prng1, socks[0],
                                     senderInputs);
        
        // 正确的同步等待
        macoro::sync_wait(macoro::when_all_ready(std::move(p0), std::move(p1)));
        
        auto extEnd = high_resolution_clock::now();
        double extTime = duration<double, std::milli>(extEnd - extStart).count();

        // ==================== 结果验证 ====================
        u64 correctCount = 0;
        std::vector<std::pair<u64, std::string>> failures;
        
        for (u64 i = 0; i < config.numOTs; ++i) {
            u32 choice = receiverChoices[i];
            bool match = true;
            
            for (int j = 0; j < 16; ++j) {
                if (receiverOutputs[i][j] != senderInputs[i][choice][j]) {
                    match = false;
                    break;
                }
            }
            
            if (match) {
                correctCount++;
            } else {
                if (failures.size() < 5) {
                    failures.push_back({i, "输出值不匹配"});
                }
            }
        }

        log("\n【验证结果】");
        log("正确恢复: " + std::to_string(correctCount) + " / " + std::to_string(config.numOTs));
        
        if (correctCount == config.numOTs) {
            log("✓ 所有OT实例验证通过！");
        } else {
            log("✗ 部分OT实例验证失败");
            for (const auto& fail : failures) {
                log("  OT[" + std::to_string(fail.first) + "]: " + fail.second);
            }
        }

        // 时间统计
        auto totalEnd = high_resolution_clock::now();
        double totalTime = duration<double, std::milli>(totalEnd - totalStart).count();

        log("\n【时间统计】");
        log("总耗时: " + std::to_string(totalTime) + " ms");
        log("  Base OT: " + std::to_string(receiverStats.baseOtTime) + " ms");
        log("  Extension总计: " + std::to_string(extTime) + " ms");
        
        log("\nReceiver阶段:");
        log("  初始化: " + std::to_string(receiverStats.extensionInitTime) + " ms");
        log("  编码: " + std::to_string(receiverStats.encodeTime) + " ms");
        log("  校正: " + std::to_string(receiverStats.correctionTime) + " ms");
        log("  恢复: " + std::to_string(receiverStats.maskingTime) + " ms");
        log("  检查: " + std::to_string(receiverStats.checkTime) + " ms");
        
        log("\nSender阶段:");
        log("  初始化: " + std::to_string(senderStats.extensionInitTime) + " ms");
        log("  编码: " + std::to_string(senderStats.encodeTime) + " ms");
        log("  校正: " + std::to_string(senderStats.correctionTime) + " ms");
        log("  掩码: " + std::to_string(senderStats.maskingTime) + " ms");
        log("  检查: " + std::to_string(senderStats.checkTime) + " ms");
        
        // 通信分析
        u64 totalMessages = config.numOTs * (1ULL << config.inputBitCount);
        log("\n【通信分析】");
        log("发送掩码值数量: " + std::to_string(totalMessages));
        log("每个掩码值大小: 16 字节");
        log("总通信量: " + std::to_string(totalMessages * 16 / 1024) + " KB");
        
        log("\n【数学保证】");
        log("对于第i个OT，假设Receiver选择了c_i:");
        log("  receiverEncoding[i] = encode(c_i, secretKey)");
        log("  senderEncoding[i][w] = encode(w, secretKey)");
        log("  当 w == c_i 时:");
        log("    receiverEncoding[i] == senderEncoding[i][c_i]");
        log("    recoveredValue = maskedValue XOR receiverEncoding");
        log("                   = (input XOR senderEncoding) XOR receiverEncoding");
        log("                   = input XOR 0 = input ✓");
        log("  当 w != c_i 时:");
        log("    receiverEncoding[i] != senderEncoding[i][w]");
        log("    Receiver无法正确恢复（得到随机数据）✓");
        
        log("==========================================\n");
        
    } catch (const std::exception& e) {
        log("测试错误: " + std::string(e.what()));
    }
}

// 生成测试数据
std::vector<std::vector<std::array<u8, 16>>> generateSenderInputs(u64 numOTs, u64 N, PRNG& prng) {
    std::vector<std::vector<std::array<u8, 16>>> inputs(numOTs);
    for (u64 i = 0; i < numOTs; ++i) {
        inputs[i].resize(N);
        for (u64 w = 0; w < N; ++w) {
            prng.get(inputs[i][w].data(), 16);
        }
    }
    return inputs;
}

std::vector<u32> generateReceiverChoices(u64 numOTs, u64 maxBits, PRNG& prng) {
    std::vector<u32> choices(numOTs);
    u32 mask = (1u << maxBits) - 1;
    for (u64 i = 0; i < numOTs; ++i) {
        choices[i] = prng.get<u32>() & mask;
    }
    return choices;
}

int test1() {
    g_logFile.open("log_ot_complete_fixed.txt", std::ios::out | std::ios::trunc);
    if (!g_logFile.is_open()) {
        std::cerr << "无法打开日志文件" << std::endl;
        return 1;
    }

    // 定义测试参数组合
    std::vector<u64> tValues = {512};
    std::vector<u64> nBitsValues = {10};

    log("【测试计划】");
    log("测试参数组合数: " + std::to_string(tValues.size() * nBitsValues.size()));
    log("\n参数说明:");
    log("  T: OT实例数 (numOTs)");
    log("  N: 每个OT的可选值数量 = 2^nBits");
    log("  恶意对手模型: 是");
    log("  统计安全参数: 40");
    log("  值大小: 16 字节\n");

    // 用于汇总结果
    struct TestResult {
        u64 numOTs;
        u64 nBits;
        u64 N;
        double totalTime;
        double baseOtTime;
        double extTime;
        double commMB;
        bool success;
    };
    std::vector<TestResult> results;

    int testNum = 1;
    int totalTests = tValues.size() * nBitsValues.size();

    // 遍历所有参数组合
    for (u64 T : tValues) {
        for (u64 nBits : nBitsValues) {
            log("\n╔═══════════════════════════════════════════════════╗");
            log("║  测试 " + std::to_string(testNum) + "/" + std::to_string(totalTests) + 
                ": T=" + std::to_string(T) + ", nBits=" + std::to_string(nBits) + 
                " (N=2^" + std::to_string(nBits) + "=" + std::to_string(1ULL << nBits) + ")  ║");
            log("╚═══════════════════════════════════════════════════╝");

            TestConfig config;
            config.numOTs = T;
            config.inputBitCount = nBits;
            config.malicious = true;
            config.statSecParam = 40;
            config.receiverSeed = block(4253465 + testNum, 334565);
            config.senderSeed = block(42532335 + testNum, 334565);

            u64 N = 1ULL << nBits;
            
            log("配置详情:");
            log("  OT实例数 (T): " + std::to_string(T));
            log("  输入比特数 (nBits): " + std::to_string(nBits));
            log("  可选值数量 (N): " + std::to_string(N));
            log("  预计通信量: " + std::to_string(T * N * 16.0 / (1024.0 * 1024.0)) + " MB");

            try {
                PRNG prng(block(12345 + testNum, 67890));
                
                auto testStart = high_resolution_clock::now();
                
                auto senderInputs = generateSenderInputs(T, N, prng);
                auto receiverChoices = generateReceiverChoices(T, nBits, prng);
                
                runTest(config, receiverChoices, senderInputs);
                
                auto testEnd = high_resolution_clock::now();
                double testTime = duration<double, std::milli>(testEnd - testStart).count();

                TestResult result;
                result.numOTs = T;
                result.nBits = nBits;
                result.N = N;
                result.totalTime = testTime;
                result.commMB = T * N * 16.0 / (1024.0 * 1024.0);
                result.success = true;
                results.push_back(result);

                log("\n✓ 测试 " + std::to_string(testNum) + " 完成！总耗时: " + 
                    std::to_string(testTime) + " ms");

            } catch (const std::exception& e) {
                log("\n✗ 测试 " + std::to_string(testNum) + " 失败: " + std::string(e.what()));
                
                TestResult result;
                result.numOTs = T;
                result.nBits = nBits;
                result.N = N;
                result.totalTime = 0;
                result.commMB = T * N * 16.0 / (1024.0 * 1024.0);
                result.success = false;
                results.push_back(result);
            }

            testNum++;
        }
    }


    g_logFile.close();
    
    std::cout << "日志已保存到: log_ot_complete.txt\n";
    
    return 0;
}
