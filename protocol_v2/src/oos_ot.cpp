#include "oos_ot.h"
#include "libOTe/NChooseOne/Oos/OosNcoOtReceiver.h"
#include "libOTe/NChooseOne/Oos/OosNcoOtSender.h"
#include "libOTe/Base/BaseOT.h"
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/Socket/LocalAsyncSock.h>
#include <iostream>
#include <chrono>
#include <stdexcept>
#include <cstring>

// 使用配置的OT数据大小
using OTDataArray = std::array<uint8_t, OT_DATA_SIZE>;

using namespace osuCrypto;
namespace cp = coproto;

// ========================================
//  辅助工具类
// ========================================
class BitUtils {
public:
    static void xorBytes(uint8_t* out, const uint8_t* a, const uint8_t* b, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            out[i] = a[i] ^ b[i];
        }
    }
};

// ========================================
//  OOSOTSender实现
// ========================================
class OOSOTSender::Impl {
public:
    OosNcoOtSender sender;
    std::vector<std::vector<Element>> inputs;
    size_t num_choices;
    uint32_t input_bit_count;
    bool malicious;
    uint64_t stat_sec_param;
    
    double total_time_ms;
    double base_ot_time_ms;
    double extension_time_ms;
    
    Impl() : num_choices(0), input_bit_count(8), malicious(true), stat_sec_param(40),
             total_time_ms(0), base_ot_time_ms(0), extension_time_ms(0) {}
};

OOSOTSender::OOSOTSender() : pImpl(new Impl()) {}

OOSOTSender::~OOSOTSender() {
    delete pImpl;
}

void OOSOTSender::configure(size_t num_choices, uint32_t input_bit_count,
                            bool malicious, uint64_t stat_sec_param) {
    pImpl->num_choices = num_choices;
    pImpl->input_bit_count = input_bit_count;
    pImpl->malicious = malicious;
    pImpl->stat_sec_param = stat_sec_param;
    
    // 验证num_choices与input_bit_count的一致性
    size_t expected_choices = 1ULL << input_bit_count;
    if (num_choices != expected_choices) {
        throw std::runtime_error("num_choices must equal 2^input_bit_count");
    }
    
    pImpl->sender.configure(malicious, stat_sec_param, input_bit_count);
}

void OOSOTSender::set_inputs(const std::vector<std::vector<Element>>& inputs) {
    pImpl->inputs = inputs;
    
    // 验证输入格式
    if (inputs.empty()) {
        throw std::runtime_error("Sender inputs cannot be empty");
    }
    
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (inputs[i].size() != pImpl->num_choices) {
            throw std::runtime_error("Sender input size mismatch at index " + std::to_string(i));
        }
    }
}

bool OOSOTSender::execute() {
    try {
        // 此函数需要与Receiver端配对使用
        // 实际执行在run_oos_ot中完成
        throw std::runtime_error("Use run_oos_ot() for paired execution");
    } catch (const std::exception& e) {
        std::cerr << "[OOSOTSender] Error: " << e.what() << std::endl;
        return false;
    }
}

double OOSOTSender::get_total_time_ms() const { return pImpl->total_time_ms; }
double OOSOTSender::get_base_ot_time_ms() const { return pImpl->base_ot_time_ms; }
double OOSOTSender::get_extension_time_ms() const { return pImpl->extension_time_ms; }

// ========================================
//  OOSOTReceiver实现
// ========================================
class OOSOTReceiver::Impl {
public:
    OosNcoOtReceiver receiver;
    std::vector<size_t> choices;
    std::vector<Element> outputs;
    size_t num_choices;
    uint32_t input_bit_count;
    bool malicious;
    uint64_t stat_sec_param;
    
    double total_time_ms;
    double base_ot_time_ms;
    double extension_time_ms;
    
    Impl() : num_choices(0), input_bit_count(8), malicious(true), stat_sec_param(40),
             total_time_ms(0), base_ot_time_ms(0), extension_time_ms(0) {}
};

OOSOTReceiver::OOSOTReceiver() : pImpl(new Impl()) {}

OOSOTReceiver::~OOSOTReceiver() {
    delete pImpl;
}

void OOSOTReceiver::configure(size_t num_choices, uint32_t input_bit_count,
                              bool malicious, uint64_t stat_sec_param) {
    pImpl->num_choices = num_choices;
    pImpl->input_bit_count = input_bit_count;
    pImpl->malicious = malicious;
    pImpl->stat_sec_param = stat_sec_param;
    
    size_t expected_choices = 1ULL << input_bit_count;
    if (num_choices != expected_choices) {
        throw std::runtime_error("num_choices must equal 2^input_bit_count");
    }
    
    pImpl->receiver.configure(malicious, stat_sec_param, input_bit_count);
}

