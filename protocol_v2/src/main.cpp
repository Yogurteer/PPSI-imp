#include "sender.h"
#include "receiver.h"
#include "config.h"
#include "oos_ot.h"
#include "network.h"
#include "common.h"
#include "../../PIRANA/src/client.h"
#include "../../PIRANA/src/server.h"
#include "../../PIRANA/src/pir_parms.h"
#include "../../PIRANA/src/hmain.h"
// #include "../thirdparty/oos_OT/oos_OT.h"
#include "common.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <getopt.h> // 用于命令行参数解析
#include <memory>

using namespace std;
using namespace std::chrono;

// 全局文件路径配置 (这些可以保留为全局或也改为参数，暂且保留)
NetworkChannel network_channel;

// Batch PIR 接口更新
std::vector<Element> run_batch_pir(
    const std::vector<Element>& database_items,
    const std::vector<uint32_t>& query_indices,
    size_t num_rows,
    size_t row_size,
    size_t payload_size,
    double &online_time,
    std::string batch_PIR_mode, 
    size_t& com_bytes) {
    
    std::vector<Element> results;
    
    uint64_t num_payloads = database_items.size();
    uint64_t num_query = query_indices.size();
    
    bool is_batch = true;
    bool is_compress = false; 
    
    // 目的是获取正确的 plain_modulus_bit 和 num_payload_slot
    // num_query 对应 PIR 矩阵的行数 (子桶数)
    // row_size 对应 PIR 矩阵的列数 (子桶容量)
    std::unique_ptr<PirParms> temp_pir_parms;
    if (batch_PIR_mode == "direct") {
        std::cout << "构造 Direct Mode PirParms (用于预处理参数计算)..." << std::endl;
        temp_pir_parms = std::make_unique<PirParms>(num_payloads, payload_size, num_query, row_size);
    }
    else if (batch_PIR_mode == "default") {
        std::cout << "构造 Default Mode PirParms (用于预处理参数计算)..." << std::endl;
        temp_pir_parms = std::make_unique<PirParms>(num_payloads, payload_size, num_query, is_batch, is_compress);
    } else {
        throw std::runtime_error("未知的 Batch PIR 模式: " + batch_PIR_mode);
    }

    auto plain_modulus_bit = temp_pir_parms->get_seal_parms().plain_modulus().bit_count();
    auto expected_num_payload_slot = temp_pir_parms->get_num_payload_slot();
    
    // std::cout << "\n[数据预处理] payload_size=" << payload_size << " bytes" << std::endl;
    // std::cout << "[数据预处理] plain_modulus_bit=" << plain_modulus_bit << std::endl;
    // std::cout << "[数据预处理] 每个payload需要 " << expected_num_payload_slot << " slots" << std::endl;
    // std::cout << "[数据预处理] 每个slot可存储 " << (plain_modulus_bit - 1) << " bits" << std::endl;

    // 将 Element (字节向量) 转换为 uint64_t 向量
    std::vector<std::vector<uint64_t>> input_db(num_payloads);
    
    // prepare input_db via temp_pir_parms
    for (size_t i = 0; i < num_payloads; ++i) {
        const auto& elem = database_items[i];
        
        if (elem.size() != payload_size) {
            std::cerr << "错误: database_items[" << i << "].size()=" << elem.size() 
                      << " 不等于 payload_size=" << payload_size << std::endl;
            throw std::runtime_error("Payload size mismatch in database_items");
        }
        
        input_db[i].resize(expected_num_payload_slot, 0);
        
        size_t bits_per_slot = plain_modulus_bit - 1;
        size_t bit_offset = 0;
        
        for (size_t slot_idx = 0; slot_idx < expected_num_payload_slot; slot_idx++) {
            uint64_t slot_value = 0;
            size_t bits_in_this_slot = std::min(bits_per_slot, payload_size * 8 - bit_offset);
            
            for (size_t bit = 0; bit < bits_in_this_slot; bit++) {
                size_t byte_idx = (bit_offset + bit) / 8;
                size_t bit_in_byte = (bit_offset + bit) % 8;
                
                if (byte_idx < elem.size()) {
                    uint64_t bit_value = (elem[byte_idx] >> bit_in_byte) & 1;
                    slot_value |= (bit_value << bit);
                }
            }
            
            if (slot_value == 0) {
                slot_value = 8888;
            }
            
            input_db[i][slot_idx] = slot_value;
            bit_offset += bits_per_slot;
        }
    }
    
    // std::cout << "[数据预处理] 完成，转换了 " << num_payloads << " 个payloads" << std::endl;

    if(batch_PIR_mode == "direct")
    {
        results = my_direct_batch_pir_main(num_payloads, payload_size, num_query, is_batch,
                       is_compress, input_db, query_indices, row_size, online_time, com_bytes);
    }
    else // default mode
    {
        results = my_batch_pir_main(num_payloads, payload_size, num_query, is_batch,
                       is_compress, input_db, query_indices, online_time, com_bytes);
    }
                   
    return results;
}

