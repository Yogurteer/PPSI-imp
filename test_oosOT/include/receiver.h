#ifndef RECEIVER_H
#define RECEIVER_H

#include "utils.h"

class Receiver {
private:
    PSI* crypto;
    ElementVector receiver_input;  // Y
    BIGNUM* r_c;  // 接收方随机数

    ElementVector Y_prime;      // 阶段1结果：H_1(H(y_i*)^r_s)
    ElementMatrix Y_star;       // 阶段2结果：接收方的桶
    std::vector<ElementMatrix> Y_sub_star;  // 子桶：Y_sub_star[主桶索引][子桶索引] = 元素列表
    
public:
    Receiver(PSI* crypto_core);
    ~Receiver();
    
    // 设置接收方输入数据 Y
    void set_input(const ElementVector& input);
    
    // DH-OPRF步骤1: 接收方计算 H(y_i)^r_c
    ElementVector dh_oprf_step1();
    
    // DH-OPRF步骤3: 接收方计算最终的Y'
    void dh_oprf_step3(const ElementVector& sender_output);
    
    // 哈希桶阶段：使用3-way cuckoo hash
    void hash_buckets_phase(HashBuckets& buckets);
    
    // 获取接收方数据数量
    size_t get_input_size() const { return receiver_input.size(); }
    
    // 获取接收方输入
    const ElementVector& get_input() const { return receiver_input; }

    // 获取部分结果
    const ElementVector& get_Y_prime() const { return Y_prime; }
    const ElementMatrix& get_Y_star() const { return Y_star; }

    // 子桶哈希阶段：对Y_star的每个主桶元素用多路布谷鸟哈希分配到子桶
    void sub_hash_buckets_phase(const HashBuckets& buckets);
    
    // 获取子桶数据
    const std::vector<ElementMatrix>& get_Y_sub_star() const { return Y_sub_star; }

};

#endif // RECEIVER_H