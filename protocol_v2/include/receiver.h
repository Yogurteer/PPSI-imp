#ifndef LPSI_RECEIVER_H
#define LPSI_RECEIVER_H

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <memory>
#include <openssl/bn.h>
#include <openssl/sha.h>
#include <kuku/kuku.h>
#include "common.h"

// 前向声明
struct SenderBucketData;



class LPSIReceiver {
private:
    // 输入数据
    ElementVector input_data;  // Y
    
    // 密码学参数
    BIGNUM* r_c;              // 接收方随机数
    BIGNUM* r_c_inv;          // r_c的逆元
    EC_GROUP* group;          // 【修改】 ECC群
    
    // 中间结果
    ElementVector Y_prime;                               // H_1(H(y_i*)^r_s)
    ElementVector H_y_rs;                                // H(y_i*)^r_s (用于解密)
    std::vector<std::vector<Element>> Y_star;            // 主桶[main_idx][0] (cuckoo每桶1个)
    std::vector<std::vector<std::vector<Element>>> Y_sub_star;  // 子桶[main][sub][slot]
    
    // 查询索引
    std::vector<std::vector<size_t>> query_indices;      // [query_group][hash_idx] = local_index (局部索引)
    std::vector<size_t> valid_bucket_indices;            // 有效查询对应的主桶索引
    std::vector<size_t> element_to_main_bucket;          // [y_idx] = main_bucket_idx (记录每个元素映射到的唯一主桶)
    std::vector<size_t> hit_sub_bucket_indices;          // 记录每个命中元素对应的子桶索引（在nh个中的哪个）
    
    // Sender的桶结构信息 (用于索引转换)
    size_t sender_num_main_buckets;
    size_t sender_nh;
    size_t sender_sub_capacity;
    
    // 最终交集
    std::vector<std::pair<Element, Element>> intersection;  // (x_i, v_i)
    
    // 日志文件
    std::ofstream debug_log;
    
    // 辅助函数
    Element hash_H1(const Element& input);
    Element hash_H2(const Element& key, const Element& data);
    Element point_to_bytes(const EC_POINT* point, BN_CTX* ctx);
    EC_POINT* bytes_to_point(const Element& bytes, BN_CTX* ctx);
    Element xor_elements(const Element& a, const Element& b);
    
    // 保存数据到文件（16进制格式）
    void save_cuckoo_input_data(const std::string& filename, const ElementVector& data);
    
public:
    LPSIReceiver();
    ~LPSIReceiver();

    // PIR结果
    std::vector<Element> pir_results;                    // PIR返回的密文
    std::vector<Element> sim_pir_results;
    std::vector<size_t> hit_element_indices;             // 记录PIR命中的元素索引（在Y_prime中的位置）
    
    // 设置输入数据 Y
    void set_input(const ElementVector& data);
    
    // Phase 1: DH-OPRF
    ElementVector compute_oprf_step1();
    void process_oprf_step3(const ElementVector& sender_output, const std::vector<size_t>* shuffle_map = nullptr);
    void reshuffle_data();  // OPRF后重随机化数据
    
    // Phase 2: 哈希桶 + 子桶
    void build_hash_buckets(size_t bucket_count, int num_hash_funcs);
    void build_sub_buckets(size_t sender_data_size, size_t num_main_buckets, int nh);
    
    // Phase 3: 生成PIR查询索引
    void generate_pir_query_indices(size_t sender_num_main_buckets, 
                                     size_t sender_nh, 
                                     size_t sender_sub_capacity);
    std::vector<uint32_t> get_query_indices_flat() const;
    
    // Phase 4: 处理PIR结果
    void process_pir_results(const std::vector<Element>& results);
    
    // Phase 5: 获取桶密钥
    // 明文模拟 (用于测试)
    std::vector<size_t> get_valid_bucket_indices() const {
        // 只返回命中元素的主桶索引
        std::vector<size_t> hit_buckets;
        for (size_t elem_idx : hit_element_indices) {
            if (elem_idx < element_to_main_bucket.size()) {
                hit_buckets.push_back(element_to_main_bucket[elem_idx]);
            }
        }
        return hit_buckets;
    }
    
    // 真实OT协议: 获取选择索引
    // 返回: 每个命中元素对应的主桶索引 (用于OT选择)
    std::vector<size_t> get_ot_choices() const;
    
    // Phase 6: 解密得到交集
    void decrypt_intersection(const std::vector<Element>& bucket_keys);
    
    // 获取结果
    const std::vector<std::pair<Element, Element>>& get_intersection() const { 
        return intersection; 
    }
    const ElementVector& get_Y_prime() const { return Y_prime; }
    size_t get_total_queries() const;
    
    // 验证OPRF正确性 (接收Sender的r_s和shuffle_map)
    void verify_oprf_correctness(const BIGNUM* sender_r_s, 
                                  const std::vector<size_t>& shuffle_map,
                                  const ElementVector& sender_output);
    
    // 验证Phase2/3映射正确性 (检查PIR查询是否能命中Sender数据库)
    void verify_phase23_mapping(const std::vector<SenderBucketData>& sender_db,
                                 size_t sender_num_main_buckets,
                                 size_t sender_nh,
                                 size_t sender_sub_capacity);
};

#endif // LPSI_RECEIVER_H