// Phase 0: 初始化测试数据 (从文件读取)
void phase0_initialize_data(
    LPSISender& sender,
    LPSIReceiver& receiver,
    std::vector<std::pair<std::string, std::string>>& sender_raw_data,
    std::vector<std::string>& receiver_raw_data,
    size_t sender_size,      
    size_t receiver_size,
    const std::string& dataset_path // 新增参数
) {
    auto string_to_element = [](const std::string& str) -> Element {
        return Element(str.begin(), str.end());
    };

    std::ifstream file(dataset_path);
    if (!file.is_open()) {
        std::cerr << "Fatal Error: 无法打开数据集文件 " << dataset_path << std::endl;
        exit(1);
    }
    
    std::string line;
    
    // 1. 读取 Sender Header (跳过)
    // 格式: "db size {sz} label bytes {bc} item bytes {bc}"
    if (!std::getline(file, line)) {
        std::cerr << "Error: 文件格式错误 (缺少 Sender Header)" << std::endl;
        exit(1);
    }
    
    // 2. 读取 Sender 数据
    sender_raw_data.clear();
    sender_raw_data.reserve(sender_size);
    std::vector<std::pair<Element, Element>> sender_data_elements;
    sender_data_elements.reserve(sender_size);
    
    for (size_t i = 0; i < sender_size; ++i) {
        if (!std::getline(file, line)) {
            std::cerr << "Error: Sender数据不足，期望 " << sender_size << " 行，在第 " << i << " 行中断。" << std::endl;
            exit(1);
        }
        
        // 格式: item,label
        std::string item, label;
        size_t comma_pos = line.find(',');
        if (comma_pos != std::string::npos) {
            item = line.substr(0, comma_pos);
            label = line.substr(comma_pos + 1);
        } else {
            // 如果没有逗号，可能没有 label 或者格式不对，视整行为 item
            item = line;
            label = ""; 
        }
        
        // 移除可能的 Windows 回车符
        if (!item.empty() && item.back() == '\r') item.pop_back();
        if (!label.empty() && label.back() == '\r') label.pop_back();

        sender_raw_data.push_back({item, label});
        sender_data_elements.push_back({
            string_to_element(item),
            Element(label.begin(), label.end())
        });
    }
    
    // 3. 读取 Receiver Header (跳过)
    // 格式: "query size {sz} intersection size {sz} item bytes {bc}"
    if (!std::getline(file, line)) {
        std::cerr << "Error: 文件格式错误 (缺少 Receiver Header)" << std::endl;
        exit(1);
    }
    
    // 4. 读取 Receiver 数据
    receiver_raw_data.clear();
    receiver_raw_data.reserve(receiver_size);
    ElementVector receiver_data_elements;
    receiver_data_elements.reserve(receiver_size);
    
    for (size_t i = 0; i < receiver_size; ++i) {
        if (!std::getline(file, line)) {
            std::cerr << "Error: Receiver数据不足，期望 " << receiver_size << " 行，在第 " << i << " 行中断。" << std::endl;
            exit(1);
        }
        
        std::string item = line;
        if (!item.empty() && item.back() == '\r') item.pop_back(); // 去除 \r
        
        receiver_raw_data.push_back(item);
        receiver_data_elements.push_back(string_to_element(item));
    }
    
    file.close();
    
    // 注入数据到协议实体
    sender.set_input(sender_data_elements);
    receiver.set_input(receiver_data_elements);
}

