#ifndef LPSI_SENDER_H
#define LPSI_SENDER_H

#include <vector>
#include <string>
#include <fstream>
#include <memory>
#include <openssl/bn.h>
#include <openssl/sha.h>
#include <kuku/kuku.h>
#include "common.h"

// Sender端的桶数据结构
struct SenderBucketData {
    Element x_prime;         // H_1(H(x_i)^r_s)
    Element masked_value;    // x_i || v_i ⊕ H_2(r_k, H(x_i)^r_s)
};

class LPSISender {
private:
    // 输入数据
    std::vector<std::pair<Element, Element>> input_data;  // (X, V) pairs
    
    // 密码学参数
    BIGNUM* r_s;              // 发送方随机数 (标量)
    EC_GROUP* group;          // 【修改】曲线群参数 (替代 prime_p)
    // BN_CTX* bn_ctx;        // 【删除】多线程不能共享同一个 ctx，将在函数内局部创建
    
    // 中间结果
    ElementVector X_prime;                              // H_1(H(x_i)^r_s)
    ElementVector H_x_rs_bytes;                         // H(x_i)^r_s (用于H_2计算)
    std::vector<Element> bucket_keys;                   // 每个主桶的随机密钥 r_k
    
    // 哈希桶数据
    std::vector<std::vector<SenderBucketData>> X_star;  // 主桶
    std::vector<std::vector<std::vector<SenderBucketData>>> X_sub_star;  // 子桶[main][sub][slot]
    
    // PIR数据库
    std::vector<SenderBucketData> flattened_database;   // 拼接后的数据库
    
    // 统计
    size_t intersection_size;
    
    // 日志文件
    std::ofstream debug_log;
    
    // 辅助函数
    // Element hash_H(const Element& input);
    Element hash_H1(const Element& input);
    Element hash_H2(const Element& key, const Element& data);
    Element xor_elements(const Element& a, const Element& b);
    // 将点转换为字节 (压缩格式)
    Element point_to_bytes(const EC_POINT* point, BN_CTX* ctx);
    // 将字节转换为点
    EC_POINT* bytes_to_point(const Element& bytes, BN_CTX* ctx);

    // 数据结构参数
    size_t num_main_b;
    
    
public:
    LPSISender();
    ~LPSISender();
    
    // 设置输入数据 (X, V)
    void set_input(const std::vector<std::pair<Element, Element>>& data);
    
    // Phase 1: DH-OPRF + PRP
    ElementVector process_oprf_step2(const ElementVector& receiver_masked, std::vector<size_t>* shuffle_map = nullptr);
    void compute_X_prime();
    void reshuffle_data();  // OPRF后重随机化数据
    
    // Phase 2: 哈希桶 + 子桶
    void build_hash_buckets(size_t receiver_size, int num_hash_funcs);
    void build_sub_buckets(size_t sender_data_size, size_t num_main_buckets, int nh);
    
    // Phase 3: 准备PIR数据库
    void prepare_pir_database();
    std::vector<Element> get_pir_database_as_bytes(size_t& num_items, size_t& item_size);
    
    // Phase 4: OT获取密钥
    // 明文模拟 (用于测试)
    std::vector<Element> send_bucket_keys_plaintext(const std::vector<size_t>& requested_indices);
    
    // 真实OT协议 (k-out-of-n OT)
    // receiver_choice_count: Receiver请求的桶数量 (用于构建OT实例)
    // 返回: OT执行是否成功
    bool prepare_ot_inputs();
    std::vector<std::vector<Element>> get_ot_inputs() const;
    
    // 获取统计信息
    size_t get_intersection_size() const { return intersection_size; }
    void set_intersection_size(size_t size) { intersection_size = size; }
    
    // 获取桶结构信息 (用于Receiver计算正确的PIR索引)
    size_t get_num_main_buckets() const { return num_main_b; }
    std::pair<size_t, size_t> get_sub_bucket_structure() const {
        if (X_sub_star.empty() || X_sub_star[0].empty()) return {0, 0};
        return {X_sub_star[0].size(), X_sub_star[0][0].size()};  // (nh, capacity)
    }
    
    // 调试接口
    const ElementVector& get_X_prime() const { return X_prime; }
    const ElementVector& get_H_x_rs_bytes() const { return H_x_rs_bytes; }
    const std::vector<Element>& get_bucket_keys() const { return bucket_keys; }
    size_t get_flattened_size() const { return flattened_database.size(); }
    const BIGNUM* get_r_s() const { return r_s; }
    const std::vector<SenderBucketData>& get_flattened_database() const { return flattened_database; }
};

#endif // LPSI_SENDER_H