void OOSOTReceiver::set_choices(const std::vector<size_t>& choices) {
    pImpl->choices = choices;
    
    // 验证选择范围
    for (size_t i = 0; i < choices.size(); ++i) {
        if (choices[i] >= pImpl->num_choices) {
            throw std::runtime_error("Invalid choice at index " + std::to_string(i));
        }
    }
}

bool OOSOTReceiver::execute() {
    try {
        throw std::runtime_error("Use run_oos_ot() for paired execution");
    } catch (const std::exception& e) {
        std::cerr << "[OOSOTReceiver] Error: " << e.what() << std::endl;
        return false;
    }
}

const std::vector<Element>& OOSOTReceiver::get_outputs() const {
    return pImpl->outputs;
}

double OOSOTReceiver::get_total_time_ms() const { return pImpl->total_time_ms; }
double OOSOTReceiver::get_base_ot_time_ms() const { return pImpl->base_ot_time_ms; }
double OOSOTReceiver::get_extension_time_ms() const { return pImpl->extension_time_ms; }

// ========================================
//  Base OT设置
// ========================================
static void setBaseOts(OosNcoOtSender& sender, OosNcoOtReceiver& recv,
                      PRNG& prng0, PRNG& prng1,
                      cp::LocalAsyncSocket& sock0, cp::LocalAsyncSocket& sock1) {
    uint64_t baseCount = sender.getBaseOTCount();
    std::vector<block> baseRecv(baseCount);
    std::vector<std::array<block, 2>> baseSend(baseCount);
    BitVector baseChoice(baseCount);
    
    baseChoice.randomize(prng0);
    prng0.get((uint8_t*)baseSend.data()->data(), sizeof(block) * 2 * baseSend.size());
    
    for (uint64_t i = 0; i < baseCount; ++i) {
        baseRecv[i] = baseSend[i][baseChoice[i]];
    }
    
    auto p0 = sender.setBaseOts(baseRecv, baseChoice, sock0);
    auto p1 = recv.setBaseOts(baseSend, prng1, sock1);
    
    macoro::sync_wait(macoro::when_all_ready(std::move(p0), std::move(p1)));
}