// Phase 1: DH-OPRF + PRP
void phase1_oprf_prp(LPSISender& sender, LPSIReceiver& receiver, double& oprf_online_time, double& oprf_offline_time, size_t& com_bytes) {
    // std::cout << "\n[Phase 1] DH-OPRF + PRP" << std::endl;
    MyTimer timer;
    oprf_offline_time = 0.0;

    // Step 1: Receiver -> Sender，默认离线阶段完成
    timer.reset(); // 开始在线请求时间计时
    ElementVector step1_output = receiver.compute_oprf_step1();
    oprf_offline_time += timer.elapsed();

    // Step 2: Sender -> Receiver (Sender进行PRP打乱,但不告诉Receiver映射关系)
    timer.reset(); // 开始在线响应时间计时
    std::vector<size_t> shuffle_map;  // Sender内部使用,不发送给Receiver
    ElementVector step2_output = sender.process_oprf_step2(step1_output, &shuffle_map);

    // Step 3: Receiver计算Y' (不知道PRP映射)
    receiver.process_oprf_step3(step2_output);  // 不传递 shuffle_map
    auto oprf_time = timer.elapsed();
    oprf_online_time = oprf_time;
    
    size_t step1_com_bytes = get_payload_size(step1_output);
    size_t step2_com_bytes = get_payload_size(step2_output);
    com_bytes = step1_com_bytes + step2_com_bytes;

    // // 调试: Receiver获取sender的r_s和shuffle_map进行对照验证
    // receiver.verify_oprf_correctness(sender.get_r_s(), shuffle_map, step2_output);
    
    timer.reset();
    // Sender计算X'
    sender.compute_X_prime();
    auto compute_X_prf = timer.elapsed();
    oprf_offline_time += compute_X_prf;
    // std::cout << "Compute X prf time: " << compute_X_prf << " ms " << std::endl;
}

// Phase 2: 构建哈希桶
void phase2_build_hash_buckets(
    LPSISender& sender,
    LPSIReceiver& receiver,
    size_t sender_data_size,
    size_t receiver_data_size,
    double& receiver_gen_idx_time,
    double& sender_gen_idx_time) {
    
    MyTimer timer;
    
    // 打印外层主桶膨胀系数和内层子桶总膨胀系数
    cout << "外层主桶膨胀系数: " << LPSIConfig::MAIN_BUCKET_FACTOR << std::endl;
    cout << "内层子桶总膨胀系数: " << LPSIConfig::NUM_SUB_BUCKETS * LPSIConfig::SUB_BUCKET_FACTOR << std::endl;

    // 计算主桶数量
    size_t num_main_buckets = static_cast<size_t>(LPSIConfig::MAIN_BUCKET_FACTOR * receiver_data_size);
    
    // 构建数据库索引 - 使用配置文件中的倍数参数
    timer.reset();
    sender.build_hash_buckets(num_main_buckets, LPSIConfig::OUTER_NUM_HASH_FUNCTIONS);
    sender.build_sub_buckets(sender_data_size, num_main_buckets, LPSIConfig::NUM_SUB_BUCKETS);
    sender_gen_idx_time = timer.elapsed();
    
    timer.reset();
    // 生成查询索引
    receiver.build_hash_buckets(num_main_buckets, LPSIConfig::OUTER_NUM_HASH_FUNCTIONS);
    receiver.build_sub_buckets(sender_data_size, num_main_buckets, LPSIConfig::NUM_SUB_BUCKETS);
    receiver_gen_idx_time = timer.elapsed();
    
    // std::cout << "✓ 哈希桶构建完成" << std::endl;
}

// Phase 3: 准备PIR数据库和查询
void phase3_prepare_pir(
    LPSISender& sender,
    LPSIReceiver& receiver,
    std::vector<Element>& database,
    std::vector<uint32_t>& queries,
    size_t& pir_db_size,
    size_t& item_size,
    double& offline_pir_prep_time) {
    
    // std::cout << "\n[Phase 3] 准备PIR数据库和查询" << std::endl;
    
    // Sender准备PIR数据库
    MyTimer timer;
    timer.reset();
    sender.prepare_pir_database(); //内部输出pirdb膨胀率等信息
    offline_pir_prep_time = timer.elapsed();
    
    // Sender发送桶结构信息给Receiver
    size_t sender_num_main_buckets = sender.get_num_main_buckets();
    auto [sender_nh, sender_sub_capacity] = sender.get_sub_bucket_structure();
    
    // Receiver生成查询索引
    receiver.generate_pir_query_indices(sender_num_main_buckets, sender_nh, sender_sub_capacity);
    
    // 获取数据库和查询
    size_t num_items;
    database = sender.get_pir_database_as_bytes(num_items, item_size);
    queries = receiver.get_query_indices_flat();
    
    // ===== 添加Phase2/3验证 =====
    // std::cout << "\n[双边映射正确性验证] 检查双层hash映射正确性..." << std::endl;
    // receiver.verify_phase23_mapping(sender.get_flattened_database(),
    //                                  sender_num_main_buckets,
    //                                  sender_nh,
    //                                  sender_sub_capacity);
}

