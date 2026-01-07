#include "receiver.h"
#include "sender.h"  // 需要 SenderBucketData 定义
#include "config.h"
#include <openssl/sha.h>
#include <cstring>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <random>
#include <cstdlib>
#include <ctime>
#include <fstream>

LPSIReceiver::LPSIReceiver() {
    // 初始化 P-256
    group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    
    BN_CTX* ctx = BN_CTX_new();
    BIGNUM* order = BN_new();
    EC_GROUP_get_order(group, order, ctx);
    
    r_c = BN_new();
    r_c_inv = BN_new();
    
    // 生成 r_c 和 r_c_inv (模 order)
    BN_rand_range(r_c, order);
    BN_mod_inverse(r_c_inv, r_c, order, ctx); // ECC是在群的阶上求逆
    
    BN_free(order);
    BN_CTX_free(ctx);
}

LPSIReceiver::~LPSIReceiver() {
    BN_free(r_c);
    BN_free(r_c_inv);
    EC_GROUP_free(group); // 释放群

    if (debug_log.is_open()) {
        debug_log.close();
    }
}

// 辅助: Point -> Bytes (33 bytes compressed)
Element LPSIReceiver::point_to_bytes(const EC_POINT* point, BN_CTX* ctx) {
    size_t len = EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED, nullptr, 0, ctx);
    Element result(len);
    EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED, result.data(), len, ctx);
    return result;
}

// 辅助: Bytes -> Point
EC_POINT* LPSIReceiver::bytes_to_point(const Element& bytes, BN_CTX* ctx) {
    EC_POINT* point = EC_POINT_new(group);
    if (!EC_POINT_oct2point(group, point, bytes.data(), bytes.size(), ctx)) {
        // 错误处理
        EC_POINT_free(point);
        return nullptr;
    }
    return point;
}

void LPSIReceiver::set_input(const ElementVector& data) {
    input_data = data;
    // std::cout << "Receiver: 设置输入 " << data.size() << " 个元素" << std::endl;
}

Element LPSIReceiver::hash_H1(const Element& input) {
    Element result(32);
    SHA256(input.data(), input.size(), result.data());
    return result;
}

