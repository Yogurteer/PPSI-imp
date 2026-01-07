#ifndef COMMON_H
#define COMMON_H

#include <cstddef>
#include <cstdint>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <vector>
#include <cstdint>
#include <omp.h>

#include "config.h"
#include "kuku/kuku.h"

// 类型定义
using Element = std::vector<unsigned char>;
using ElementVector = std::vector<Element>;

// 测量ElementVector总字节数
inline size_t get_payload_size(const ElementVector& ev) {
    size_t total_bytes = 0;
    
    // 方法 1：简单的遍历累加 (推荐，清晰明了)
    for (const auto& element : ev) {
        // element.size() 返回的是内部 unsigned char 的数量，即字节数
        total_bytes += element.size();
    }

    return total_bytes;
}

// 【新增】配置部分
namespace MTConfig {
    // 默认并行线程数，设置为0则使用系统最大线程数
    constexpr int OMP_THREAD_COUNT = 64; 
}

// 辅助函数: 获取实际使用的线程数
inline int get_thread_count() {
    if (MTConfig::OMP_THREAD_COUNT > 0) return MTConfig::OMP_THREAD_COUNT;
    return omp_get_max_threads();
}

// 辅助函数: 打印Element (静默模式，只记录到日志)
inline void print_element(const std::string& name, const Element& elem, size_t max_bytes = 8) {
    // 静默模式：不在终端输出哈希值，仅记录到日志文件
    std::ofstream log("debug_elements.log", std::ios::app);
    if (log.is_open()) {
        log << name << ": ";
        for (size_t i = 0; i < std::min(elem.size(), max_bytes); ++i) {
            log << std::hex << std::setw(2) << std::setfill('0') << (int)elem[i];
        }
        if (elem.size() > max_bytes) log << "...";
        log << std::endl;
        log.close();
    }
}

// 1. 把任意长度字节序列压缩成 Kuku item_type（128 bit）
//    方法：使用 Kuku 官方 HashFunction::hash(bytes)
// 将任意长度的 bytes 压缩成 kuku::item_type (128-bit)
inline kuku::item_type make_item_from_bytes(const std::vector<uint8_t>& bytes)
{
    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256(bytes.data(), bytes.size(), hash);

    uint64_t high = 0, low = 0;

    // 前 8 字节 → high
    for (int i = 0; i < 8; i++) {
        high = (high << 8) | hash[i];
    }
    // 后 8 字节 → low
    for (int i = 8; i < 16; i++) {
        low = (low << 8) | hash[i];
    }

    return kuku::make_item(high, low);
}

// 2. 多实例哈希函数（使用 Kuku 官方 tabulation hashing）
inline size_t instance_hash(const Element& bytes,
                                int hash_idx,
                                size_t bucket_count)
{
    // Step 1: convert bytes → 128-bit item
    kuku::item_type item = make_item_from_bytes(bytes);

    // Step 2: 不同 hash_idx 使用不同 seed（转换成 item_type 满足 Kuku 接口）
    uint64_t seed_low = 0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(hash_idx + 1);
    uint64_t seed_high = seed_low ^ 0xD1B54A32D192ED03ULL;
    kuku::item_type seed_item = kuku::make_item(seed_low, seed_high);

    // 3. 构造 Kuku 的 LocFunc（Kuku 2.1 正确的哈希接口）
    kuku::LocFunc loc_func(static_cast<kuku::table_size_type>(bucket_count), seed_item);

    // 4. 调用定位函数，得到桶索引
    size_t pos = loc_func(item);

    return pos % bucket_count;
}

class MyTimer
{
public:
  MyTimer() { _start = std::chrono::high_resolution_clock::now(); }

  inline double elapsed()
  {
    _end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(_end - _start)
               .count() /
           1000.0;
  }

  inline void reset() { _start = std::chrono::high_resolution_clock::now(); }
  ~MyTimer(){};

private:
  std::chrono::high_resolution_clock::time_point _start, _end;
};

// 【新增】辅助函数: Hash-to-Curve (Try-and-Increment 方法)
// 将任意数据映射为 P-256 曲线上的一个有效点
inline EC_POINT* map_data_to_point(const EC_GROUP* group, const Element& data, BN_CTX* ctx) {
    EC_POINT* point = EC_POINT_new(group);
    BIGNUM* x_coord = BN_new();
    uint32_t counter = 0;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    
    // 复制数据，以便追加 counter
    std::vector<uint8_t> buffer = data;
    size_t original_size = buffer.size();
    buffer.resize(original_size + 4); // 预留4字节给 counter

    while (true) {
        // buffer = data || counter
        buffer[original_size] = (counter >> 24) & 0xFF;
        buffer[original_size + 1] = (counter >> 16) & 0xFF;
        buffer[original_size + 2] = (counter >> 8) & 0xFF;
        buffer[original_size + 3] = counter & 0xFF;

        SHA256(buffer.data(), buffer.size(), hash);
        BN_bin2bn(hash, SHA256_DIGEST_LENGTH, x_coord);

        // 尝试将 hash 值作为 X 坐标，恢复 Y 坐标 (压缩点解压)
        // 参数 0 表示我们要取偶数的 Y (为了确定性)
        if (EC_POINT_set_compressed_coordinates_GFp(group, point, x_coord, 0, ctx) == 1) {
            break; // 成功找到点
        }
        counter++;
    }

    BN_free(x_coord);
    return point;
}

#endif // COMMON_H