// Phase 4: 执行PIR查询
void phase4_execute_pir(
    LPSIReceiver& receiver,
    LPSISender& sender,
    const std::vector<Element>& database,
    const std::vector<uint32_t>& queries,
    size_t item_size,
    double &online_time,
    std::string batch_PIR_mode, 
    size_t& com_bytes) {
    
    // 获取数据库的行列结构
    size_t sender_num_main_buckets = sender.get_num_main_buckets();
    auto [sender_nh, sender_sub_capacity] = sender.get_sub_bucket_structure();
    size_t num_rows = sender_num_main_buckets * sender_nh;  // 总行数 = 主桶数 × 子桶数
    size_t row_size = sender_sub_capacity;  // 每行大小 = 子桶容量

    std::cout << "---------------Call Batch PIR-----------------------" << std::endl;
    
    std::vector<Element> pir_results = run_batch_pir(database, queries, num_rows, row_size, item_size, online_time, batch_PIR_mode, com_bytes);

    receiver.pir_results = pir_results;
    
}

void simulate_pir(
    LPSIReceiver& receiver,
    LPSISender& sender,
    const std::vector<Element>& database,
    const std::vector<uint32_t>& queries,
    size_t item_size) {
    
    // 获取数据库的行列结构
    size_t sender_num_main_buckets = sender.get_num_main_buckets();
    auto [sender_nh, sender_sub_capacity] = sender.get_sub_bucket_structure();
    size_t num_rows = sender_num_main_buckets * sender_nh;  // 总行数 = 主桶数 × 子桶数
    size_t row_size = sender_sub_capacity;  // 每行大小 = 子桶容量
    
    for(size_t i = 0; i < queries.size(); ++i) {
        uint32_t query_idx = queries[i];
        // **关键修复**: 检查索引是否超出实际数据范围
        size_t actual_db_size = num_rows * row_size;
        if (query_idx >= actual_db_size) {
            // 返回全0的payload（表示未找到）
            std::vector<unsigned char> empty(item_size, 0);
            receiver.sim_pir_results.push_back(empty);
        } else {
            receiver.sim_pir_results.push_back(database[query_idx]);
        }
    }
}

// Phase 5: OT获取桶密钥
void phase5_ot_bucket_keys(
    LPSISender& sender,
    LPSIReceiver& receiver,
    std::vector<Element>& bucket_keys,
    size_t len_byte_r, size_t& com_bytes,
    double& ot_online_time,
    double& ot_offline_time) {
    
    std::cout << "\n---------------Call OOS OT-----------------------" << std::endl;
    
    // 方式0: 明文模拟 (用于测试对比)
    // std::vector<size_t> requested_buckets = receiver.get_valid_bucket_indices();
    // bucket_keys = sender.send_bucket_keys_plaintext(requested_buckets);

    // 方式1: 调用test oos_OT
    // oos_OT_test();
    
    // 方式2: 真实OT协议
    std::vector<size_t> receiver_choices = receiver.get_ot_choices();
    
    if (receiver_choices.empty()) {
        std::cout << "警告: Receiver没有命中任何元素,跳过OT" << std::endl;
        throw std::runtime_error("Receiver OT choices are empty");  
        return;
    }
    
    // Sender准备OT输入，获取receiver请求的桶数量，作为sender最终output
    if (!sender.prepare_ot_inputs(receiver_choices.size())) {
            std::cerr << "Sender OT桶密钥未初始化" << std::endl;
            return;
        }
    
    // 获取Sender的OT输入 (所有桶密钥作为选项) ot_inputs[0] = bucket_keys
    std::vector<std::vector<Element>> sender_ot_base = sender.get_ot_inputs();
    std::cout << "Sender: own " << sender_ot_base[0].size() << " items" << std::endl;
    
    if (sender_ot_base.empty() || sender_ot_base[0].empty()) {
        std::cerr << "Sender OT input empty" << std::endl;
        return;
    }
    
    // 计算input_bit_count: 需要足够的比特数表示所有桶索引
    size_t num_buckets = sender.get_num_main_buckets();
    uint32_t input_bit_count = 0;
    while ((1ULL << input_bit_count) < num_buckets) {
        input_bit_count++;
    }
    
    size_t N = 1ULL << input_bit_count;  // OT选择数N需要从num_buckets扩展到最近2的幂次
    
    // 将桶密钥扩展到N个 (填充空密钥)
    // OT现在支持OT_DATA_SIZE字节的传输(默认32字节)
    std::vector<Element> padded_keys = sender_ot_base[0];
    
    while (padded_keys.size() < N) {
        padded_keys.push_back(Element(len_byte_r, 0));  // 填充空密钥
    }
    
    // 构建完整的OT输入: k个实例,每个OT实例都提供N个选择
    // 安全性考虑,在此处receiver告知了sender请求桶密钥的数量
    size_t num_ot_instances = receiver_choices.size();
    std::vector<std::vector<Element>> sender_ot_inputs(num_ot_instances, padded_keys);
    
    // 执行OT协议
    std::vector<Element> ot_outputs;
    double ot_offline_time_ms = 0.0;
    double ot_online_time_ms = 0.0;
    bool ot_success = run_oos_ot(sender_ot_inputs, receiver_choices, ot_outputs, com_bytes,
                                  input_bit_count, true, &ot_offline_time_ms, &ot_online_time_ms);
    ot_online_time = ot_online_time_ms;
    ot_offline_time = ot_offline_time_ms;
    
    if (!ot_success) {
        std::cerr << "OT执行失败" << std::endl;
        return;
    }
    
    std::cout << "OT Offline时间: " << ot_offline_time_ms << " ms" << std::endl;
    std::cout << "OT Online时间: " << ot_online_time_ms << " ms" << std::endl;
    
    // 将OT输出作为桶密钥
    bucket_keys = ot_outputs;
}

