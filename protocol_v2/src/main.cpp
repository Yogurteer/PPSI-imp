#include "sender.h"
#include "receiver.h"
#include "config.h"
#include "oos_ot.h"
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
const string DATA_FILE = "../data/kv_2_24.txt";
const string result_file = "../result/PPSI_dif_size.txt";

// 辅助函数: 从PIR answer提取payload
std::vector<uint64_t> extract_payload_slots_from_answer(
    const std::vector<std::vector<uint64_t>>& answer,
    uint32_t index,
    PirParms& pir_parms) {
    
    auto N = pir_parms.get_seal_parms().poly_modulus_degree();
    auto half_N = N / 2;
    auto num_slot = pir_parms.get_num_payload_slot();
    
    std::vector<uint64_t> result;
    result.reserve(num_slot);
    
    uint32_t count = 0;
    uint64_t left_rotate_slot =
        std::ceil(static_cast<double>(num_slot) / pir_parms.get_pre_rotate());
    
    for (uint32_t i = 0; i < answer.size() && count < num_slot; i++) {
        auto row_index = index / half_N;
        auto offset =
            (index - std::min(left_rotate_slot, pir_parms.get_rotate_step())) %
            half_N;
        for (uint32_t j = 0; j < pir_parms.get_rotate_step() && count < num_slot; j++) {
            for (uint32_t k = 0; k < pir_parms.get_pre_rotate() / 2 && count < num_slot; k++) {
                uint32_t idx = row_index * half_N +
                    (k * pir_parms.get_rotate_step() + j + offset) % half_N;
                auto response_item = answer[i][idx % N];
                result.push_back(response_item);
                count++;
            }
            for (uint32_t k = 0; k < pir_parms.get_pre_rotate() / 2 && count < num_slot; k++) {
                uint32_t idx = (row_index + 1) * half_N +
                    (k * pir_parms.get_rotate_step() + j + offset) % half_N;
                auto response_item = answer[i][idx % N];
                result.push_back(response_item);
                count++;
            }
        }
        left_rotate_slot -= pir_parms.get_rotate_step();
    }
    
    return result;
}

// // Batch PIR
// std::vector<Element> run_batch_pir(
//     const std::vector<Element>& database_items,
//     const std::vector<uint32_t>& query_indices,
//     size_t num_rows,
//     size_t row_size,
//     size_t payload_size,
//     double &online_time) {
    
//     std::vector<Element> results;
    
//     // std::cout << "[PIR] 完成 " << results.size() << " 个查询" << std::endl;
//     uint64_t num_payloads = database_items.size();
//     uint64_t num_query = query_indices.size();
//     bool is_batch = true;
//     bool is_compress = false;
    
//     // 注意: my_batch_pir_main 的 payload_size 参数应该是字节数，不是 uint64_t 数量
//     // PirParms 会根据 payload_size(字节) 自动计算需要的 num_payload_slot
//     // 计算公式: num_payload_slot = ceil(payload_size * 8 / (plain_modulus_bit - 1))
    
//     // 首先需要创建 PirParms 来获取正确的 num_payload_slot
//     cout << "构造 PirParms ..." << endl;
//     PirParms temp_pir_parms(num_payloads, payload_size, num_query, is_batch, is_compress);
//     auto plain_modulus_bit = temp_pir_parms.get_seal_parms().plain_modulus().bit_count();
//     auto expected_num_payload_slot = temp_pir_parms.get_num_payload_slot();
    
//     std::cout << "\n[数据预处理] payload_size=" << payload_size << " bytes" << std::endl;
//     std::cout << "[数据预处理] plain_modulus_bit=" << plain_modulus_bit << std::endl;
//     std::cout << "[数据预处理] 每个payload需要 " << expected_num_payload_slot << " slots" << std::endl;
//     std::cout << "[数据预处理] 每个slot可存储 " << (plain_modulus_bit - 1) << " bits" << std::endl;
    
//     // 将 Element (字节向量) 转换为 uint64_t 向量
//     // 每个 payload 需要转换为 expected_num_payload_slot 个 uint64_t
//     std::vector<std::vector<uint64_t>> input_db(num_payloads);
    
//     for (size_t i = 0; i < num_payloads; ++i) {
//         const auto& elem = database_items[i];
        
//         if (elem.size() != payload_size) {
//             std::cerr << "错误: database_items[" << i << "].size()=" << elem.size() 
//                       << " 不等于 payload_size=" << payload_size << std::endl;
//             throw std::runtime_error("Payload size mismatch in database_items");
//         }
        
