#include "receiver.h"

Receiver::Receiver(PSI* crypto_core) : crypto(crypto_core) {
    r_c = BN_new();
    
    // 生成随机数
    const BIGNUM* order = EC_GROUP_get0_order(crypto->get_curve_group());
    BN_rand_range(r_c, order);
}

Receiver::~Receiver() {
    BN_free(r_c);
}

void Receiver::set_input(const ElementVector& input) {
    receiver_input = input;
}

ElementVector Receiver::dh_oprf_step1() {
    ElementVector result;
    
    for (const auto& y : receiver_input) {
        // 计算 H(y_i)
        Element hashed_y = crypto->hash_H(y);
        
        // 将哈希值映射到椭圆曲线点
        EC_POINT* point = crypto->map_to_curve(hashed_y);
        
        // 计算 point^r_c
        EC_POINT* result_point = EC_POINT_new(crypto->get_curve_group());
        EC_POINT_mul(crypto->get_curve_group(), result_point, nullptr, 
                   point, r_c, crypto->get_bn_ctx());
        
        // 转换为字节数组
        Element encoded = crypto->point_to_bytes(result_point);
        result.push_back(encoded);
        
        EC_POINT_free(point);
        EC_POINT_free(result_point);
    }
    
    return result;
}

void Receiver::dh_oprf_step3(const ElementVector& sender_output) {
    Y_prime.clear();
    // 计算 r_c^(-1)
    BIGNUM* r_c_inv = BN_new();
    const BIGNUM* order = EC_GROUP_get0_order(crypto->get_curve_group());
    BN_mod_inverse(r_c_inv, r_c, order, crypto->get_bn_ctx());
    
    for (const auto& encoded_point : sender_output) {
        // 解码点
        EC_POINT* point = crypto->bytes_to_point(encoded_point);
        
        // 计算 point^(r_c^(-1)) = H(y_i*)^r_s
        EC_POINT* result_point = EC_POINT_new(crypto->get_curve_group());
        EC_POINT_mul(crypto->get_curve_group(), result_point, nullptr, 
                   point, r_c_inv, crypto->get_bn_ctx());
        
        // 转换为字节数组并计算H_1
        Element point_bytes = crypto->point_to_bytes(result_point);
        Element h1_result = crypto->hash_H1(point_bytes);
        
        Y_prime.push_back(h1_result);
        
        EC_POINT_free(point);
        EC_POINT_free(result_point);
    }
    
    BN_free(r_c_inv);
}

void Receiver::hash_buckets_phase(HashBuckets& buckets) {
    size_t bucket_count = buckets.get_bucket_count();
    Y_star.clear();
    Y_star.resize(bucket_count);
    
    for (const auto& y_prime : Y_prime) {
        bool placed = false;
        Element current_item = y_prime;
        
        // 最多尝试20次
        for (int attempt = 0; attempt < 20 && !placed; ++attempt) {
            // 计算当前元素的3个候选桶
            std::vector<size_t> candidates(3);
            for (int h = 0; h < 3; ++h) {
                candidates[h] = buckets.compute_simple_hash_bucket(current_item, h);
            }
            
            // 步骤1：优先查找空桶
            for (size_t bucket_idx : candidates) {
                if (Y_star[bucket_idx].empty()) {
                    Y_star[bucket_idx].push_back(current_item);
                    placed = true;
                    break;
                }
            }
            
            // 步骤2：如果都满了，踢出第一个候选桶的元素
            if (!placed) {
                size_t kick_bucket = candidates[0];  // 选第一个候选桶
                Element evicted = Y_star[kick_bucket][0];
                Y_star[kick_bucket][0] = current_item;
                current_item = evicted;  // 被踢元素继续寻找位置
            }
        }
        
        if (!placed) {
            std::cerr << "Warning: Receiver主桶哈希失败\n";
        }
    }
}

void Receiver::sub_hash_buckets_phase(const HashBuckets& buckets) {
    size_t main_bucket_count = Y_star.size();
    size_t sub_bucket_count = buckets.get_sub_bucket_count();
    int nh = buckets.get_sub_nh();  // 与发送方一致的子桶哈希函数数量
    
    // 初始化子桶结构
    Y_sub_star.clear();
    Y_sub_star.resize(main_bucket_count);
    for (auto& sub_buckets : Y_sub_star) {
        sub_buckets.resize(sub_bucket_count);
    }
    
    // 遍历每个主桶，处理元素
    for (size_t main_idx = 0; main_idx < main_bucket_count; ++main_idx) {
        const auto& main_bucket = Y_star[main_idx];
        if (main_bucket.empty()) continue;

        for (const auto& y_prime : main_bucket) {
            // 计算nh个候选子桶
            std::vector<size_t> candidates(nh);
            for (int h = 0; h < nh; ++h) {
                candidates[h] = buckets.compute_sub_hash_bucket(y_prime, h);
            }

            // 直接放入所有nh个子桶
            for (size_t sub_idx : candidates) {
                Y_sub_star[main_idx][sub_idx].push_back(y_prime);
            }
        }
    }
}