// Phase 6 (需传入 RECEIVER_SIZE 进行验证)
void phase6_decrypt_and_verify(
    LPSISender& sender, 
    LPSIReceiver& receiver, 
    const std::vector<Element>& bucket_keys, 
    const std::vector<std::pair<std::string, std::string>>& sender_raw_data, 
    const std::vector<std::string>& receiver_raw_data,
    size_t receiver_size // 【修改】传入 size 用于验证
    ) {
    
    receiver.decrypt_intersection(bucket_keys);
    const auto& intersection = receiver.get_intersection();
    

    std::cout << "========== Protocol Output ==========" << std::endl;

    std::cout << "[Sender]get inter size: " << sender.get_intersection_size() << std::endl;
    std::cout << "[Receiver]get inter size&data: " << intersection.size() << std::endl;
    
    size_t valid_count = 0;
    auto element_to_string = [](const Element& elem) -> std::string { return std::string(elem.begin(), elem.end()); };
    for (size_t i = 0; i < intersection.size(); ++i) {
        std::string x_str = element_to_string(intersection[i].first);
        std::string v_str = element_to_string(intersection[i].second);
        bool found_in_sender = false;
        bool key_in_receiver = false;
        for (const auto& kv : sender_raw_data) {
            if (x_str == kv.first && v_str == kv.second) { found_in_sender = true; break; }
        }
        for (const auto& key : receiver_raw_data) {
            if (x_str == key) { key_in_receiver = true; break; }
        }
        if (found_in_sender && key_in_receiver) valid_count++;
    }
    
    std::cout << "[Intersection Data Verification] " << valid_count << "/" << intersection.size() << " correct"<< std::endl;

    // if (intersection.size() == receiver_size && sender.get_intersection_size() == intersection.size() && sender.get_intersection_size() != 0) {
    //     std::cout << "\n[Final Correctness Verification]✓ Protocol executed correctly" << std::endl;
    // } else {
    //     std::cout << "\n[Final Correctness Verification]✗ Protocol execution error" << std::endl;
    // }
}