//         // 按照PIR的要求，将字节数据分割到多个slots中
//         // 每个slot存储 (plain_modulus_bit - 1) bits
//         input_db[i].resize(expected_num_payload_slot, 0);
        
//         size_t bits_per_slot = plain_modulus_bit - 1;
//         size_t bit_offset = 0;
        
//         for (size_t slot_idx = 0; slot_idx < expected_num_payload_slot; slot_idx++) {
//             uint64_t slot_value = 0;
//             size_t bits_in_this_slot = std::min(bits_per_slot, payload_size * 8 - bit_offset);
            
//             // 从字节数组中提取bits_in_this_slot个比特
//             for (size_t bit = 0; bit < bits_in_this_slot; bit++) {
//                 size_t byte_idx = (bit_offset + bit) / 8;
//                 size_t bit_in_byte = (bit_offset + bit) % 8;
                
//                 if (byte_idx < elem.size()) {
//                     uint64_t bit_value = (elem[byte_idx] >> bit_in_byte) & 1;
//                     slot_value |= (bit_value << bit);
//                 }
//             }
            
//             // 与PIR内部逻辑保持一致：将0值替换为8888
//             if (slot_value == 0) {
//                 slot_value = 8888;
//             }
            
//             input_db[i][slot_idx] = slot_value;
//             bit_offset += bits_per_slot;
//         }
//     }
    
//     std::cout << "[数据预处理] 完成，转换了 " << num_payloads << " 个payloads" << std::endl;

//     results = my_batch_pir_main(num_payloads, payload_size, num_query, is_batch,
//                    is_compress, input_db, query_indices, online_time);
//     return results;
// }

