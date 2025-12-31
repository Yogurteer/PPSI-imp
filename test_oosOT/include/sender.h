#ifndef SENDER_H
#define SENDER_H

#include "utils.h"

struct SenderBucketData {
    Element x_prime;         // 原始x_prime（用于子桶哈希）
    Element masked_value;    // 掩码后的payload
};

class Sender {
private:
    PSI* crypto;
    std::vector<std::pair<Element, Element>> sender_input;  // (X,V)对
    BIGNUM* r_s;  // 发送方随机数

    ElementVector X_prime;              // H_1(H(x_i)^r_s)
    ElementVector H_x_rs_bytes;         // H(x_i)^r_s
    // ElementMatrix X_star;               // 阶段2结果：发送方的桶
    std::vector<std::vector<SenderBucketData>> X_star;  // 主桶：每个元素包含x_prime和masked_value
    std::vector<Element> bucket_keys;   // 阶段2结果：桶密钥
    // std::vector<ElementMatrix> X_sub_star;  // 子桶：X_sub_star[主桶索引][子桶索引] = 元素列表
    std::vector<std::vector<std::vector<SenderBucketData>>> X_sub_star;  // 子桶：主桶->子桶->SenderBucketData
    
public:
    Sender(PSI* crypto_core);
    ~Sender();
    
    // 设置发送方输入数据 (X,V)
    void set_input(const std::vector<std::pair<Element, Element>>& input);
    
    // DH-OPRF步骤2: 发送方使用PRP打乱，然后计算 (H(y_i*)^r_c)^r_s
    ElementVector dh_oprf_step2(const ElementVector& receiver_output);
    
    // 计算X对应的H_1(H(x_i)^r_s)
    void compute_X_prime();
    
    // 哈希桶阶段：使用3-way simple hash
    void hash_buckets_phase(HashBuckets& buckets);
    
    // 获取发送方数据数量
    size_t get_input_size() const { return sender_input.size(); }
    
    // 获取发送方输入
    ElementVector get_X() const;

    // 获取部分结果
    const ElementVector& get_X_prime() const { return X_prime; }
    const std::vector<std::vector<SenderBucketData>>& get_X_star() const { return X_star; }
    const std::vector<Element>& get_bucket_keys() const { return bucket_keys; }

    // 子桶哈希阶段：对X_star的每个主桶元素用多路布谷鸟哈希分配到子桶
    void sub_hash_buckets_phase(const HashBuckets& buckets);
    
    // 获取子桶数据
    const std::vector<std::vector<std::vector<SenderBucketData>>>& get_X_sub_star() const { return X_sub_star; }

};

#endif // SENDER_H