int run_main(size_t sender_size, size_t receiver_size, size_t inter_size, size_t payload_size, std::string batch_PIR_mode, std::string dataset_path) {    std::cout << "========================================" << std::endl;
    std::cout << "   Payable LPSI协议 (动态参数分段计时)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Mode: " << batch_PIR_mode << std::endl;

    // 创建实例
    LPSISender sender;
    LPSIReceiver receiver;
    
    std::vector<std::pair<std::string, std::string>> sender_raw_data;
    std::vector<std::string> receiver_raw_data;

    // [1] Initialization
    std::cout << "\n[1] 正在执行: 导入数据和初始化..." << std::endl;
    auto t0_start = high_resolution_clock::now();
    
    // 【修改】传入 size
    phase0_initialize_data(sender, receiver, sender_raw_data, receiver_raw_data, sender_size, receiver_size, dataset_path);
    std::cout << "[数据导入] Sender数据项数: " << sender_size << std::endl;
    std::cout << "[数据导入] Receiver数据项数: " << receiver_size << std::endl;
    std::cout << "[数据导入] Payload size: " << payload_size << " 字节" << std::endl;
    
    auto t0_end = high_resolution_clock::now();
    auto dur_init = duration_cast<milliseconds>(t0_end - t0_start).count();

    // [2] DH-OPRF
    std::cout << "\n[2] 正在执行: DH-OPRF..." << std::endl;
    double receiver_oprf_time = 0.0;
    double oprf_offline_time = 0.0;
    size_t p1_com_bytes = 0;
    phase1_oprf_prp(sender, receiver, receiver_oprf_time, oprf_offline_time, p1_com_bytes);
    auto dur_oprf_online = receiver_oprf_time;
    auto dur_oprf_offline = oprf_offline_time;
    cout << "[OPRF] 通信开销: " << p1_com_bytes << " Bytes" << std::endl;

    // [3] Hash Buckets
    std::cout << "\n[3] 正在执行: 构造双层Hash..." << std::endl;
    double receiver_gen_idx_time = 0.0;
    double sender_gen_idx_time = 0.0;
    
    // 填充receiver_raw_data到N的整数倍
    size_t original_receiver_size = receiver_raw_data.size();
    PpsiParm gloal_parm;
    size_t N = gloal_parm.poly_degree;
    size_t ratio = (original_receiver_size + N - 1) / N; // 向上取整
    size_t pad_receiver_size = ratio * N;
    phase2_build_hash_buckets(sender, receiver, sender_raw_data.size(), receiver_raw_data.size(), receiver_gen_idx_time, sender_gen_idx_time);
    // phase2_build_hash_buckets(sender, receiver, sender_raw_data.size(), pad_receiver_size, receiver_gen_idx_time);
    auto online_receiver_genidx_time = receiver_gen_idx_time;
    auto offline_sender_genidx_time = sender_gen_idx_time;

    // ==========================================
    // 4. Batch PIR (Prepare + Execute)
    // ==========================================
    std::cout << "\n[4] 正在执行: Batch PIR (Prepare + Execute)..." << std::endl;
    
    std::vector<Element> database;
    std::vector<uint32_t> queries;
    size_t pir_db_size, item_size;
    double offline_pir_prep_time = 0.0;
    double online_pir_process_time = 0.0;

    // 准备PIR (DB & Query Generation)
    phase3_prepare_pir(sender, receiver, database, queries, pir_db_size, item_size, offline_pir_prep_time);

    // 执行PIR (Cryptographic operations)
    size_t p4_com_bytes = 0;
    phase4_execute_pir(receiver, sender, database, queries, item_size, online_pir_process_time, batch_PIR_mode, p4_com_bytes);
    cout << "[Batch PIR] 通信开销: " << p4_com_bytes << " Bytes" << std::endl;
    
    // // --- 模拟PIR与验证 (不计入时间) ---
    // simulate_pir(receiver, sender, database, queries, item_size);
    // bool pir_match = true;
    // if (receiver.sim_pir_results.size() != receiver.pir_results.size()) pir_match = false;
    // else {
    //     for (size_t i = 0; i < receiver.sim_pir_results.size(); ++i) {
    //         if (receiver.sim_pir_results[i] != receiver.pir_results[i]) {
    //             pir_match = false; break;
    //         }
    //     }
    // }
    // // std::cout << "[PIR外部验证] 模拟与实际结果匹配: " << (pir_match ? "YES" : "NO") << std::endl;
    
    // recevier处理PIR结果兼容格式
    // receiver.process_pir_results(receiver.sim_pir_results);
    receiver.process_pir_results(receiver.pir_results);

    // [5] OT
    std::cout << "\n[5] 正在执行: OT..." << std::endl;
    auto t4_start = high_resolution_clock::now();
    std::vector<Element> bucket_keys;
    size_t len_byte_r = LPSIConfig::BUCKET_KEY_SIZE_BYTES;
    size_t p5_com_bytes = 0;

    double ot_online_time = 0.0;
    double ot_offline_time = 0.0;
    phase5_ot_bucket_keys(sender, receiver, bucket_keys, len_byte_r, p5_com_bytes, ot_online_time, ot_offline_time);
    
    auto t4_end = high_resolution_clock::now();
    // auto dur_ot = duration_cast<milliseconds>(t4_end - t4_start).count();
    auto dur_ot = ot_online_time;
    cout << "[OT] 通信开销: " << p5_com_bytes << " Bytes" << std::endl;

    // [6] Decrypt
    std::cout << "\n[6] 正在执行: 解密验证..." << std::endl;
    auto t5_start = high_resolution_clock::now();
    // 【修改】传入 size
    phase6_decrypt_and_verify(sender, receiver, bucket_keys, sender_raw_data, receiver_raw_data, receiver_size);
    auto t5_end = high_resolution_clock::now();
    auto dur_decrypt = duration_cast<milliseconds>(t5_end - t5_start).count();

    // ==========================================
    // 汇总输出 - 重新整理
    // ==========================================
    
    // 计算各阶段总时间
    double total_offline_time = dur_oprf_offline + offline_sender_genidx_time + offline_pir_prep_time + ot_offline_time;
    double total_online_time = dur_oprf_online + online_receiver_genidx_time + online_pir_process_time + ot_online_time + dur_decrypt;
    double total_protocol_time = total_offline_time + total_online_time;
    
    // 定义列宽常量
    const int LABEL_WIDTH = 40;
    const int TIME_WIDTH  = 12;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "        协议执行性能统计" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 设置统一的小数格式：固定小数点，保留2位小数
    std::cout << std::fixed << std::setprecision(2);
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << "0. 数据导入 (Init):" 
              << std::right << std::setw(TIME_WIDTH) << (double)dur_init << " ms" << std::endl;
    
    std::cout << "\n----------------------------------------" << std::endl;
    std::cout << "           离线阶段 (Offline)" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << "1. OPRF (offline):" 
              << std::right << std::setw(TIME_WIDTH) << dur_oprf_offline << " ms" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << "2. 构造索引-Sender (offline):" 
              << std::right << std::setw(TIME_WIDTH) << offline_sender_genidx_time << " ms" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << "3. PIR准备-Sender (offline):" 
              << std::right << std::setw(TIME_WIDTH) << offline_pir_prep_time << " ms" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << "4. OT Base (offline):" 
              << std::right << std::setw(TIME_WIDTH) << ot_offline_time << " ms" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << ">>> 离线阶段总计:" 
              << std::right << std::setw(TIME_WIDTH) << total_offline_time << " ms" << std::endl;
    
    std::cout << "\n----------------------------------------" << std::endl;
    std::cout << "           在线阶段 (Online)" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << "1. OPRF (online):" 
              << std::right << std::setw(TIME_WIDTH) << dur_oprf_online << " ms" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << "2. 构造索引-Receiver (online):" 
              << std::right << std::setw(TIME_WIDTH) << online_receiver_genidx_time << " ms" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << "3. PIR查询 (online):" 
              << std::right << std::setw(TIME_WIDTH) << online_pir_process_time << " ms" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << "4. OT Extension (online):" 
              << std::right << std::setw(TIME_WIDTH) << ot_online_time << " ms" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << "5. 解密验证 (online):" 
              << std::right << std::setw(TIME_WIDTH) << (double)dur_decrypt << " ms" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << ">>> 在线阶段总计:" 
              << std::right << std::setw(TIME_WIDTH) << total_online_time << " ms" << std::endl;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << std::left << std::setw(LABEL_WIDTH) << "总协议执行时间:" 
              << std::right << std::setw(TIME_WIDTH) << total_protocol_time << " ms" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 输出通信开销统计
    size_t total_com_bytes = p1_com_bytes + p4_com_bytes + p5_com_bytes;
    std::cout << "\n========================================" << std::endl;
    std::cout << "        协议通信开销统计" << std::endl; 
    std::cout << "========================================" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << "1. OPRF 通信:" 
              << std::right << std::setw(TIME_WIDTH) << p1_com_bytes << " Bytes" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << "2. Batch PIR 通信:" 
              << std::right << std::setw(TIME_WIDTH) << p4_com_bytes << " Bytes" << std::endl;
    
    std::cout << std::left << std::setw(LABEL_WIDTH) << "3. OT 通信:" 
              << std::right << std::setw(TIME_WIDTH) << p5_com_bytes << " Bytes" << std::endl;
    
    std::cout << "----------------------------------------" << std::endl;
    std::cout << std::left << std::setw(LABEL_WIDTH) << "总通信开销:" 
              << std::right << std::setw(TIME_WIDTH) << total_com_bytes << " Bytes" << std::endl;
    std::cout << std::left << std::setw(LABEL_WIDTH) << "" 
              << std::right << std::setw(TIME_WIDTH) << std::setprecision(2) << (double)total_com_bytes / 1024.0 << " KB" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 格式化汇总输出 (单位: 秒)
    std::cout << "\n========== Overall Result (s/MB) ==========" << std::endl;
    std::cout << "Dataset: Sender=" << sender_size << " Receiver=" << receiver_size << " Payload=" << payload_size << " bytes" << std::endl;
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Total_online: " << total_online_time / 1000.0 << " s" << std::endl;
    std::cout << "Total_offline: " << total_offline_time / 1000.0 << " s" << std::endl;
    std::cout << "Total_time: " << total_protocol_time / 1000.0 << " s" << std::endl;
    std::cout << "Breakdown_offline: OPRF=" << dur_oprf_offline / 1000.0 
              << " Gen_idx_sender=" << offline_sender_genidx_time / 1000.0 
              << " PIR_prep=" << offline_pir_prep_time / 1000.0 
              << " OT_base=" << ot_offline_time / 1000.0 << std::endl;
    std::cout << "Breakdown_online: OPRF=" << dur_oprf_online / 1000.0 
              << " Gen_idx_receiver=" << online_receiver_genidx_time / 1000.0 
              << " PIR_query=" << online_pir_process_time / 1000.0 
              << " OT_ext=" << ot_online_time / 1000.0 
              << " Dec=" << (double)dur_decrypt / 1000.0 << std::endl;
    std::cout << "Communication: " << total_com_bytes << " Bytes (" << (double)total_com_bytes / 1024.0 << " KB, " << (double)total_com_bytes / (1024.0 * 1024.0) << " MB)" << std::endl;
    std::cout << "Com(MB)" << ": " << (double)total_com_bytes / (1024.0 * 1024.0) << std::endl;
    std::cout << "================================================" << std::endl;

    // CSV格式输出：Sender,Receiver,Payload,sum_online,oprf_on,gen_idx_on,query_on,ot_on,dec_on,sum_offline,oprf_off,gen_idx_off,pir_off,ot_off,com
    std::cout << "\n========== CSV Format Output ==========" << std::endl;
    std::cout << "S_size,R_size,I_size,Payload_bytes,Total_on_time,OPRF_on_s,Genidx_r,PIR_query,OT_online,Decrypt,Total_off_time,OPRF_off,Gen_idx_s,PIR_prep,OT_off,Total_com_MB" << std::endl;
    std::cout << sender_size << ","
              << receiver_size << ","
              << inter_size << ","
              << payload_size << ","
              << std::fixed << std::setprecision(3)
              << total_online_time / 1000.0 << ","
              << dur_oprf_online / 1000.0 << ","
              << online_receiver_genidx_time / 1000.0 << ","
              << online_pir_process_time / 1000.0 << ","
              << ot_online_time / 1000.0 << ","
              << (double)dur_decrypt / 1000.0 << ","
              << total_offline_time / 1000.0 << ","
              << dur_oprf_offline / 1000.0 << ","
              << offline_sender_genidx_time / 1000.0 << ","
              << offline_pir_prep_time / 1000.0 << ","
              << ot_offline_time / 1000.0 << ","
              << (double)total_com_bytes / (1024.0 * 1024.0) << std::endl;
    std::cout << "=======================================" << std::endl;
    
    
    return 0;
}