// ========================================
//  Receiver扩展任务
// ========================================
static macoro::task<void> receiverExtensionTask(
    OosNcoOtReceiver& receiver,
    uint64_t numOTs,
    uint32_t input_bit_count,
    bool malicious,
    PRNG& prng,
    cp::LocalAsyncSocket sock,
    const std::vector<size_t>& choices,
    std::vector<Element>& outputs) {
    
    try {
        uint64_t N = 1ULL << input_bit_count;
        outputs.resize(numOTs);
        
        // 初始化
        co_await receiver.init(numOTs, prng, sock);
        
        // 编码选择
        std::vector<OTDataArray> encodings(numOTs);
        uint64_t stepSize = 128;
        
        for (uint64_t i = 0; i < numOTs; i += stepSize) {
            uint64_t curStepSize = std::min(stepSize, numOTs - i);
            
            for (uint64_t k = 0; k < curStepSize; ++k) {
                uint32_t choice = static_cast<uint32_t>(choices[i + k]);
                receiver.encode(i + k, &choice, encodings[i + k].data(), OT_DATA_SIZE);
            }
        }
        
        // 发送纠正信息
        for (uint64_t i = 0; i < numOTs; i += stepSize) {
            uint64_t curStepSize = std::min(stepSize, numOTs - i);
            co_await receiver.sendCorrection(sock, curStepSize);
        }
        
        // 安全检查
        if (malicious) {
            block seed = prng.get<block>();
            co_await receiver.check(sock, seed);
        }
        
        // 接收掩码并恢复
        for (uint64_t i = 0; i < numOTs; ++i) {
            uint32_t choice = static_cast<uint32_t>(choices[i]);
            
            for (uint64_t w = 0; w < N; ++w) {
                OTDataArray maskedValue;
                co_await sock.recv(maskedValue);
                
                if (w == choice) {
                    outputs[i].resize(OT_DATA_SIZE);
                    BitUtils::xorBytes(outputs[i].data(), maskedValue.data(),
                                     encodings[i].data(), OT_DATA_SIZE);
                }
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[Receiver Extension] Error: " << e.what() << std::endl;
        throw;
    }
    
    co_return;
}

// ========================================
//  Sender扩展任务
// ========================================
static macoro::task<void> senderExtensionTask(
    OosNcoOtSender& sender,
    uint64_t numOTs,
    uint32_t input_bit_count,
    bool malicious,
    PRNG& prng,
    cp::LocalAsyncSocket sock,
    const std::vector<std::vector<Element>>& inputs) {
    
    try {
        uint64_t N = 1ULL << input_bit_count;
        
        // 初始化
        co_await sender.init(numOTs, prng, sock);
        
        // 编码所有可能的选择
        std::vector<std::vector<OTDataArray>> encodings(numOTs);
        for (uint64_t i = 0; i < numOTs; ++i) {
            encodings[i].resize(N);
        }
        
        uint64_t stepSize = 128;
        for (uint64_t i = 0; i < numOTs; i += stepSize) {
            uint64_t curStepSize = std::min(stepSize, numOTs - i);
            
            // 接收纠正信息
            co_await sender.recvCorrection(sock, curStepSize);
            
            // 编码所有选择
            for (uint64_t k = 0; k < curStepSize; ++k) {
                for (uint64_t w = 0; w < N; ++w) {
                    uint32_t choice = static_cast<uint32_t>(w);
                    sender.encode(i + k, &choice, encodings[i + k][w].data(), OT_DATA_SIZE);
                }
            }
        }
        
        // 安全检查
        if (malicious) {
            block seed = prng.get<block>();
            co_await sender.check(sock, seed);
        }
        
        // 计算并发送掩码
        for (uint64_t i = 0; i < numOTs; ++i) {
            for (uint64_t w = 0; w < N; ++w) {
                OTDataArray maskedValue;
                
                // 处理输入长度不足OT_DATA_SIZE字节的情况
                OTDataArray paddedInput = {0};
                size_t copyLen = std::min(inputs[i][w].size(), OT_DATA_SIZE);
                std::memcpy(paddedInput.data(), inputs[i][w].data(), copyLen);
                
                BitUtils::xorBytes(maskedValue.data(), paddedInput.data(),
                                 encodings[i][w].data(), OT_DATA_SIZE);
                
                co_await sock.send(maskedValue);
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[Sender Extension] Error: " << e.what() << std::endl;
        throw;
    }
    
    co_return;
}

// ========================================
//  便捷函数实现
// ========================================
bool run_oos_ot(
    const std::vector<std::vector<Element>>& sender_inputs,
    const std::vector<size_t>& receiver_choices,
    std::vector<Element>& receiver_outputs,
    uint32_t input_bit_count,
    bool malicious) {
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // 参数验证
        if (sender_inputs.empty() || receiver_choices.empty()) {
            throw std::runtime_error("Inputs cannot be empty");
        }
        
        if (sender_inputs.size() != receiver_choices.size()) {
            throw std::runtime_error("Number of OT instances mismatch");
        }
        
        uint64_t numOTs = sender_inputs.size();
        uint64_t N = 1ULL << input_bit_count;
        uint64_t stat_sec_param = 40;
        
        // 验证sender输入格式
        for (size_t i = 0; i < sender_inputs.size(); ++i) {
            if (sender_inputs[i].size() != N) {
                throw std::runtime_error("Sender input size mismatch at OT " + std::to_string(i));
            }
        }
        
        // 验证receiver选择范围
        for (size_t i = 0; i < receiver_choices.size(); ++i) {
            if (receiver_choices[i] >= N) {
                throw std::runtime_error("Invalid choice at OT " + std::to_string(i));
            }
        }
        
        // 创建OT对象
        OosNcoOtSender sender;
        OosNcoOtReceiver receiver;
        
        sender.configure(malicious, stat_sec_param, input_bit_count);
        receiver.configure(malicious, stat_sec_param, input_bit_count);
        
        // 创建通信socket
        auto socks = cp::LocalAsyncSocket::makePair();
        
        // 创建随机数生成器
        PRNG prng0(block(4253465, 334565));
        PRNG prng1(block(42532335, 334565));
        
        // Base OT
        auto base_start = std::chrono::high_resolution_clock::now();
        setBaseOts(sender, receiver, prng0, prng1, socks[0], socks[1]);
        auto base_end = std::chrono::high_resolution_clock::now();
        
        double base_time = std::chrono::duration<double, std::milli>(base_end - base_start).count();
        
        // Extension阶段
        auto ext_start = std::chrono::high_resolution_clock::now();
        
        auto p0 = receiverExtensionTask(receiver, numOTs, input_bit_count, malicious,
                                       prng0, socks[1], receiver_choices, receiver_outputs);
        auto p1 = senderExtensionTask(sender, numOTs, input_bit_count, malicious,
                                     prng1, socks[0], sender_inputs);
        
        macoro::sync_wait(macoro::when_all_ready(std::move(p0), std::move(p1)));
        
        auto ext_end = std::chrono::high_resolution_clock::now();
        auto total_end = std::chrono::high_resolution_clock::now();
        
        double ext_time = std::chrono::duration<double, std::milli>(ext_end - ext_start).count();
        double total_time = std::chrono::duration<double, std::milli>(total_end - start_time).count();
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[run_oos_ot] Error: " << e.what() << std::endl;
        return false;
    }
}
