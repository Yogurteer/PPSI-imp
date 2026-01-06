#include "sender.h"
#include "config.h"
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <random>

LPSISender::LPSISender(){
    // 初始化 P-256 曲线
    group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    
    // 生成随机标量 r_s (1 到 order-1)
    BN_CTX* ctx = BN_CTX_new();
    BIGNUM* order = BN_new();
    EC_GROUP_get_order(group, order, ctx); // 获取群的阶
    
    r_s = BN_new();
    BN_rand_range(r_s, order); // 生成随机数
    
    BN_free(order);
    BN_CTX_free(ctx);
    
    // 打开日志文件
    debug_log.open("sender_oprf_debug.log", std::ios::out | std::ios::trunc);
    if (!debug_log.is_open()) {
        std::cerr << "警告: 无法打开Sender日志文件" << std::endl;
    }
}

LPSISender::~LPSISender() {
    BN_free(r_s);
    EC_GROUP_free(group); // 释放群
    if (debug_log.is_open()) debug_log.close();
}

// 辅助: Point -> Bytes (33 bytes compressed)
Element LPSISender::point_to_bytes(const EC_POINT* point, BN_CTX* ctx) {
    size_t len = EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED, nullptr, 0, ctx);
    Element result(len);
    EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED, result.data(), len, ctx);
    return result;
}

// 辅助: Bytes -> Point
EC_POINT* LPSISender::bytes_to_point(const Element& bytes, BN_CTX* ctx) {
    EC_POINT* point = EC_POINT_new(group);
    if (!EC_POINT_oct2point(group, point, bytes.data(), bytes.size(), ctx)) {
        // 错误处理
        EC_POINT_free(point);
        return nullptr;
    }
    return point;
}

void LPSISender::set_input(const std::vector<std::pair<Element, Element>>& data) {
    input_data = data;
    // std::cout << "Sender: 设置输入 " << data.size() << " 个键值对" << std::endl;
}

// 哈希函数 H1: G -> {0,1}^λ
Element LPSISender::hash_H1(const Element& input) {
    Element result(32);
    SHA256(input.data(), input.size(), result.data());
    return result;
}

// 哈希函数 H2: {0,1}^λ × G -> {0,1}^L
Element LPSISender::hash_H2(const Element& key, const Element& data) {
    Element combined;
    combined.insert(combined.end(), key.begin(), key.end());
    combined.insert(combined.end(), data.begin(), data.end());
    
    // 生成足够长的伪随机流 (使用SHA256链式哈希)
    Element result;
    Element current_seed = combined;
    
    // 生成最多256字节 (足够大)
    for (int i = 0; i < 8; ++i) {  // 8 * 32 = 256 bytes
        unsigned char hash_out[32];
        SHA256(current_seed.data(), current_seed.size(), hash_out);
        result.insert(result.end(), hash_out, hash_out + 32);
        
        // 下一轮使用当前哈希输出作为种子
        current_seed.assign(hash_out, hash_out + 32);
    }
    
    return result;
}

Element LPSISender::xor_elements(const Element& a, const Element& b) {
    size_t max_len = std::max(a.size(), b.size());
    Element result(max_len, 0);
    
    for (size_t i = 0; i < max_len; ++i) {
        unsigned char byte_a = (i < a.size()) ? a[i] : 0;
        unsigned char byte_b = (i < b.size()) ? b[i] : 0;
        result[i] = byte_a ^ byte_b;
    }
    
    return result;
}