// ==========================================
// Main Entry: Argument Parsing (eg. ./lpsi_test -x 4096 -y 256 -p 1)
// ==========================================
int main(int argc, char** argv) {
    // 默认参数
    size_t sender_size = 4096;   // 默认 2^12
    size_t receiver_size = 1024;  // 默认 2^8
    size_t payload_size = 1;     // 默认 1 byte
    size_t is_default_mode = 1; // 默认使用 Default Mode
    size_t inter_size = 0;
    std::string dataset_path = "../data/dataset_default.csv"; // 默认路径
    
    int opt;
    while ((opt = getopt(argc, argv, "x:y:i:p:m:f:h")) != -1) {
        switch (opt) {
            case 'x':
                sender_size = std::stoul(optarg);
                break;
            case 'y':
                receiver_size = std::stoul(optarg);
                break;
            case 'i':
                inter_size = std::stoul(optarg);
                break;
            case 'p':
                payload_size = std::stoul(optarg);

                LPSIConfig::PIR_PAYLOAD_SIZE = 128; // label byte length
                break;
            case 'f': dataset_path = optarg; break; // 读取文件路径
            case 'm':
                // 1-default mode, 0-direct mode
                is_default_mode = std::stoul(optarg);
                break;
            case 'h':
                std::cout << "Usage: " << argv[0] << " [options]\n"
                          << "Options:\n"
                          << "  -x <size>   Sender data size (default: 4096)\n"
                          << "  -y <size>   Receiver data size (default: 256)\n"
                          << "  -p <bytes>  Payload size in bytes (default: 1)\n"
                          << "  -m <mode>   Batch PIR mode: 1-default, 0-direct (default: 1)\n"
                          << "  -h          Show this help message\n";
                return 0;
            default:
                std::cerr << "Unknown option. Use -h for help." << std::endl;
                return 1;
        }
    }

    // 运行主逻辑
    std::string batch_PIR_mode = (is_default_mode == 1 || receiver_size <= 1024) ? "default" : "direct";
    run_main(sender_size, receiver_size, inter_size, payload_size, batch_PIR_mode, dataset_path);
    
    return 0;
}