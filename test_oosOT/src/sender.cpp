#include "sender.h"

Sender::Sender(PSI* crypto_core) : crypto(crypto_core) {
    r_s = BN_new();
    
    // 生成随机数
    const BIGNUM* order = EC_GROUP_get0_order(crypto->get_curve_group());
    BN_rand_range(r_s, order);
}

Sender::~Sender() {
    BN_free(r_s);
}

void Sender::set_input(const std::vector<std::pair<Element, Element>>& input) {
    sender_input = input;
}

ElementVector Sender::dh_oprf_step2(const ElementVector& receiver_output) {
    // 使用PRP打乱
    PRP prp(receiver_output.size());
    ElementVector shuffled = prp.shuffle(receiver_output);
    
    ElementVector result;
    
    for (const auto& encoded_point : shuffled) {
        // 解码点
        EC_POINT* point = crypto->bytes_to_point(encoded_point);
        
        // 计算 point^r_s
        EC_POINT* result_point = EC_POINT_new(crypto->get_curve_group());
        EC_POINT_mul(crypto->get_curve_group(), result_point, nullptr, 
                   point, r_s, crypto->get_bn_ctx());
        
        // 转换为字节数组
        Element encoded = crypto->point_to_bytes(result_point);
        result.push_back(encoded);
        
        EC_POINT_free(point);
        EC_POINT_free(result_point);
    }
    
    return result;
}

void Sender::compute_X_prime() {
    X_prime.clear();
    H_x_rs_bytes.clear();
    
    for (const auto& pair : sender_input) {
        const Element& x = pair.first;
        
        // 计算 H(x_i)
        Element hashed_x = crypto->hash_H(x);
        
        // 将哈希值映射到椭圆曲线点
        EC_POINT* point = crypto->map_to_curve(hashed_x);
        
        // 计算 point^r_s
        EC_POINT* result_point = EC_POINT_new(crypto->get_curve_group());
        EC_POINT_mul(crypto->get_curve_group(), result_point, nullptr, 
                   point, r_s, crypto->get_bn_ctx());
        
        // 转换为字节数组并计算H_1
        Element point_bytes = crypto->point_to_bytes(result_point);
        Element h1_result = crypto->hash_H1(point_bytes);
        
        H_x_rs_bytes.push_back(point_bytes);
        X_prime.push_back(h1_result);
        
        EC_POINT_free(point);
        EC_POINT_free(result_point);
    }
}

void Sender::hash_buckets_phase(HashBuckets& buckets) {
    size_t bucket_count = buckets.get_bucket_count();
    X_star.clear();
    X_star.resize(bucket_count);
    bucket_keys.clear();
    bucket_keys.resize(bucket_count);
    
    // 为每个桶生成随机密钥r_k
    for (size_t i = 0; i < bucket_count; ++i) {
        bucket_keys[i].resize(32);
        RAND_bytes(bucket_keys[i].data(), 32);
    }
    
    // 将X_prime中的每个元素放入对应的桶
    for (size_t i = 0; i < X_prime.size(); ++i) {
        const Element& x_prime = X_prime[i];
        const Element& h_x_rs_bytes = H_x_rs_bytes[i];
        const Element& x_i = sender_input[i].first;
        const Element& v_i = sender_input[i].second;
        
        // 计算存储值: x_i || v_i ⊕ H_2(r_k, H(x_i)^r_s)
        Element combined_xv = x_i;
        combined_xv.insert(combined_xv.end(), v_i.begin(), v_i.end());

        // 使用三个哈希函数，将元素放入三个不同的桶
        for (int hash_func_idx = 0; hash_func_idx < 3; ++hash_func_idx) {
            // 计算应该放入哪个桶
            size_t bucket_idx = buckets.compute_simple_hash_bucket(x_prime, hash_func_idx);
            
            // 使用对应桶的密钥计算掩码
            Element mask = crypto->hash_H2(bucket_keys[bucket_idx], h_x_rs_bytes);
            
            // XOR操作
            Element masked_value = combined_xv;
            for (size_t j = 0; j < std::min(masked_value.size(), mask.size()); ++j) {
                masked_value[j] ^= mask[j];
            }
            
            // 存储 (H_1(H(x_i)^r_s), masked_value) 对
            // Element pair_data = x_prime;
            // pair_data.insert(pair_data.end(), masked_value.begin(), masked_value.end());
            // X_star[selected_bucket].push_back(pair_data);
            // 存储x_prime和masked_value（不拼接）
            SenderBucketData data;
            data.x_prime = x_prime;
            data.masked_value = masked_value;
            // 放入选中的主桶
            X_star[bucket_idx].push_back(data);
            }
    }
}

ElementVector Sender::get_X() const {
    ElementVector result;
    for (const auto& pair : sender_input) {
        result.push_back(pair.first);
    }
    return result;
}

void Sender::sub_hash_buckets_phase(const HashBuckets& buckets) {
    size_t main_bucket_count = X_star.size();
    size_t sub_bucket_count = buckets.get_sub_bucket_count();
    int nh = buckets.get_sub_nh();
    
    X_sub_star.clear();
    X_sub_star.resize(main_bucket_count);
    for (auto& sub_buckets : X_sub_star) {
        sub_buckets.resize(sub_bucket_count);
    }
    
    for (size_t main_idx = 0; main_idx < main_bucket_count; ++main_idx) {
        const auto& main_bucket = X_star[main_idx];
        if (main_bucket.empty()) continue;
        
        for (const auto& data : main_bucket) {
            bool placed = false;
            SenderBucketData current_data = data;
            
            // 最多尝试10次
            for (int attempt = 0; attempt < 10 && !placed; ++attempt) {
                // 计算当前元素的nh个候选子桶
                std::vector<size_t> candidates(nh);
                for (int h = 0; h < nh; ++h) {
                    candidates[h] = buckets.compute_sub_hash_bucket(current_data.x_prime, h);
                }
                
                // 步骤1：优先查找空桶
                for (size_t sub_idx : candidates) {
                    if (X_sub_star[main_idx][sub_idx].empty()) {
                        X_sub_star[main_idx][sub_idx].push_back(current_data);
                        placed = true;
                        break;
                    }
                }
                
                // 步骤2：如果都满了，踢出第一个候选桶的元素
                if (!placed) {
                    size_t kick_bucket = candidates[0];
                    SenderBucketData evicted = X_sub_star[main_idx][kick_bucket][0];
                    X_sub_star[main_idx][kick_bucket][0] = current_data;
                    current_data = evicted;  // 被踢元素继续寻找位置
                }
            }
            
            if (!placed) {
                std::cerr << "Warning: Sender子桶哈希失败（主桶" << main_idx << "）\n";
            }
        }
    }
}