// Phase 1: DH-OPRF Step 2
ElementVector LPSISender::process_oprf_step2(const ElementVector& receiver_masked, std::vector<size_t>* shuffle_map) {
    // std::cout << "Sender: 处理OPRF Step 2..." << std::endl;
    
    // 写入日志头
    debug_log << "========== SENDER OPRF STEP 2 ==========" << std::endl;
    char* r_s_str = BN_bn2dec(r_s);
    debug_log << "发送方随机数 r_s: " << r_s_str << std::endl;
    OPENSSL_free(r_s_str);
    debug_log << std::endl;
    
    // 使用PRP打乱顺序（固定种子，确保可复现）
    ElementVector shuffled = receiver_masked;
    std::vector<size_t> indices(receiver_masked.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;  // 初始顺序 [0, 1, 2, 3, 4]
    }
    
    std::mt19937 gen(123456);  // 固定种子，可复现shuffle结果
    
    // 同时打乱数据和索引
    for (size_t i = receiver_masked.size() - 1; i > 0; --i) {
        std::uniform_int_distribution<size_t> dist(0, i);
        size_t j = dist(gen);
        std::swap(shuffled[i], shuffled[j]);
        std::swap(indices[i], indices[j]);
    }
    
    // 保存映射关系 (如果提供了指针)
    if (shuffle_map) {
        *shuffle_map = indices;
    }
    
    // 记录打乱映射到日志
    debug_log << "打乱映射 (shuffle_map):" << std::endl;
    for (size_t i = 0; i < indices.size(); ++i) {
        debug_log << "  打乱后位置[" << i << "] <- 原始位置[" << indices[i] << "]" << std::endl;
    }
    debug_log << std::endl;
    
    ElementVector result(shuffled.size());
    // [多线程] ECC 计算: (H(y)^r_c)^r_s
    #pragma omp parallel for num_threads(get_thread_count())
    for (size_t i = 0; i < shuffled.size(); ++i) {
        // 【关键】每个线程独立的 Context
        BN_CTX* local_ctx = BN_CTX_new();
        EC_POINT* point = bytes_to_point(shuffled[i], local_ctx);
        
        if (point) {
            // ECC 标量乘法: result = point * r_s
            EC_POINT_mul(group, point, nullptr, point, r_s, local_ctx);
            
            result[i] = point_to_bytes(point, local_ctx);
            EC_POINT_free(point);
        } else {
            // 错误处理: 填充空或抛出异常
            result[i] = Element(33, 0); 
        }
        
        BN_CTX_free(local_ctx);
    }
    
    return result;
}

// 【核心修改】计算 X' = H_1( H(x)^r_s )
void LPSISender::compute_X_prime() {
    std::cout << "Sender: 计算 X' (ECC + OpenMP)..." << std::endl;
    
    X_prime.resize(input_data.size());
    H_x_rs_bytes.resize(input_data.size());

    #pragma omp parallel for num_threads(get_thread_count())
    for (size_t i = 0; i < input_data.size(); ++i) {
        BN_CTX* local_ctx = BN_CTX_new();
        
        // 1. MapToPoint: H(x) -> Point P
        EC_POINT* point = map_data_to_point(group, input_data[i].first, local_ctx);
        
        // 2. Scalar Mult: P * r_s
        EC_POINT_mul(group, point, nullptr, point, r_s, local_ctx);
        
        // 3. 序列化 H(x)^r_s
        Element h_x_rs = point_to_bytes(point, local_ctx);
        H_x_rs_bytes[i] = h_x_rs;
        
        // 4. 计算 X' = H1(...)
        X_prime[i] = hash_H1(h_x_rs);
        
        EC_POINT_free(point);
        BN_CTX_free(local_ctx);
    }
}

