#ifndef LPSI_CONFIG_H
#define LPSI_CONFIG_H

#include <cstddef>
#include <cstdint>
#include <string>

// ========================================
//  可支付LPSI协议配置参数
// ========================================
class PpsiParm {
    
public:
    PpsiParm();
    ~PpsiParm();

    uint64_t poly_degree = 8192; // 多项式度数 (PIR参数)
};


namespace LPSIConfig {
    // ===== 哈希桶配置 =====
    const std::string input_data_main_cuckoo = "../data/input_data_main_cuckoo.txt";
    
    // 主桶数量倍数 (相对于receiver数据量)
    // 推荐值: 2.5 - 3.0 (更大的值提高Cuckoo哈希成功率，但增加通信开销)
    constexpr double MAIN_BUCKET_FACTOR = 1.5;
    constexpr double SUB_BUCKET_FACTOR = 0.6; // 每个sub桶容量倍数 (相对于主桶最大元素数)
    // 最大重试次数，防止无限循环
    constexpr size_t MAX_RETRY = 750;

    // 哈希函数数量 (Cuckoo哈希的候选位置数)
    constexpr int OUTER_NUM_HASH_FUNCTIONS = 3;
    
    // 子桶数量 (每个主桶内的子桶数，用于第二层哈希)
    // 注意：内层Cuckoo哈希的hash函数数量应该等于子桶数量
    constexpr int NUM_SUB_BUCKETS = 3;
    
    // 子桶容量 (每个子桶能容纳的元素数)
    // 注意：此值已不再使用，容量将根据实际数据规模动态计算
    constexpr size_t SUB_BUCKET_CAPACITY = 20;  // 保留作为最小容量
    
    // 桶密钥尺寸设置
    constexpr size_t BUCKET_KEY_SIZE_BYTES = 32;
    
    // ===== 哈希函数种子 =====
    
    // 用于不同哈希实例的种子值 (确保哈希函数的独立性)
    constexpr uint32_t HASH_SEEDS[] = {
        0x12345678, // 哈希函数 0
        0x9ABCDEF0, // 哈希函数 1
        0xFEDCBA98, // 哈希函数 2
        0x13579BDF, // 哈希函数 3
        0x2468ACE1, // 哈希函数 4
        0x0F1E2D3C, // 哈希函数 5
        0x89ABCDEF, // 哈希函数 6
        0xCAFEBABE, // 哈希函数 7
        0xDEADBEEF, // 哈希函数 8
        0xA5A5A5A5  // 哈希函数 9
    };
    
    // ===== PIR配置 =====
    
    // PIR数据库项大小 (字节)
    // x_prime + masked_value
    
    inline size_t PIR_PAYLOAD_SIZE = 64;
    
    // ===== 密码学参数 =====
    
    // 随机密钥大小 (字节)
    constexpr size_t BUCKET_KEY_SIZE = 32;
    
    // DH-OPRF素数位数
    constexpr int DH_PRIME_BITS = 2048;
}

#endif // LPSI_CONFIG_H