// Batch PIR 接口更新
std::vector<Element> run_batch_pir(
    const std::vector<Element>& database_items,
    const std::vector<uint32_t>& query_indices,
    size_t num_rows,
    size_t row_size,
    size_t payload_size,
    double &online_time,
    std::string batch_PIR_mode) {
    
    std::vector<Element> results;
    
    uint64_t num_payloads = database_items.size();
    uint64_t num_query = query_indices.size();
    
    // Direct Mode 默认开启压缩以提高效率
    bool is_batch = true;
    bool is_compress = false; 
    
    // 【关键修改 1】使用 Direct Mode 的构造函数初始化临时参数
    // 目的是获取正确的 plain_modulus_bit 和 num_payload_slot
    // num_query 对应 PIR 矩阵的行数 (子桶数)
    // row_size 对应 PIR 矩阵的列数 (子桶容量)
    std::unique_ptr<PirParms> temp_pir_parms;
    if (batch_PIR_mode == "direct") {
        is_compress = true; // Direct Mode 默认开启压缩
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
    
    std::cout << "\n[数据预处理] payload_size=" << payload_size << " bytes" << std::endl;
    std::cout << "[数据预处理] plain_modulus_bit=" << plain_modulus_bit << std::endl;
    std::cout << "[数据预处理] 每个payload需要 " << expected_num_payload_slot << " slots" << std::endl;
    std::cout << "[数据预处理] 每个slot可存储 " << (plain_modulus_bit - 1) << " bits" << std::endl;
    
    // --- 数据转换逻辑保持不变 ---
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
    
    std::cout << "[数据预处理] 完成，转换了 " << num_payloads << " 个payloads" << std::endl;

    // 【关键修改 2】调用新的 Direct Batch PIR 函数
    // 注意：你需要确保 batch_pir.cc 中的 my_direct_batch_pir_main 定义中增加了 col_size 参数
    // 这里的 row_size 就是子桶容量，对应 PIR 中的 col_size
    // results = my_direct_batch_pir_main(num_payloads, payload_size, num_query, is_batch,
    //                is_compress, input_db, query_indices, row_size, online_time);
    // 能正常运行的非紧凑Batch PIR调用 
    if(batch_PIR_mode == "direct")
    {
        results = my_direct_batch_pir_main(num_payloads, payload_size, num_query, is_batch,
                       is_compress, input_db, query_indices, row_size, online_time);
    }
    else // default mode
    {
        results = my_batch_pir_main(num_payloads, payload_size, num_query, is_batch,
                       is_compress, input_db, query_indices, online_time);
    }
                   
    return results;
}

// Phase 0: 初始化测试数据 (从文件读取)
void phase0_initialize_data(
    LPSISender& sender,
    LPSIReceiver& receiver,
    std::vector<std::pair<std::string, std::string>>& sender_raw_data,
    std::vector<std::string>& receiver_raw_data,
    size_t sender_size,      // 【修改】通过参数传入
    size_t receiver_size     // 【修改】通过参数传入
) {
    
    // 辅助函数保持不变
    auto string_to_element = [](const std::string& str) -> Element {
        return Element(str.begin(), str.end());
    };
    auto binary_string_to_bytes = [](const std::string& binary_str) -> std::vector<uint8_t> {
        std::vector<uint8_t> result;
        if (binary_str.length() == 8) {
            uint8_t high_nibble = 0;
            for (int i = 0; i < 4; ++i) if (binary_str[i] == '1') high_nibble |= (1 << (3 - i));
            uint8_t low_nibble = 0;
            for (int i = 4; i < 8; ++i) if (binary_str[i] == '1') low_nibble |= (1 << (7 - i));
            result.push_back(high_nibble);
            result.push_back(low_nibble);
        }
        return result;
    };
    
    string input_data_file = DATA_FILE;
    std::ifstream file(input_data_file);
    if (!file.is_open()) {
        std::cerr << "错误: 无法打开文件 " << input_data_file << std::endl;
        exit(1);
    }
    
    std::vector<std::pair<std::string, std::string>> all_data;
    std::string line;
    
    // 读取所有数据行 (直到满足 Sender 需求)
    while (std::getline(file, line) && all_data.size() < sender_size) {
        std::istringstream iss(line);
        std::string keyword, binary_payload;
        if (iss >> keyword >> binary_payload) {
            auto payload_bytes = binary_string_to_bytes(binary_payload);
            if (!payload_bytes.empty()) {
                std::string payload(payload_bytes.begin(), payload_bytes.end());
                all_data.push_back({keyword, payload});
            }
        }
    }
    file.close();
    
    if (all_data.size() < sender_size) {
        std::cerr << "错误: 文件数据不足! 需要 " << sender_size << " 行，实际只有 " 
                  << all_data.size() << " 行。" << std::endl;
        exit(1);
    }
    
    // 设置 Sender 数据
    sender_raw_data.reserve(sender_size);
    std::vector<std::pair<Element, Element>> sender_data;
    sender_data.reserve(sender_size);
    
    for (size_t i = 0; i < sender_size; ++i) {
        sender_raw_data.push_back(all_data[i]);
        sender_data.push_back({
            string_to_element(all_data[i].first),
            Element(all_data[i].second.begin(), all_data[i].second.end())
        });
    }
    
    // 设置 Receiver 数据 (随机选择)
    receiver_raw_data.reserve(receiver_size);
    ElementVector receiver_data;
    receiver_data.reserve(receiver_size);
    
    std::vector<size_t> indices(sender_size);
    for (size_t i = 0; i < sender_size; ++i) indices[i] = i;
    
    std::srand(42);
    std::random_shuffle(indices.begin(), indices.end());
    
    for (size_t i = 0; i < receiver_size; ++i) {
        size_t selected_idx = indices[i];
        receiver_raw_data.push_back(all_data[selected_idx].first);
        receiver_data.push_back(string_to_element(all_data[selected_idx].first));
    }
    
    sender.set_input(sender_data);
    receiver.set_input(receiver_data);
}

// Phase 1: DH-OPRF + PRP
void phase1_oprf_prp(LPSISender& sender, LPSIReceiver& receiver, double& receiver_oprf_time) {
    // std::cout << "\n[Phase 1] DH-OPRF + PRP" << std::endl;
    MyTimer timer;

    // Step 1: Receiver -> Sender，默认离线阶段完成
    ElementVector step1_output = receiver.compute_oprf_step1();

    timer.reset(); // 开始在线响应时间计时
    // Step 2: Sender -> Receiver (Sender进行PRP打乱,但不告诉Receiver映射关系)
    std::vector<size_t> shuffle_map;  // Sender内部使用,不发送给Receiver
    ElementVector step2_output = sender.process_oprf_step2(step1_output, &shuffle_map);
    // Step 3: Receiver计算Y' (不知道PRP映射)
    receiver.process_oprf_step3(step2_output);  // 不传递 shuffle_map
    auto oprf_time = timer.elapsed();
    receiver_oprf_time = oprf_time;

    // 调试: Receiver获取sender的r_s和shuffle_map进行对照验证
    receiver.verify_oprf_correctness(sender.get_r_s(), shuffle_map, step2_output);
    
    timer.reset();
    // Sender计算X'
    sender.compute_X_prime();
    auto compute_X_prf = timer.elapsed();
    std::cout << "Compute X prf time: " << compute_X_prf << " ms " << std::endl;
}

// Phase 2: 构建哈希桶
void phase2_build_hash_buckets(
    LPSISender& sender,
    LPSIReceiver& receiver,
    size_t sender_data_size,
    size_t receiver_data_size,
    double& receiver_gen_idx_time) {
    
    MyTimer timer;
    
    // 计算主桶数量
    size_t num_main_buckets = static_cast<size_t>(LPSIConfig::MAIN_BUCKET_FACTOR * receiver_data_size);
    
    // 构建数据库索引 - 使用配置文件中的倍数参数
    sender.build_hash_buckets(num_main_buckets, LPSIConfig::OUTER_NUM_HASH_FUNCTIONS);
    sender.build_sub_buckets(sender_data_size, num_main_buckets, LPSIConfig::NUM_SUB_BUCKETS);
    
    timer.reset();
    // 生成查询索引
    receiver.build_hash_buckets(num_main_buckets, LPSIConfig::OUTER_NUM_HASH_FUNCTIONS);
    receiver.build_sub_buckets(sender_data_size, num_main_buckets, LPSIConfig::NUM_SUB_BUCKETS);
    auto gen_idx_time = timer.elapsed();
    receiver_gen_idx_time = gen_idx_time;
    
    // std::cout << "✓ 哈希桶构建完成" << std::endl;
}

// Phase 3: 准备PIR数据库和查询
void phase3_prepare_pir(
    LPSISender& sender,
    LPSIReceiver& receiver,
    std::vector<Element>& database,
    std::vector<uint32_t>& queries,
    size_t& pir_db_size,
    size_t& item_size) {
    
    // std::cout << "\n[Phase 3] 准备PIR数据库和查询" << std::endl;
    
    // Sender准备PIR数据库
    sender.prepare_pir_database(); //内部输出pirdb膨胀率等信息
    
    // Sender发送桶结构信息给Receiver
    size_t sender_num_main_buckets = sender.get_num_main_buckets();
    auto [sender_nh, sender_sub_capacity] = sender.get_sub_bucket_structure();
    
    // Receiver使用Sender的桶结构生成查询索引
    receiver.generate_pir_query_indices(sender_num_main_buckets, sender_nh, sender_sub_capacity);
    
    // 获取数据库和查询
    size_t num_items;
    database = sender.get_pir_database_as_bytes(num_items, item_size);
    queries = receiver.get_query_indices_flat();
    
    // std::cout << "✓ PIR数据库: " << num_items << " 项, 每项 " << item_size << " 字节" << std::endl;
    // std::cout << "✓ 查询索引: " << queries.size() << " 个" << std::endl;
    
    // // 将数据库大小调整为2的幂次
    // pir_db_size = 1;
    // while (pir_db_size < num_items) pir_db_size <<= 1;
    
    // // **关键修复**: 填充非0数据，避免PIR查询padding位置时返回transparent密文
    // while (database.size() < pir_db_size) {
    //     Element padding_item(item_size, 0);
    //     // 填充固定模式的非0数据
    //     for (size_t i = 0; i < item_size; ++i) {
    //         padding_item[i] = static_cast<unsigned char>((i + database.size()) % 256);
    //     }
    //     database.push_back(padding_item);
    // }
    
    // // std::cout << "✓ PIR数据库调整为 " << pir_db_size << " 项 (2的幂次)" << std::endl;
    
    // ===== 添加Phase2/3验证 =====
    // std::cout << "\n[双边映射正确性验证] 检查双层hash映射正确性..." << std::endl;
    receiver.verify_phase23_mapping(sender.get_flattened_database(),
                                     sender_num_main_buckets,
                                     sender_nh,
                                     sender_sub_capacity);
}

// Phase 4: 执行PIR查询
void phase4_execute_pir(
    LPSIReceiver& receiver,
    LPSISender& sender,
    const std::vector<Element>& database,
    const std::vector<uint32_t>& queries,
    size_t item_size,
    double &online_time,
    std::string batch_PIR_mode) {
    
    // 获取数据库的行列结构
    size_t sender_num_main_buckets = sender.get_num_main_buckets();
    auto [sender_nh, sender_sub_capacity] = sender.get_sub_bucket_structure();
    size_t num_rows = sender_num_main_buckets * sender_nh;  // 总行数 = 主桶数 × 子桶数
    size_t row_size = sender_sub_capacity;  // 每行大小 = 子桶容量

    std::cout << "---------------Call Batch PIR-----------------------" << std::endl;
    
    std::vector<Element> pir_results = run_batch_pir(database, queries, num_rows, row_size, item_size, online_time, batch_PIR_mode);

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
    std::vector<Element>& bucket_keys) {
    
    std::cout << "\n---------------Call OOS OT-----------------------" << std::endl;
    
    // 方式0: 明文模拟 (用于测试对比)
    // std::vector<size_t> requested_buckets = receiver.get_valid_bucket_indices();
    // bucket_keys = sender.send_bucket_keys_plaintext(requested_buckets);

    // 方式1: 调用test oos_OT
    // oos_OT_test();
    
    // 方式2: 真实OT协议
    std::vector<size_t> receiver_choices = receiver.get_ot_choices();
    
    if (receiver_choices.empty()) {
        // std::cout << "警告: Receiver没有命中任何元素,跳过OT" << std::endl;
        return;
    }
    
    // Sender准备OT输入
    if (!sender.prepare_ot_inputs(receiver_choices.size())) {
        std::cerr << "Sender OT准备失败" << std::endl;
        return;
    }
    
    // 获取Sender的OT输入 (所有桶密钥作为选项)
    std::vector<std::vector<Element>> sender_ot_base = sender.get_ot_inputs();
    
    if (sender_ot_base.empty() || sender_ot_base[0].empty()) {
        std::cerr << "Sender OT输入为空" << std::endl;
        return;
    }
    
    // 计算input_bit_count: 需要足够的比特数表示所有桶索引
    size_t num_buckets = sender.get_num_main_buckets();
    uint32_t input_bit_count = 0;
    while ((1ULL << input_bit_count) < num_buckets) {
        input_bit_count++;
    }
    
    size_t N = 1ULL << input_bit_count;  // OT选择数 (2的幂次)
    
    // 将桶密钥扩展到N个 (填充空密钥)
    // OT现在支持OT_DATA_SIZE字节的传输(默认32字节)
    std::vector<Element> padded_keys = sender_ot_base[0];
    
    while (padded_keys.size() < N) {
        padded_keys.push_back(Element(32, 0));  // 填充32字节的空密钥
    }
    
    // 构建完整的OT输入: 每个OT实例都提供N个选择
    size_t num_ot_instances = receiver_choices.size();
    std::vector<std::vector<Element>> sender_ot_inputs(num_ot_instances, padded_keys);
    
    // 执行OT协议
    std::vector<Element> ot_outputs;
    bool ot_success = run_oos_ot(sender_ot_inputs, receiver_choices, ot_outputs, 
                                  input_bit_count, true);
    
    if (!ot_success) {
        std::cerr << "OT执行失败" << std::endl;
        return;
    }
    
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
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "         协议执行输出:" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "【Sender视角】交集大小: " << sender.get_intersection_size() << std::endl;
    std::cout << "【Receiver视角】交集大小: " << intersection.size() << std::endl;
    
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
    
    std::cout << "[交集数据验证] " << valid_count << "/" << intersection.size() << " 个正确"<< std::endl;
    // 验证逻辑：Receiver取了16个交集，或者全部是交集？
    // 原逻辑：RECEIVER_SIZE 全部都是交集
    if (intersection.size() == receiver_size && sender.get_intersection_size() == intersection.size() && sender.get_intersection_size() != 0) {
        std::cout << "\n[Final 正确性验证]✓ 协议执行正确" << std::endl;
    } else {
        std::cout << "\n[Final 正确性验证]✗ 协议执行错误" << std::endl;
    }
}

void global_set_up() {
    cout<< "setup"<<endl;
    
}

int run_main(size_t sender_size, size_t receiver_size, size_t payload_size, std::string batch_PIR_mode) {
    std::cout << "========================================" << std::endl;
    std::cout << "   Payable LPSI协议 (动态参数分段计时)" << std::endl;
    std::cout << "========================================" << std::endl;

    // 创建实例
    LPSISender sender;
    LPSIReceiver receiver;
    
    std::vector<std::pair<std::string, std::string>> sender_raw_data;
    std::vector<std::string> receiver_raw_data;

    // [1] Initialization
    std::cout << "\n[1] 正在执行: 导入数据和初始化..." << std::endl;
    auto t0_start = high_resolution_clock::now();
    
    // 【修改】传入 size
    phase0_initialize_data(sender, receiver, sender_raw_data, receiver_raw_data, sender_size, receiver_size);
    
    std::cout << "[数据导入] Sender数据项数: " << sender_size << std::endl;
    std::cout << "[数据导入] Receiver数据项数: " << receiver_size << std::endl;
    std::cout << "[数据导入] Payload size: " << payload_size << " 字节" << std::endl;
    
    auto t0_end = high_resolution_clock::now();
    auto dur_init = duration_cast<milliseconds>(t0_end - t0_start).count();

    // [2] DH-OPRF
    std::cout << "\n[2] 正在执行: DH-OPRF..." << std::endl;
    double receiver_oprf_time = 0.0;
    phase1_oprf_prp(sender, receiver, receiver_oprf_time);
    auto dur_oprf = receiver_oprf_time;

    // [3] Hash Buckets
    std::cout << "\n[3] 正在执行: 构造双层Hash..." << std::endl;
    double receiver_gen_idx_time = 0.0;
    // 【修改】使用传入的 size
    phase2_build_hash_buckets(sender, receiver, sender_raw_data.size(), receiver_raw_data.size(), receiver_gen_idx_time);
    auto dur_hash = receiver_gen_idx_time;

    // ==========================================
    // 4. Batch PIR (Prepare + Execute)
    // ==========================================
    std::cout << "\n[4] 正在执行: Batch PIR (Prepare + Execute)..." << std::endl;
    
    std::vector<Element> database;
    std::vector<uint32_t> queries;
    size_t pir_db_size, item_size;

    // 准备PIR (DB & Query Generation)
    phase3_prepare_pir(sender, receiver, database, queries, pir_db_size, item_size);
    // 执行PIR (Cryptographic operations)
    double batch_pir_online_time = 0.0;
    phase4_execute_pir(receiver, sender, database, queries, item_size, batch_pir_online_time, batch_PIR_mode);
    auto dur_pir = batch_pir_online_time; // 转换为毫秒
    
    // --- 模拟PIR与验证 (不计入时间) ---
    simulate_pir(receiver, sender, database, queries, item_size);
    bool pir_match = true;
    if (receiver.sim_pir_results.size() != receiver.pir_results.size()) pir_match = false;
    else {
        for (size_t i = 0; i < receiver.sim_pir_results.size(); ++i) {
            if (receiver.sim_pir_results[i] != receiver.pir_results[i]) {
                pir_match = false; break;
            }
        }
    }
    std::cout << "[PIR外部验证] 模拟与实际结果匹配: " << (pir_match ? "YES" : "NO") << std::endl;
    
    // recevier处理PIR结果兼容格式
    // receiver.process_pir_results(receiver.sim_pir_results);
    receiver.process_pir_results(receiver.pir_results);

    // [5] OT
    std::cout << "\n[5] 正在执行: OT..." << std::endl;
    auto t4_start = high_resolution_clock::now();
    std::vector<Element> bucket_keys;
    phase5_ot_bucket_keys(sender, receiver, bucket_keys);
    auto t4_end = high_resolution_clock::now();
    auto dur_ot = duration_cast<milliseconds>(t4_end - t4_start).count();

    // [6] Decrypt
    std::cout << "\n[6] 正在执行: 解密验证..." << std::endl;
    auto t5_start = high_resolution_clock::now();
    // 【修改】传入 size
    phase6_decrypt_and_verify(sender, receiver, bucket_keys, sender_raw_data, receiver_raw_data, receiver_size);
    auto t5_end = high_resolution_clock::now();
    auto dur_decrypt = duration_cast<milliseconds>(t5_end - t5_start).count();

    // ==========================================
    // 汇总输出
    // ==========================================
    long long total_protocol_time = dur_oprf + dur_hash + dur_pir + dur_ot + dur_decrypt;
    
    // 定义列宽常量，方便调整
    const int LABEL_WIDTH = 32; // 标签列宽（因为包含中文，需要设大一点以保证对齐）
    const int TIME_WIDTH  = 10; // 时间数值列宽

    std::cout << "\n========================================" << std::endl;
    std::cout << "        协议执行在线耗时统计 (ms)" << std::endl;
    std::cout << "========================================" << std::endl;

    // 设置统一的小数格式：固定小数点，保留2位小数 (2 ms -> 2.00 ms)
    std::cout << std::fixed << std::setprecision(2);

    std::cout << std::left  << std::setw(LABEL_WIDTH) << "1. 导入数据 (Init):" 
            << std::right << std::setw(TIME_WIDTH)  << (double)dur_init << " ms" << std::endl;

    std::cout << "----------------------------------------" << std::endl;

    std::cout << std::left  << std::setw(LABEL_WIDTH) << "2. DH-OPRF:" 
            << std::right << std::setw(TIME_WIDTH)  << dur_oprf << " ms" << std::endl;

    std::cout << std::left  << std::setw(LABEL_WIDTH) << "3. 构造查询索引:" 
            << std::right << std::setw(TIME_WIDTH)  << dur_hash << " ms" << std::endl;

    std::cout << std::left  << std::setw(LABEL_WIDTH) << "4. Batch PIR:" 
            << std::right << std::setw(TIME_WIDTH)  << dur_pir << " ms" << std::endl;

    std::cout << std::left  << std::setw(LABEL_WIDTH) << "5. OT:" 
            << std::right << std::setw(TIME_WIDTH)  << (double)dur_ot << " ms" << std::endl;

    std::cout << std::left  << std::setw(LABEL_WIDTH) << "6. 解密结果:" 
            << std::right << std::setw(TIME_WIDTH)  << (double)dur_decrypt << " ms" << std::endl;

    std::cout << "========================================" << std::endl;

    std::cout << std::left  << std::setw(LABEL_WIDTH) << "总协议执行时间:" 
            << std::right << std::setw(TIME_WIDTH)  << total_protocol_time << " ms" << std::endl;
    std::cout << "(Total = 2+3+4+5+6, 不含初始化)" << std::endl;
    std::cout << "========================================" << std::endl;

    // 格式化汇总输出 (单位: 秒)
    std::cout << "\n============format online result(s)============================" << std::endl;
    // 打印receiver和sender的数据规模
    std::cout << "Sender_size " << sender_size << " Receiver_size " << receiver_size << " Payload_size_bytes " << payload_size << std::endl;
    std::cout << std::fixed << std::setprecision(3) << (double)total_protocol_time / 1000.0 << std::endl;
    std::cout << "(OPRF " << std::setprecision(3) << (double)dur_oprf / 1000.0 
              << " Gen_idx " << std::setprecision(3) << (double)dur_hash / 1000.0 
              << " Query_idx " << std::setprecision(3) << (double)dur_pir / 1000.0 
              << " Get_key " << std::setprecision(3) << (double)dur_ot / 1000.0 
              << " Dec " << std::setprecision(3) << (double)dur_decrypt / 1000.0 << ")" << std::endl;
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
    

    int opt;
    while ((opt = getopt(argc, argv, "x:y:p:h")) != -1) {
        switch (opt) {
            case 'x':
                sender_size = std::stoul(optarg);
                break;
            case 'y':
                receiver_size = std::stoul(optarg);
                break;
            case 'p':
                payload_size = std::stoul(optarg);
                // 注意: 还需要确保 LPSIConfig 中的 PIR_ITEM_SIZE 也被更新
                // 如果 config 是静态常量，这里可能需要额外处理
                // 建议在 config.h 中将 PIR_ITEM_SIZE 改为 static 变量或添加设置函数
                LPSIConfig::PIR_ITEM_SIZE = 128; // 示例: 固定为128 bytes，实际可根据需要调整
                break;
            case 'h':
                std::cout << "Usage: " << argv[0] << " [options]\n"
                          << "Options:\n"
                          << "  -x <size>   Sender data size (default: 4096)\n"
                          << "  -y <size>   Receiver data size (default: 256)\n"
                          << "  -p <bytes>  Payload size in bytes (default: 1)\n"
                          << "  -h          Show this help message\n";
                return 0;
            default:
                std::cerr << "Unknown option. Use -h for help." << std::endl;
                return 1;
        }
    }

    // 运行主逻辑
    std::string batch_PIR_mode = "direct"; // "default" or "direct"
    if (receiver_size == 1) {
        batch_PIR_mode = "default"; // 单元素查询使用默认模式
    }
    batch_PIR_mode = "default"; 
    run_main(sender_size, receiver_size, payload_size, batch_PIR_mode);
    
    return 0;
}