Element LPSIReceiver::hash_H2(const Element& key, const Element& data) {
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

// Element LPSIReceiver::bn_to_bytes(const BIGNUM* bn) {
//     int bn_size = BN_num_bytes(bn);
//     Element result(bn_size);
//     BN_bn2bin(bn, result.data());
//     return result;
// }

// BIGNUM* LPSIReceiver::bytes_to_bn(const Element& bytes) {
//     return BN_bin2bn(bytes.data(), bytes.size(), nullptr);
// }

Element LPSIReceiver::xor_elements(const Element& a, const Element& b) {
    size_t max_len = std::max(a.size(), b.size());
    Element result(max_len, 0);
    
    for (size_t i = 0; i < max_len; ++i) {
        unsigned char byte_a = (i < a.size()) ? a[i] : 0;
        unsigned char byte_b = (i < b.size()) ? b[i] : 0;
        result[i] = byte_a ^ byte_b;
    }
    
    return result;
}

// 【核心修改】Phase 1: OPRF Step 1 (离线计算 H(y)^r_c)
ElementVector LPSIReceiver::compute_oprf_step1() {
    std::cout << "Receiver: 计算OPRF Step 1 (ECC + OpenMP)..." << std::endl;
    ElementVector result(input_data.size());

    #pragma omp parallel for num_threads(get_thread_count())
    for (size_t i = 0; i < input_data.size(); ++i) {
        BN_CTX* local_ctx = BN_CTX_new();
        
        // 1. Map y to Point P
        EC_POINT* point = map_data_to_point(group, input_data[i], local_ctx);
        
        // 2. Compute P * r_c
        EC_POINT_mul(group, point, nullptr, point, r_c, local_ctx);
        
        result[i] = point_to_bytes(point, local_ctx);
        
        EC_POINT_free(point);
        BN_CTX_free(local_ctx);
    }
    return result;
}

// 【核心修改】Phase 1: OPRF Step 3 (去盲)
void LPSIReceiver::process_oprf_step3(const ElementVector& sender_output, const std::vector<size_t>* shuffle_map) {
    std::cout << "Receiver: 处理OPRF Step 3 (ECC + OpenMP)..." << std::endl;
    
    Y_prime.resize(sender_output.size());
    H_y_rs.resize(sender_output.size());

    #pragma omp parallel for num_threads(get_thread_count())
    for (size_t i = 0; i < sender_output.size(); ++i) {
        BN_CTX* local_ctx = BN_CTX_new();
        
        // 1. 反序列化
        EC_POINT* point = bytes_to_point(sender_output[i], local_ctx);
        
        if (point) {
            // 2. 乘逆元: Point * r_c_inv = H(y)^{rc * rs * rc^-1} = H(y)^rs
            EC_POINT_mul(group, point, nullptr, point, r_c_inv, local_ctx);
            
            // 3. 保存结果 (用于 Phase 6 解密)
            H_y_rs[i] = point_to_bytes(point, local_ctx);
            
            // 4. 计算 Y' = H1(...) (用于 PSI 匹配)
            Y_prime[i] = hash_H1(H_y_rs[i]);
            
            EC_POINT_free(point);
        }
        BN_CTX_free(local_ctx);
    }
}

// OPRF后重随机化数据 (使数据分布更均匀，便于后续cuckoo hash)
void LPSIReceiver::reshuffle_data() {
    // std::cout << "Receiver: OPRF后重随机化数据..." << std::endl;
    
    if (Y_prime.empty() || H_y_rs.empty()) {
        std::cerr << "警告: 没有数据可以重随机化" << std::endl;
        return;
    }
    
    std::vector<size_t> indices(Y_prime.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    
    // 使用随机种子打乱索引
    std::mt19937 gen(std::random_device{}());
    std::shuffle(indices.begin(), indices.end(), gen);
    
    // 按照打乱后的索引重新排列数据
    ElementVector shuffled_Y_prime;
    ElementVector shuffled_H_y_rs;
    
    for (size_t idx : indices) {
        shuffled_Y_prime.push_back(Y_prime[idx]);
        shuffled_H_y_rs.push_back(H_y_rs[idx]);
    }
    
    Y_prime = std::move(shuffled_Y_prime);
    H_y_rs = std::move(shuffled_H_y_rs);
    
    // std::cout << "Receiver: 数据重随机化完成" << std::endl;
}

// 保存数据到文件（16进制格式）
void LPSIReceiver::save_cuckoo_input_data(const std::string& filename, const ElementVector& data) {
    std::ofstream outfile(filename, std::ios::out | std::ios::trunc);
    if (!outfile.is_open()) {
        std::cerr << "错误: 无法打开文件 " << filename << " 进行写入" << std::endl;
        return;
    }
    
    std::cout << "Receiver: 保存 " << data.size() << " 个元素到文件 " << filename << std::endl;
    
    // 写入文件头
    outfile << "========== Cuckoo Hash 输入数据 (16进制格式) ==========" << std::endl;
    outfile << "总元素数量: " << data.size() << std::endl;
    outfile << "每行格式: [序号] 数据(16进制)" << std::endl;
    outfile << "======================================================" << std::endl;
    outfile << std::endl;
    
    // 逐个写入元素
    for (size_t i = 0; i < data.size(); ++i) {
        const Element& elem = data[i];
        
        // 写入序号
        outfile << "[" << std::setw(6) << std::setfill('0') << i << "] ";
        
        // 写入数据的16进制表示
        for (size_t j = 0; j < elem.size(); ++j) {
            outfile << std::hex << std::setw(2) << std::setfill('0') 
                    << static_cast<int>(elem[j]);
        }
        
        outfile << std::dec << std::endl;  // 恢复十进制格式并换行
    }
    
    outfile.close();
    // std::cout << "Receiver: 数据保存完成" << std::endl;
}

// Phase 2: 构建主哈希桶 (Receiver 使用 Cuckoo Hash 确保每个元素映射到唯一主桶)
void LPSIReceiver::build_hash_buckets(size_t bucket_count, int num_hash_funcs)
{
    std::cout << "Receiver: 构建主哈希桶 (Cuckoo Hash)..." << std::endl;

    // 输出结构准备
    Y_star.resize(bucket_count);
    // 使用 bucket_count 作为无效值，因为有效的桶索引范围是 [0, bucket_count-1]
    size_t INVALID_BUCKET = bucket_count;
    element_to_main_bucket.resize(Y_prime.size(), INVALID_BUCKET);
    
    // 使用向量记录每个桶存储的元素索引，INVALID_BUCKET 表示空
    std::vector<size_t> bucket_to_element(bucket_count, INVALID_BUCKET);
    
    size_t cur_MAX_RETRY = LPSIConfig::MAX_RETRY;
    
    // 初始化随机种子
    std::srand(static_cast<unsigned int>(time(nullptr)));

    // // 保存main cuckoo hash的输入数据到文件
    // save_cuckoo_input_data(LPSIConfig::input_data_main_cuckoo, Y_prime);
    
    // 遍历所有 receiver 元素
    for (size_t y_idx = 0; y_idx < Y_prime.size(); ++y_idx)
    {
        const Element& y_prime = Y_prime[y_idx];
        
        // 计算候选桶
        std::vector<size_t> candidate_buckets;
        candidate_buckets.reserve(num_hash_funcs);
        for (int h = 0; h < num_hash_funcs; ++h)
        {
            size_t bucket_idx = instance_hash(y_prime, h, bucket_count);
            candidate_buckets.push_back(bucket_idx);
        }
        
        // 尝试插入当前元素
        size_t current_idx = y_idx;
        Element current_y_prime = y_prime;
        std::vector<size_t> current_candidates = candidate_buckets;
        
        bool inserted = false;
        for (size_t retry = 0; retry < cur_MAX_RETRY && !inserted; ++retry)
        {
            // 尝试所有候选桶
            for (size_t k = 0; k < current_candidates.size(); ++k)
            {
                size_t b = current_candidates[k];
                
                // 如果桶空，直接插入
                if (bucket_to_element[b] == INVALID_BUCKET)
                {
                    bucket_to_element[b] = current_idx;
                    Y_star[b].push_back(current_y_prime);
                    if (current_idx == y_idx) {
                        element_to_main_bucket[y_idx] = b;
                    } else {
                        // 更新被踢出元素的映射
                        element_to_main_bucket[current_idx] = b;
                    }
                    inserted = true;
                    break;
                }
            }
            
            if (!inserted)
            {
                // 所有候选桶都满了，随机踢出一个元素
                size_t victim_bucket = current_candidates[std::rand() % current_candidates.size()];
                size_t victim_idx = bucket_to_element[victim_bucket];
                
                // 保存被踢出元素的信息
                Element victim_y_prime = Y_prime[victim_idx];
                
                // 将当前元素放入该桶
                bucket_to_element[victim_bucket] = current_idx;
                Y_star[victim_bucket].clear();
                Y_star[victim_bucket].push_back(current_y_prime);
                if (current_idx == y_idx) {
                    element_to_main_bucket[y_idx] = victim_bucket;
                } else {
                    element_to_main_bucket[current_idx] = victim_bucket;
                }
                
                // 准备重新插入被踢出的元素
                current_idx = victim_idx;
                current_y_prime = victim_y_prime;
                
                // 重新计算被踢出元素的候选桶
                current_candidates.clear();
                for (int h = 0; h < num_hash_funcs; ++h)
                {
                    size_t bucket_idx = instance_hash(victim_y_prime, h, bucket_count);
                    current_candidates.push_back(bucket_idx);
                }
            }
        }
        
        if (!inserted)
        {
            std::cerr << "错误: 无法为元素 " << y_idx << " 找到合适的主桶，Cuckoo Hash 失败" << std::endl;
            // 回退到第一个候选桶
            size_t fallback_bucket = candidate_buckets[0];
            bucket_to_element[fallback_bucket] = y_idx;
            Y_star[fallback_bucket].push_back(y_prime);
            element_to_main_bucket[y_idx] = fallback_bucket;
        }
    }
    
    // 验证每个元素都有唯一的映射
    std::set<size_t> used_buckets;
    bool unique_mapping = true;
    for (size_t y_idx = 0; y_idx < Y_prime.size(); ++y_idx)
    {
        size_t bucket = element_to_main_bucket[y_idx];
        if (bucket == INVALID_BUCKET)
        {
            std::cerr << "错误: 元素 " << y_idx << " 没有映射到任何主桶" << std::endl;
            unique_mapping = false;
            continue;
        }
        if (used_buckets.find(bucket) != used_buckets.end())
        {
            std::cerr << "错误: 主桶 " << bucket << " 被多个元素映射" << std::endl;
            unique_mapping = false;
        }
        used_buckets.insert(bucket);
    }
    
    // 打印映射信息用于调试
    // std::cout << "主桶映射详情:" << std::endl;
    // for (size_t y_idx = 0; y_idx < Y_prime.size(); ++y_idx)
    // {
    //     size_t bucket = element_to_main_bucket[y_idx];
    //     std::cout << "  元素[" << y_idx << "] -> 主桶[" << bucket << "]" << std::endl;
    // }
    
    if (unique_mapping)
    {
        std::cout << "✓ 所有元素成功映射到唯一主桶" << std::endl;
    }
    else
    {
        std::cerr << "✗ 主桶映射存在冲突或未映射的元素" << std::endl;
        throw std::runtime_error("主桶映射失败，存在冲突或未映射的元素");
    }
    
    std::cout << "Receiver: 主哈希桶数量 = " << bucket_count << std::endl;
}

// Phase 2: 构建子哈希桶 (Receiver使用多路Simple Hash - 生成nh个候选位置)
void LPSIReceiver::build_sub_buckets(size_t sender_data_size, size_t num_main_buckets, int nh) {
    std::cout << "Receiver: 构建子哈希桶 (3way Simple Hash)..." << std::endl;
    
    // Receiver不需要实际构建子桶，只需要在Phase3使用Sender的容量参数计算hash
    // 这里仅预留结构用于验证
    Y_sub_star.resize(Y_star.size());
    for (size_t main_idx = 0; main_idx < Y_star.size(); ++main_idx) {
        Y_sub_star[main_idx].resize(nh);
    }
    for (size_t y_idx = 0; y_idx < Y_prime.size(); ++y_idx) {
        
    }
    
    // std::cout << "Receiver: 子哈希桶结构预留完成" << std::endl;
}

// Phase 3: 生成PIR查询索引 (对应sender的Kuku cuckoo hash位置)
void LPSIReceiver::generate_pir_query_indices(size_t sender_num_main_buckets,
                                                size_t sender_nh,
                                                size_t sender_sub_capacity) {
    // 保存sender的桶结构信息
    this->sender_num_main_buckets = sender_num_main_buckets;
    this->sender_nh = sender_nh;
    this->sender_sub_capacity = sender_sub_capacity;
    
    // std::cout << "Receiver: 生成PIR查询索引..." << std::endl;
    std::cout << "Sender桶结构: " << sender_num_main_buckets << " 个主桶, "
              << sender_nh << " 个子桶, 每个容量 " << sender_sub_capacity << std::endl;
    
    query_indices.clear();
    valid_bucket_indices.clear();
    
    // 使用与sender和验证阶段相同的instance_hash函数确保一致性
    
    // 为每个主桶生成查询
    for (size_t main_idx = 0; main_idx < sender_num_main_buckets; ++main_idx) {
        std::vector<size_t> local_query_indices;  // 该主桶的nh个局部索引
        
        // 查找是否有元素映射到这个主桶
        bool has_element = false;
        for (size_t y_idx = 0; y_idx < Y_prime.size(); ++y_idx) {
            if (element_to_main_bucket[y_idx] == main_idx) {
                // 找到了映射到这个主桶的元素
                const Element& y_prime = Y_prime[y_idx];
                has_element = true;
                
                // **修复**: 直接使用instance_hash，无需创建Kuku表
                
                // **关键修复**: 使用与验证阶段相同的instance_hash函数
                // 不使用Kuku库的all_locations，直接调用instance_hash确保一致性
                for (size_t h = 0; h < sender_nh; ++h) {
                    size_t slot = instance_hash(y_prime, 10 + h, sender_sub_capacity);
                    local_query_indices.push_back(slot);
                }
                
                // 如果候选位置不足nh个,用随机位置填充
                while (local_query_indices.size() < sender_nh) {
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<size_t> dis(0, sender_sub_capacity - 1);
                    local_query_indices.push_back(dis(gen));
                }
                
                break;  // 每个主桶最多1个元素
            }
        }
        
        if (!has_element) {
            // 空桶：生成nh个随机dummy查询
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<size_t> dis(0, sender_sub_capacity - 1);
            
            for (size_t h = 0; h < sender_nh; ++h) {
                local_query_indices.push_back(dis(gen));
            }
        }
        
        // 保存该主桶的查询索引
        query_indices.push_back(local_query_indices);
        valid_bucket_indices.push_back(main_idx);
    }
    
    std::cout << "Receiver: PIR查询索引生成完成, 总主桶数 = " << query_indices.size() 
              << ", 总查询数 = " << (query_indices.size() * sender_nh) << std::endl;
    double y_inflation = static_cast<double>(query_indices.size() * sender_nh) / static_cast<double>(input_data.size());
    std::cout << "  PIR输入查询集合相比原始查询数据Y膨胀率: x" << y_inflation << std::endl;
}

std::vector<uint32_t> LPSIReceiver::get_query_indices_flat() const {
    std::vector<uint32_t> flat;
    
    // 将调试信息写入日志文件
    std::ofstream debug_log("receiver_query_indices_flat.log");
    debug_log << "========== 平坦化查询索引 (虚拟行模式) ==========" << std::endl;
    debug_log << "PIR数据库: 按子桶为行, 总行数 = " << (sender_num_main_buckets * sender_nh) << std::endl;
    debug_log << "每行大小: " << sender_sub_capacity << " slots" << std::endl;
    debug_log << "主桶数量: " << query_indices.size() << std::endl << std::endl;
    
    for (size_t main_idx = 0; main_idx < query_indices.size(); ++main_idx) {
        const auto& local_indices = query_indices[main_idx];  // nh个局部索引
        
        debug_log << "主桶[" << main_idx << "]: ";
        
        // 每个主桶有nh个查询，对应nh个虚拟行
        for (size_t h = 0; h < sender_nh; ++h) {
            size_t virtual_row = main_idx * sender_nh + h;  // 虚拟行索引
            size_t slot = local_indices[h];  // 该虚拟行中的slot位置
            
            // **关键修复**: 验证slot不超出容量
            if (slot >= sender_sub_capacity) {
                debug_log << "[ERROR] slot=" << slot << " >= capacity=" << sender_sub_capacity << "! ";
                // 截断到有效范围
                slot = sender_sub_capacity - 1;
            }
            
            size_t global_idx = virtual_row * sender_sub_capacity + slot;
            
            flat.push_back(static_cast<uint32_t>(global_idx));
            
            debug_log << "(虚拟行" << virtual_row << ", slot=" << slot 
                      << ") -> " << global_idx << "  ";
        }
        debug_log << std::endl;
    }
    
    debug_log << "\n总查询数: " << flat.size() << " (每个主桶 " << sender_nh << " 个查询)" << std::endl;
    debug_log.close();
    
    return flat;
}

size_t LPSIReceiver::get_total_queries() const {
    size_t total = 0;
    for (const auto& group : query_indices) {
        total += group.size();
    }
    return total;
}

void LPSIReceiver::process_pir_results(const std::vector<Element>& results) {
    // std::cout << "\nReceiver: 处理PIR结果..." << std::endl;
    pir_results = results;
    std::cout << "Receiver: 收到 " << results.size() << " 个PIR结果" << std::endl;
    
    hit_element_indices.clear();
    hit_sub_bucket_indices.clear();
    
    std::cout << "\n========== 筛选PIR命中元素 ==========" << std::endl;
    
    // 现在每个主桶有nh个查询结果
    size_t result_idx = 0;
    for (size_t main_idx = 0; main_idx < query_indices.size(); ++main_idx) {
        // 检查这个主桶是否有Receiver的元素
        bool is_valid_bucket = false;
        size_t elem_idx = 0;
        for (size_t y_idx = 0; y_idx < Y_prime.size(); ++y_idx) {
            if (element_to_main_bucket[y_idx] == main_idx) {
                is_valid_bucket = true;
                elem_idx = y_idx;
                break;
            }
        }
        
        if (!is_valid_bucket) {
            // 空桶：跳过nh个dummy结果
            result_idx += sender_nh;
            continue;
        }
        
        // 有效桶：检查nh个查询结果，找到命中的
        bool found_hit = false;
        for (size_t h = 0; h < sender_nh; ++h) {
            if (result_idx >= pir_results.size()) {
                std::cerr << "ERROR: PIR结果索引越界!" << std::endl;
                result_idx++;
                continue;
            }
            
            const Element& pir_result = pir_results[result_idx];
            
            if (pir_result.size() < 32) {
                result_idx++;
                continue;  // 结果太小，跳过
            }
            
            // 提取 x_prime (前32字节)
            Element x_prime(pir_result.begin(), pir_result.begin() + 32);
            
            // 检查是否命中: x_prime == y_prime
            if (elem_idx < Y_prime.size() && x_prime == Y_prime[elem_idx]) {
                if (!found_hit) {  // 只记录一次
                    hit_element_indices.push_back(elem_idx);
                    hit_sub_bucket_indices.push_back(h);  // 记录命中的子桶索引
                    found_hit = true;
                    // std::cout << "主桶[" << main_idx << "] 元素[" << elem_idx << "]: 命中✓ (hash" << h << ")" << std::endl;
                }
            }
            result_idx++;  // 移动到下一个结果
        }
        
        if (!found_hit && is_valid_bucket) {
            std::cout << "主桶[" << main_idx << "] 元素[" << elem_idx << "]: 未命中 (所有" << sender_nh << "个查询都未找到)" << std::endl;
        }
    }
    
    std::cout << "总计: 命中 " << hit_element_indices.size() << "/" << Y_prime.size() << " 个元素" << std::endl;
}

// Phase 5: 获取OT选择索引
std::vector<size_t> LPSIReceiver::get_ot_choices() const {
    std::vector<size_t> choices;
    
    // 返回每个命中元素对应的主桶索引
    for (size_t elem_idx : hit_element_indices) {
        if (elem_idx < element_to_main_bucket.size()) {
            choices.push_back(element_to_main_bucket[elem_idx]);
        }
    }
    
    std::cout << "Receiver: 生成 " << choices.size() << " 个OT选择索引" << std::endl;
    return choices;
}

// Phase 6: 解密得到交集（只处理命中的元素）
void LPSIReceiver::decrypt_intersection(const std::vector<Element>& bucket_keys) {
    std::cout << "\n========== 解密交集 ==========" << std::endl;
    std::cout << "处理 " << hit_element_indices.size() << " 个命中元素..." << std::endl;
    
    if (bucket_keys.size() != hit_element_indices.size()) {
        std::cout << "警告: bucket_keys数量(" << bucket_keys.size() 
                  << ") 与命中元素数(" << hit_element_indices.size() << ") 不匹配" << std::endl;
    }
    
    if (hit_element_indices.size() != hit_sub_bucket_indices.size()) {
        std::cout << "错误: hit_element_indices和hit_sub_bucket_indices大小不一致" << std::endl;
        return;
    }
    
    intersection.clear();
    
    // 只处理命中的元素
    for (size_t key_idx = 0; key_idx < hit_element_indices.size(); ++key_idx) {
        size_t elem_idx = hit_element_indices[key_idx];
        size_t sub_bucket_idx = hit_sub_bucket_indices[key_idx];
        
        // std::cout << "\n--- 处理命中元素[" << elem_idx << "] (key_idx=" << key_idx 
        //           << ", 子桶索引=" << sub_bucket_idx << ") ---" << std::endl;
        
        if (elem_idx >= element_to_main_bucket.size()) {
            std::cout << "  错误: elem_idx越界" << std::endl;
            continue;
        }
        
        // 找到该元素对应的主桶索引
        size_t main_bucket_idx = element_to_main_bucket[elem_idx];
        
        if (main_bucket_idx >= query_indices.size()) {
            std::cout << "  错误: main_bucket_idx越界" << std::endl;
            continue;
        }
        
        const auto& indices = query_indices[main_bucket_idx];
        
        if (key_idx >= bucket_keys.size()) {
            std::cout << "  跳过: bucket_keys 索引越界" << std::endl;
            continue;
        }
        
        if (sub_bucket_idx >= indices.size()) {
            std::cout << "  错误: sub_bucket_idx越界" << std::endl;
            continue;
        }
        
        const Element& r_k = bucket_keys[key_idx];
        
        // std::cout << "  元素映射到主桶[" << main_bucket_idx << "]" << std::endl;
        
        // 计算该主桶的PIR结果起始位置
        // PIR结果是按主桶顺序组织的，每个主桶有 sender_nh 个结果
        size_t result_start_idx = main_bucket_idx * sender_nh;
        
        // 只检查命中的子桶查询，而不是遍历所有子桶
        size_t query_idx = sub_bucket_idx;
        size_t result_idx = result_start_idx + query_idx;
        
        if (result_idx >= pir_results.size()) {
            std::cout << "  错误: PIR结果索引越界" << std::endl;
            continue;
        }
        
        const Element& pir_result = pir_results[result_idx];
        
        // std::cout << "  命中查询[子桶" << query_idx << "] 索引=" << indices[query_idx] 
        //           << ", PIR结果大小=" << pir_result.size() << " bytes" << std::endl;
        
        if (pir_result.size() < 32) {
            std::cout << "    -> 结果太小,跳过" << std::endl;
            continue;
        }
        
        // 提取 x_prime (前32字节)
        Element x_prime(pir_result.begin(), pir_result.begin() + 32);
        
        // 检查是否匹配当前 Y_prime[elem_idx]（应该匹配，因为这是命中查询）
        if (elem_idx < Y_prime.size() && x_prime == Y_prime[elem_idx]) {
            // std::cout << "    ✓ 匹配! 开始解密..." << std::endl;
            
            // 提取 masked_value (后32字节)
            Element masked_value_with_len(pir_result.begin() + 32, pir_result.end());
            
            if (masked_value_with_len.size() < 4) {
                std::cout << "    错误: masked_value 太小" << std::endl;
                continue;
            }
            
            // 读取实际数据长度(前2字节)
            size_t data_len = (static_cast<size_t>(masked_value_with_len[0]) << 8) | 
                              static_cast<size_t>(masked_value_with_len[1]);
            
            // 读取 x 的长度(接下来2字节)
            size_t x_len = (static_cast<size_t>(masked_value_with_len[2]) << 8) | 
                           static_cast<size_t>(masked_value_with_len[3]);
            
            // // 详细的调试信息
            // std::cout << "\n    [调试] 解密数据长度信息:" << std::endl;
            // std::cout << "      PIR结果总大小: " << pir_result.size() << " bytes" << std::endl;
            // std::cout << "      x_prime大小: 32 bytes" << std::endl;
            // std::cout << "      masked_value_with_len大小: " << masked_value_with_len.size() << " bytes" << std::endl;
            // std::cout << "      读取到的data_len: " << data_len << " bytes" << std::endl;
            // std::cout << "      读取到的x_len: " << x_len << " bytes" << std::endl;
            // std::cout << "      可用数据空间(减去4字节头): " << (masked_value_with_len.size() - 4) << " bytes" << std::endl;
            
            // 检查data_len合理性
            if (data_len > masked_value_with_len.size() - 4) {
                std::cout << "    ✗ 错误: data_len(" << data_len << ") > 可用空间(" 
                          << (masked_value_with_len.size() - 4) << ")" << std::endl;
                std::cout << "      头部4字节(hex): ";
                for (size_t i = 0; i < 4 && i < masked_value_with_len.size(); ++i) {
                    std::cout << std::hex << std::setw(2) << std::setfill('0') 
                              << static_cast<int>(masked_value_with_len[i]) << " ";
                }
                std::cout << std::dec << std::endl;
                continue;
            }
            
            // 检查x_len合理性
            if (x_len > data_len) {
                std::cout << "    ✗ 错误: x_len(" << x_len << ") > data_len(" 
                          << data_len << ")" << std::endl;
                continue;
            }
            
            // std::cout << "    ✓ 长度检查通过" << std::endl;
            
            // 提取加密的数据部分(跳过前4字节头部)
            Element encrypted_data(masked_value_with_len.begin() + 4, 
                                  masked_value_with_len.begin() + 4 + data_len);
            
            // 解密: x || v = encrypted_data ⊕ H_2(r_k, H(y)^r_s)
            if (elem_idx >= H_y_rs.size()) {
                std::cout << "    错误: H_y_rs 索引越界" << std::endl;
                continue;
            }
            
            Element h2_result = hash_H2(r_k, H_y_rs[elem_idx]);
            h2_result.resize(data_len);
            
            Element decrypted = xor_elements(encrypted_data, h2_result);
            
            // 分离 x 和 v: 使用存储的 x_len
            if (x_len > decrypted.size()) {
                std::cout << "    错误: x_len 超出解密数据范围" << std::endl;
                continue;
            }
            
            Element x(decrypted.begin(), decrypted.begin() + x_len);
            Element v(decrypted.begin() + x_len, decrypted.end());
            
            std::string x_str(x.begin(), x.end());
            
            // 将value的每个字节直接作为16进制字符打印（每字节是0-15）
            std::ostringstream v_hex;
            for (size_t b = 0; b < v.size(); ++b) {
                v_hex << std::hex << static_cast<int>(static_cast<unsigned char>(v[b]));
            }
            
            // std::cout << "    ✓ 解密成功: x=\"" << x_str << "\", v=0x" << v_hex.str() << std::endl;
            
            intersection.push_back({x, v});
        } else {
            std::cout << "  ✗ 错误: 命中的PIR结果不匹配Y_prime" << std::endl;
        }
    }
    
    std::cout << "\n========== 解密完成 ==========" << std::endl;
    std::cout << "Receiver: 解密得到交集大小 = " << intersection.size() << std::endl;
}

// 【完全重写】验证OPRF正确性 (适配 ECC)
void LPSIReceiver::verify_oprf_correctness(const BIGNUM* sender_r_s, 
                                           const std::vector<size_t>& shuffle_map,
                                           const ElementVector& sender_output) {
    if (!sender_r_s || input_data.empty() || H_y_rs.empty() || shuffle_map.empty()) {
        std::cout << "[DEBUG] 跳过OPRF验证 (数据不全)" << std::endl;
        return;
    }

    debug_log << "\n========== RECEIVER OPRF 对照验证 (ECC版) ==========" << std::endl;

    // 离线计算正确的 H(y)^r_s = sender_r_s * MapToPoint(y)
    std::vector<Element> offline_h_y_rs(input_data.size());

    // 使用多线程加速验证计算
    #pragma omp parallel for num_threads(get_thread_count())
    for (size_t idx = 0; idx < input_data.size(); ++idx) {
        BN_CTX* local_ctx = BN_CTX_new();
        
        // 1. MapToPoint
        EC_POINT* point = map_data_to_point(group, input_data[idx], local_ctx);
        
        // 2. 标量乘法: point = point * sender_r_s
        EC_POINT_mul(group, point, nullptr, point, sender_r_s, local_ctx);
        
        // 3. 转字节
        offline_h_y_rs[idx] = point_to_bytes(point, local_ctx);
        
        EC_POINT_free(point);
        BN_CTX_free(local_ctx);
    }

    // 对比验证
    size_t match_count = 0;
    // ... 保持原有 shuffle 映射对比逻辑，但对比的是 ECC 点 ...
    
    for (size_t i = 0; i < std::min(H_y_rs.size(), shuffle_map.size()); ++i) {
        size_t original_idx = shuffle_map[i];
        if (original_idx >= offline_h_y_rs.size()) continue;

        // 对比: 本地计算的(shuffle后) vs 协议在线跑出来的 H_y_rs
        if (offline_h_y_rs[original_idx] == H_y_rs[i]) {
            match_count++;
        } else {
             debug_log << "Mismatch at index " << i << std::endl;
        }
    }
    
    std::cout << "[OPRF验证] " << match_count << "/" << H_y_rs.size() << " 匹配" << std::endl;
}

void LPSIReceiver::verify_phase23_mapping(const std::vector<SenderBucketData>& sender_db,
                                           size_t sender_num_main_buckets,
                                           size_t sender_nh,
                                           size_t sender_sub_capacity) {
    // std::cout << "\n========== Phase2/3 映射验证 ==========" << std::endl;
    
    size_t hit_count = 0;
    size_t miss_count = 0;
    
    // 验证前几个receiver元素的映射正确性
    size_t verify_count = Y_prime.size();
    
    for (size_t y_idx = 0; y_idx < verify_count; ++y_idx) {
        const Element& y_prime = Y_prime[y_idx];
        
        // 找到该元素映射的主桶
        size_t main_bucket_idx = element_to_main_bucket[y_idx];
        
        // std::cout << "验证元素[" << y_idx << "] -> 主桶[" << main_bucket_idx << "]: ";
        
        // 在新的架构中，需要检查sender在对应主桶中是否有相同的元素
        // Sender使用内层simple hash，每个元素会尝试3个子桶位置
        bool found_match = false;
        
        for (size_t h = 0; h < sender_nh && !found_match; ++h) {
            // 计算该元素在子桶h中的位置 (与sender的计算保持一致)
            size_t slot = instance_hash(y_prime, 10 + h, sender_sub_capacity);
            size_t virtual_row = main_bucket_idx * sender_nh + h;  // 子桶对应的虚拟行
            size_t global_idx = virtual_row * sender_sub_capacity + slot;
            
            if (global_idx < sender_db.size()) {
                const auto& db_entry = sender_db[global_idx];
                
                if (!db_entry.x_prime.empty() && db_entry.x_prime == y_prime) {
                    found_match = true;
                    // std::cout << "命中 (子桶" << h << ", slot=" << slot << ")";
                    break;
                }
            }
        }
        
        if (found_match) {
            hit_count++;
            // std::cout << " ✓" << std::endl;
        } else {
            miss_count++;
            std::cout << " ✗ 未命中" << std::endl;
            
            // 详细调试信息
            std::cout << "  调试信息: Y'前8字节=";
            for (size_t i = 0; i < std::min(y_prime.size(), size_t(8)); ++i) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)y_prime[i];
            }
            std::cout << std::dec << std::endl;
            
            // 检查3个候选位置
            for (size_t h = 0; h < sender_nh; ++h) {
                size_t slot = instance_hash(y_prime, 10 + h, sender_sub_capacity);
                size_t virtual_row = main_bucket_idx * sender_nh + h;
                size_t global_idx = virtual_row * sender_sub_capacity + slot;
                
                std::cout << "    子桶" << h << ", slot=" << slot << ", global_idx=" << global_idx;
                
                if (global_idx < sender_db.size()) {
                    const auto& db_entry = sender_db[global_idx];
                    if (db_entry.x_prime.empty()) {
                        std::cout << " (空)";
                    } else {
                        std::cout << " (有其他元素)";
                    }
                } else {
                    std::cout << " (越界)";
                }
                std::cout << std::endl;
            }
            
            // **新增**: 扫描整个主桶，找到该元素实际存储的位置
            std::cout << "  扫描整个主桶寻找该元素..." << std::endl;
            bool found_in_bucket = false;
            for (size_t h = 0; h < sender_nh; ++h) {
                size_t virtual_row = main_bucket_idx * sender_nh + h;
                size_t row_start = virtual_row * sender_sub_capacity;
                
                for (size_t slot = 0; slot < sender_sub_capacity; ++slot) {
                    size_t global_idx = row_start + slot;
                    if (global_idx < sender_db.size()) {
                        const auto& db_entry = sender_db[global_idx];
                        if (!db_entry.x_prime.empty() && db_entry.x_prime == y_prime) {
                            std::cout << "    找到! 实际位置: 子桶" << h << ", slot=" << slot << std::endl;
                            found_in_bucket = true;
                        }
                    }
                }
            }
            
            if (!found_in_bucket) {
                std::cout << "    未在整个主桶中找到该元素!" << std::endl;
            }
        }
    }
    
    // std::cout << "命中: " << hit_count << "/" << verify_count << std::endl;
    // std::cout << "未命中: " << miss_count << "/" << verify_count << std::endl;
    
    if (miss_count > 0) {
        std::cerr << "\n❌ 错误: 双边映射验证阶段存在 " << miss_count << " 个未命中元素!" << std::endl;
        std::cerr << "映射一致性验证失败，程序终止。" << std::endl;
        exit(1);
    } else {
        std::cout << "[双边映射正确性验证] ✓ 所有测试元素都正确映射，验证通过!" << std::endl;
        std::cout << std::endl;
    }   
}
