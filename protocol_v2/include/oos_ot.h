#ifndef OOS_OT_H
#define OOS_OT_H

#include <vector>
#include <array>
#include <cstdint>
#include <cstddef>

// 前向声明,避免在头文件中暴露libOTe的依赖
namespace osuCrypto {
    class OosNcoOtSender;
    class OosNcoOtReceiver;
    class PRNG;
}

namespace coproto {
    class LocalAsyncSocket;
}

// Element类型定义 (与sender.h/receiver.h保持一致)
using Element = std::vector<unsigned char>;

// OT传输数据大小配置
constexpr size_t OT_DATA_SIZE = 32;  // OT传输的字节数 (可调整为16, 32, 64等)

// ========================================
//  OOS OT Sender封装类
// ========================================
class OOSOTSender {
public:
    OOSOTSender();
    ~OOSOTSender();
    
    // 配置OT参数
    // num_choices: 每个OT可选择的选项数量 (N = 2^inputBitCount)
    // input_bit_count: 选择索引的比特数 (例如8表示256个选项)
    // malicious: 是否启用恶意安全
    // stat_sec_param: 统计安全参数
    void configure(size_t num_choices, uint32_t input_bit_count = 8, 
                   bool malicious = true, uint64_t stat_sec_param = 40);
    
    // 设置输入数据
    // inputs[i][j] 表示第i个OT实例的第j个可选值
    // inputs.size() = OT实例数量
    // inputs[i].size() = num_choices (必须一致)
    void set_inputs(const std::vector<std::vector<Element>>& inputs);
    
    // 执行OT协议 (与Receiver端协同运行)
    // 返回值: true表示成功, false表示失败
    bool execute();
    
    // 获取统计信息
    double get_total_time_ms() const;
    double get_base_ot_time_ms() const;
    double get_extension_time_ms() const;
    
private:
    class Impl;
    Impl* pImpl;
};

// ========================================
//  OOS OT Receiver封装类
// ========================================
class OOSOTReceiver {
public:
    OOSOTReceiver();
    ~OOSOTReceiver();
    
    // 配置OT参数 (必须与Sender端一致)
    void configure(size_t num_choices, uint32_t input_bit_count = 8,
                   bool malicious = true, uint64_t stat_sec_param = 40);
    
    // 设置选择索引
    // choices[i] 表示第i个OT实例的选择 (范围: 0 ~ num_choices-1)
    void set_choices(const std::vector<size_t>& choices);
    
    // 执行OT协议 (与Sender端协同运行)
    bool execute();
    
    // 获取输出结果
    // outputs[i] 表示第i个OT实例恢复的值
    const std::vector<Element>& get_outputs() const;
    
    // 获取统计信息
    double get_total_time_ms() const;
    double get_base_ot_time_ms() const;
    double get_extension_time_ms() const;
    
private:
    class Impl;
    Impl* pImpl;
};

// ========================================
//  便捷函数: 执行完整的k-out-of-n OT
// ========================================
// sender_inputs[i][j]: 第i个OT的第j个可选值
// receiver_choices[i]: 第i个OT的选择索引
// receiver_outputs[i]: 第i个OT恢复的值
// 返回: true表示成功
bool run_oos_ot(
    const std::vector<std::vector<Element>>& sender_inputs,
    const std::vector<size_t>& receiver_choices,
    std::vector<Element>& receiver_outputs,
    size_t& com_bytes,
    uint32_t input_bit_count,
    bool malicious = true);

#endif // OOS_OT_H