// OPRF后重随机化数据 (使数据分布更均匀，便于后续cuckoo hash)
void LPSISender::reshuffle_data() {
    // std::cout << "Sender: OPRF后重随机化数据..." << std::endl;
    
    if (X_prime.empty() || H_x_rs_bytes.empty() || input_data.empty()) {
        std::cerr << "警告: 没有数据可以重随机化" << std::endl;
        return;
    }
    
    std::vector<size_t> indices(X_prime.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    
    // 使用随机种子打乱索引
    std::mt19937 gen(std::random_device{}());
    std::shuffle(indices.begin(), indices.end(), gen);
    
    // 按照打乱后的索引重新排列数据
    ElementVector shuffled_X_prime;
    ElementVector shuffled_H_x_rs;
    std::vector<std::pair<Element, Element>> shuffled_input_data;
    
    for (size_t idx : indices) {
        shuffled_X_prime.push_back(X_prime[idx]);
        shuffled_H_x_rs.push_back(H_x_rs_bytes[idx]);
        shuffled_input_data.push_back(input_data[idx]);
    }
    
    X_prime = std::move(shuffled_X_prime);
    H_x_rs_bytes = std::move(shuffled_H_x_rs);
    input_data = std::move(shuffled_input_data);
    
    // std::cout << "Sender: 数据重随机化完成" << std::endl;
}

// Phase 2: 构建主哈希桶 (3-way Simple Hash - Sender建立多副本)
void LPSISender::build_hash_buckets(size_t num_main_buckets, int num_hash_funcs) {
    std::cout << "Sender: 构建主哈希桶 (3-way Simple Hash)..." << std::endl;
    
    // 与Receiver保持一致，使用配置文件中的倍数参数

    num_main_b = num_main_buckets;
    size_t bucket_count = num_main_buckets;
    X_star.resize(num_main_b);
    bucket_keys.resize(num_main_b);
    
    // 为每个桶生成随机密钥 r_k
    for (size_t k = 0; k < num_main_b; ++k) {
        Element key(32);
        RAND_bytes(key.data(), 32);
        bucket_keys[k] = key;
    }
    
    // 使用 3-way simple hash 将元素分配到桶 (每个元素映射到3个主桶,建立副本)
    for (size_t i = 0; i < X_prime.size(); ++i) {
        const Element& x_prime = X_prime[i];
        const Element& x = input_data[i].first;
        const Element& v = input_data[i].second;
        
        std::string x_str(x.begin(), x.end());
        
        // 使用 3 个不同的哈希函数,将元素插入到3个不同的主桶
        for (int h = 0; h < num_hash_funcs; ++h) {
            size_t bucket_idx = instance_hash(x_prime, h, bucket_count);
            // 计算 masked_value = length(4 bytes) || (x || v ⊕ H_2(r_k, H(x)^r_s))
            Element x_concat_v;
            x_concat_v.insert(x_concat_v.end(), x.begin(), x.end());
            x_concat_v.insert(x_concat_v.end(), v.begin(), v.end());
            
            Element h2_result = hash_H2(bucket_keys[bucket_idx], H_x_rs_bytes[i]);
            h2_result.resize(x_concat_v.size());  // 调整到相同长度
            
            Element encrypted_data = xor_elements(x_concat_v, h2_result);
            
            // 在前面添加长度信息(4字节): data_len(2) + x_len(2)
            Element masked_value;
            size_t data_len = x_concat_v.size();
            size_t x_len = x.size();
            
            // 前2字节: 总数据长度
            masked_value.push_back(static_cast<unsigned char>(data_len >> 8));  // 高字节
            masked_value.push_back(static_cast<unsigned char>(data_len & 0xFF)); // 低字节
            
            // 接下来2字节: x 的长度
            masked_value.push_back(static_cast<unsigned char>(x_len >> 8));     // 高字节
            masked_value.push_back(static_cast<unsigned char>(x_len & 0xFF));   // 低字节
            
            // 最后是加密的数据
            masked_value.insert(masked_value.end(), encrypted_data.begin(), encrypted_data.end());
            
            SenderBucketData data;
            data.x_prime = x_prime;
            data.masked_value = masked_value;
            
            // 插入到第 h 个主桶 (建立副本)
            X_star[bucket_idx].push_back(data);
        }
    }
}

// Phase 2: 构建子哈希桶 (Sender使用Kuku Cuckoo Hash)
void LPSISender::build_sub_buckets(size_t sender_data_size, size_t num_main_buckets, int nh) {
    
    // 动态计算子桶容量
    size_t max_main_bucket_size = 0;
    size_t non_empty_buckets = 0;
    for (const auto& bucket : X_star) {
        if (!bucket.empty()) {
            non_empty_buckets++;
            max_main_bucket_size = std::max(max_main_bucket_size, bucket.size());
        }
    }
    
    // 每个主桶有nh个子桶,每个子桶的容量 = (主桶元素数 / nh) * 负载因子
    // 使用配置的因子计算容量
    size_t sub_bucket_capacity = static_cast<size_t>(std::ceil(max_main_bucket_size * LPSIConfig::SUB_BUCKET_FACTOR));
    
    // 确保最小容量
    sub_bucket_capacity = std::max(sub_bucket_capacity, size_t(20));
    
    std::cout << "[子桶容量] 子桶容量计算: " 
              << "max_bucket_size=" << max_main_bucket_size
              << ", nh=" << nh 
              << " -> 每个子桶capacity=" << sub_bucket_capacity 
              << ", 总容量=" << (sub_bucket_capacity * nh) << std::endl;
    
    std::cout << "Sender: 构建子哈希桶 (每个主桶独立Cuckoo Hash)..." << std::endl;
    
    X_sub_star.resize(X_star.size());
    
    // 随机数生成器用于Eviction
    std::mt19937 rng(std::random_device{}());
    
    // 为每个主桶独立构建内层Kuku hash
    for (size_t main_idx = 0; main_idx < X_star.size(); ++main_idx) {
        // 初始化子桶结构
        X_sub_star[main_idx].resize(nh);
        for (int sub_idx = 0; sub_idx < nh; ++sub_idx) {
            X_sub_star[main_idx][sub_idx].resize(sub_bucket_capacity);
        }
        
        const auto& main_bucket = X_star[main_idx];
        if (main_bucket.empty()) continue;
        
        // 去重: 外层simple hash在同一个主桶hash冲突,会产生重复元素影响后续内层cuckoo
        std::vector<SenderBucketData> unique_elements;
        std::set<Element> seen_x_primes;
        
        for (const auto& data : main_bucket) {
            if (seen_x_primes.find(data.x_prime) == seen_x_primes.end()) {
                seen_x_primes.insert(data.x_prime);
                unique_elements.push_back(data);
            }
        }
        
        // 使用真正的Cuckoo Hash (带Eviction) 为每个唯一元素分配位置
        bool bucket_success = true;
        
        for (const auto& initial_elem : unique_elements) {
            SenderBucketData current_item = initial_elem;
            bool placed = false;
            
            // 尝试插入，带有踢出机制 (Eviction)
            // 使用配置的最大重试次数
            int max_retries = LPSIConfig::MAX_RETRY; 
            if (max_retries <= 0) max_retries = 500; // 默认值保护
            
            for (int retry = 0; retry < max_retries; ++retry) {
                // 1. 尝试在nh个候选位置中找空位
                int empty_h = -1;
                size_t empty_slot = 0;
                
                for (int h = 0; h < nh; ++h) {
                    size_t slot = instance_hash(current_item.x_prime, 10 + h, sub_bucket_capacity);
                    if (X_sub_star[main_idx][h][slot].x_prime.empty()) {
                        empty_h = h;
                        empty_slot = slot;
                        break;
                    }
                }
                
                // 2. 如果找到空位，直接插入并结束当前元素的处理
                if (empty_h != -1) {
                    X_sub_star[main_idx][empty_h][empty_slot] = current_item;
                    placed = true;
                    break;
                }
                
                // 3. 如果没找到空位，随机踢出一个元素 (Eviction)
                // 随机选择一个子桶
                std::uniform_int_distribution<int> dist(0, nh - 1);
                int victim_h = dist(rng);
                size_t victim_slot = instance_hash(current_item.x_prime, 10 + victim_h, sub_bucket_capacity);
                
                // 交换: 把新元素放进去，把老元素踢出来变成 current_item，进入下一次循环尝试重新插入
                SenderBucketData victim_item = X_sub_star[main_idx][victim_h][victim_slot];
                X_sub_star[main_idx][victim_h][victim_slot] = current_item;
                current_item = victim_item;
                
                // 继续循环，尝试插入被踢出的 victim_item
            }
            
            if (!placed) {
                std::cerr << "ERROR: 主桶[" << main_idx << "] Cuckoo插入失败 (达到最大踢出次数)!" << std::endl;
                std::cerr << "  元素前16字节: ";
                for (size_t i = 0; i < std::min(current_item.x_prime.size(), size_t(16)); ++i) {
                    std::cerr << std::hex << std::setw(2) << std::setfill('0') << (int)current_item.x_prime[i];
                }
                std::cerr << std::dec << std::endl;
                
                double load = static_cast<double>(unique_elements.size()) / (sub_bucket_capacity * nh);
                std::cerr << "  当前桶负载率: " << (load * 100) << "%" << std::endl;
                
                bucket_success = false;
                break; // 只要有一个元素插入失败，该桶就失败
            }
        }
        
        if (!bucket_success) {
            std::cerr << "Sender构建子桶失败，程序退出。" << std::endl;
            exit(1);
        }
    }
    
    std::cout << "Sender: 子桶构建完成, 总子桶数 = " << (X_star.size() * nh) << std::endl;
}

// Phase 3: 准备PIR数据库 (按子桶为行组织，每个子桶是独立的一行)
void LPSISender::prepare_pir_database() {
    // std::cout << "Sender: 准备PIR数据库 (按子桶为行组织)..." << std::endl;
    
    // 打开专用日志文件记录扁平化数据库
    std::ofstream phase3_log("sender_phase3_database.log");
    phase3_log << "========== Sender Phase3: 扁平化PIR数据库 (子桶为行) ==========" << std::endl;
    phase3_log << "数据库组织: 每个子桶是一个独立的行" << std::endl;
    phase3_log << "行索引 = main_idx * nh + sub_idx" << std::endl;
    phase3_log << "总行数 = " << X_sub_star.size() << " × " << X_sub_star[0].size() 
              << " = " << (X_sub_star.size() * X_sub_star[0].size()) << std::endl;
    phase3_log << "每行大小 = " << X_sub_star[0][0].size() << " slots" << std::endl << std::endl;
    
    flattened_database.clear();
    
    size_t non_empty = 0;
    size_t row_idx = 0;
    
    // 按子桶为单位扁平化: 每个子桶是一行
    for (size_t main_idx = 0; main_idx < X_sub_star.size(); ++main_idx) {
        const auto& main_bucket = X_sub_star[main_idx];
        for (size_t sub_idx = 0; sub_idx < main_bucket.size(); ++sub_idx) {
            const auto& sub_bucket = main_bucket[sub_idx];
            
            phase3_log << "行[" << row_idx << "] = (主桶" << main_idx << ", 子桶" << sub_idx << "): ";
            
            size_t row_non_empty = 0;
            for (size_t slot_idx = 0; slot_idx < sub_bucket.size(); ++slot_idx) {
                const auto& slot = sub_bucket[slot_idx];
                if (!slot.x_prime.empty()) {
                    non_empty++;
                    row_non_empty++;
                }
                flattened_database.push_back(slot);
            }
            
            phase3_log << row_non_empty << " 个非空slot" << std::endl;
            row_idx++;
        }
    }
    
    phase3_log << "\n总计: " << flattened_database.size() << " 个slots, " 
              << non_empty << " 个非空" << std::endl;
    phase3_log.close();
    
    std::cout << "Sender: PIR数据库准备完成" << std::endl;
    std::cout << "  总行数: " << row_idx << " (每个子桶1行)" << std::endl;
    std::cout << "  每行大小: " << X_sub_star[0][0].size() << " slots" << std::endl;
    std::cout << "  非空slots: " << non_empty << " / " << flattened_database.size() << std::endl;
    // PIR数据库相比原始sender size的膨胀率
    double inflation = static_cast<double>(flattened_database.size()) / static_cast<double>(input_data.size());
    std::cout << "  PIR输入数据库相比原始数据集X膨胀率: x" << inflation << std::endl;
}

// 将PIR数据库转换为字节格式
std::vector<Element> LPSISender::get_pir_database_as_bytes(size_t& num_items, size_t& item_size) {
    num_items = flattened_database.size();
    // item_size = 128;
    item_size = LPSIConfig::PIR_ITEM_SIZE;  // 使用配置文件中的值
    
    std::vector<Element> result;
    
    int non_empty_count = 0;
    for (const auto& data : flattened_database) {
        Element item(item_size, 0);
        
        bool is_empty_slot = data.x_prime.empty() && data.masked_value.empty();
        
        if (is_empty_slot) {
            // **关键修复**: 空slot填充随机非0数据，避免PIR返回transparent密文
            // 使用固定模式避免随机性导致的不确定行为
            for (size_t i = 0; i < item_size; ++i) {
                item[i] = static_cast<unsigned char>((i + 1) % 256);
            }
        } else {
            // 前32字节: x_prime
            if (!data.x_prime.empty()) {
                size_t copy_len = std::min(data.x_prime.size(), size_t(32));
                std::copy(data.x_prime.begin(), data.x_prime.begin() + copy_len, item.begin());
                non_empty_count++;
            }
            
            // 后96字节: masked_value (从32字节扩展到96字节以容纳更大的数据)
            if (!data.masked_value.empty()) {
                size_t max_masked_size = item_size - 32;  // 剩余空间
                size_t copy_len = std::min(data.masked_value.size(), max_masked_size);
                std::copy(data.masked_value.begin(), data.masked_value.begin() + copy_len, 
                         item.begin() + 32);
                
                // 如果masked_value超过可用空间，输出警告
                if (data.masked_value.size() > max_masked_size) {
                    std::cerr << "警告: masked_value大小(" << data.masked_value.size() 
                              << ") 超过可用空间(" << max_masked_size << "), 数据被截断" << std::endl;
                }
            }
        }
        
        result.push_back(item);
    }
    
    // [验证] 输出前3个非空条目的信息
    // std::cout << "[验证-Sender] PIR数据库准备: " << non_empty_count << " 个非空条目" << std::endl;
    int shown = 0;
    for (size_t i = 0; i < result.size() && shown < 3; ++i) {
        bool is_empty = true;
        for (size_t b = 0; b < 32; ++b) {
            if (result[i][b] != 0) {
                is_empty = false;
                break;
            }
        }
        // 静默模式: 不在终端显示哈希值
        if (!is_empty) {
            // std::cout << "  条目[" << i << "] X'前8字节: ";
            // for (size_t b = 0; b < 8; ++b) {
            //     printf("%02x", result[i][b]);
            // }
            // std::cout << ", masked_value前8字节: ";
            // for (size_t b = 32; b < 40; ++b) {
            //     printf("%02x", result[i][b]);
            // }
            // std::cout << std::endl;
            shown++;
        }
    }
    
    return result;
}

// Phase 4: OT模拟 - 明文发送桶密钥
std::vector<Element> LPSISender::send_bucket_keys_plaintext(
    const std::vector<size_t>& requested_indices) {
    
    // std::cout << "Sender: 明文发送 " << requested_indices.size() << " 个桶密钥" << std::endl;
    
    
    std::vector<Element> keys;
    for (size_t idx : requested_indices) {
        if (idx < bucket_keys.size()) {
            keys.push_back(bucket_keys[idx]);
        } else {
            Element empty_key(32, 0);
            keys.push_back(empty_key);
        }
    }

    return keys;
}

// Phase 4: 准备真实OT输入
bool LPSISender::prepare_ot_inputs(size_t receiver_choice_count) {
    // std::cout << "Sender: 准备OT输入 (总共 " << bucket_keys.size() << " 个桶密钥)" << std::endl;
    
    // 验证
    if (bucket_keys.empty()) {
        std::cerr << "错误: 桶密钥未生成" << std::endl;
        return false;
    }

    intersection_size = receiver_choice_count;

    return true;
}

std::vector<std::vector<Element>> LPSISender::get_ot_inputs() const {
    // 为OT协议准备输入
    // 每个OT实例: Receiver从N个桶中选择一个
    // 策略: 每个命中的元素对应一个OT实例
    //       Sender提供所有桶的密钥作为选项
    //       Receiver选择对应主桶的索引
    
    std::vector<std::vector<Element>> ot_inputs;
    
    // 计算需要的OT实例数 (等于Receiver命中的元素数)
    // 但这个信息Sender不知道,所以需要从外部传入
    // 暂时返回一个包含所有桶密钥的向量,每个OT实例都提供完整的选择集
    
    // 注意: 这会在外部被复制为多个OT实例
    ot_inputs.push_back(bucket_keys);
    
    return ot_inputs;
}