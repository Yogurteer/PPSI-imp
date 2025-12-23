#include "../PIRANA/src/client.h"
#include "../PIRANA/src/server.h"
#include "../PIRANA/src/pir_parms.h"
#include "../PIRANA/src/test.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <cstring>
#include <random>

// 定义Element类型为字节向量
using Element = std::vector<unsigned char>;

// 辅助函数：从slots解码为字节数组
std::vector<unsigned char> decode_payload(
    const std::vector<uint64_t>& slots,
    size_t payload_size,
    uint64_t bits_per_slot) {
    
    std::vector<unsigned char> result(payload_size, 0);
    uint64_t bit_pos = 0;
    
    for (size_t byte_idx = 0; byte_idx < payload_size; ++byte_idx) {
        unsigned char byte_value = 0;
        for (int bit = 0; bit < 8; ++bit, ++bit_pos) {
            uint64_t slot_idx = bit_pos / bits_per_slot;
            uint64_t bit_in_slot = bit_pos % bits_per_slot;
            
            if (slot_idx < slots.size()) {
                uint64_t bit_value = (slots[slot_idx] >> bit_in_slot) & 1;
                byte_value |= (bit_value << bit);
            }
        }
        result[byte_idx] = byte_value;
    }
    
    return result;
}

// 执行Batch PIR
std::vector<Element> run_batch_pir(
    const std::vector<Element>& database_items,
    const std::vector<uint32_t>& query_indices,
    size_t num_payloads,
    size_t payload_size) {
    
    std::cout << "\n=== 执行Batch PIR ===" << std::endl;
    std::cout << "数据库大小: " << num_payloads << " 项" << std::endl;
    std::cout << "每项大小: " << payload_size << " 字节" << std::endl;
    std::cout << "查询数量: " << query_indices.size() << std::endl;
    
    // 创建PIR参数
    PirParms pir_parms(num_payloads, payload_size);
    
    // 创建PIRANA中的Server和Client
    Server pir_server(pir_parms, false);  // false = 不生成随机数据库
    Client pir_client(pir_parms);
    
    // 交换密钥
    std::stringstream keys = pir_client.save_keys();
    pir_server.set_keys(keys);
    
    // 调试用: 设置decryptor
    auto sk = pir_client.send_secret_keys();
    pir_server.set_decryptor(sk);
    
    // 将数据库转换为PIR格式
    // 注意: PIRANA会将num_payloads向上取整到2的幂次方
    // 所以我们需要填充数据库到实际大小
    size_t actual_db_size = pir_parms.get_num_payloads();
    std::cout << "实际数据库大小 (2的幂次方): " << actual_db_size << std::endl;
    
    // 创建数据库副本并填充到实际大小
    std::vector<Element> padded_db = database_items;
    while (padded_db.size() < actual_db_size) {
        padded_db.push_back(Element(payload_size, 0));
    }
    
    std::vector<std::vector<uint64_t>> raw_db;
    auto seal_parms = pir_parms.get_seal_parms();
    uint64_t plain_modulus_bit = seal_parms.plain_modulus().bit_count();
    uint64_t bits_per_slot = plain_modulus_bit - 1;
    uint64_t num_slots = std::ceil(payload_size * 8.0 / bits_per_slot);
    
    for (const auto& item : padded_db) {
        std::vector<uint64_t> slots(num_slots, 0);
        
        uint64_t bit_pos = 0;
        for (unsigned char byte : item) {
            for (int bit = 0; bit < 8; ++bit, ++bit_pos) {
                uint64_t bit_value = (byte >> bit) & 1;
                uint64_t slot_idx = bit_pos / bits_per_slot;
                uint64_t bit_in_slot = bit_pos % bits_per_slot;
                if (slot_idx < num_slots) {
                    slots[slot_idx] |= (bit_value << bit_in_slot);
                }
            }
        }
        
        raw_db.push_back(slots);
    }
    
    // 设置服务器数据库
    pir_server.set_database(raw_db);
    pir_server.encode_to_ntt_db();
    
    std::cout << "PIR数据库编码完成" << std::endl;
    
    // [验证] 检查编码前后数据一致性
    std::cout << "[验证] 检查前3个数据库条目的编码..." << std::endl;
    for (size_t i = 0; i < std::min(size_t(3), padded_db.size()); ++i) {
        std::cout << "  条目[" << i << "] 原始大小: " << padded_db[i].size() 
                  << " bytes, 编码后slots数: " << raw_db[i].size() << std::endl;
    }
    
    // 执行批量查询
    std::vector<Element> results;
    
    for (uint32_t query_idx : query_indices) {
        if (query_idx >= num_payloads) {
            std::cout << "警告: 查询索引 " << query_idx << " 超出范围" << std::endl;
            results.push_back(Element(payload_size, 0));
            continue;
        }
        
        // 生成查询
        std::stringstream query = pir_client.gen_query(query_idx);
        
        // 生成响应
        std::stringstream response = pir_server.gen_response(query);
        
        // 提取答案
        std::vector<std::vector<uint64_t>> answer = pir_client.extract_answer(response);
        
        // 使用PIRANA的测试函数验证正确性
        test_pir_correctness(pir_server, answer, query_idx, pir_parms);
        
        // 从answer中提取payload slots
        auto real_item = pir_server.get_plain_response(query_idx);
        
        // 解码为字节
        std::vector<unsigned char> decoded = decode_payload(
            real_item, payload_size, bits_per_slot);
        
        // [验证] 对于前3个查询，验证解码结果
        if (results.size() < 3) {
            std::cout << "[验证-PIR] 查询索引=" << query_idx 
                      << ", 解码结果大小=" << decoded.size() << " bytes" << std::endl;
            if (decoded.size() >= 8) {
                std::cout << "  前8字节: ";
                for (size_t b = 0; b < 8; ++b) {
                    printf("%02x", decoded[b]);
                }
                std::cout << "..." << std::endl;
            }
        }
        
        results.push_back(decoded);
    }
    
    std::cout << "PIR查询完成, 返回 " << results.size() << " 个结果" << std::endl;
    return results;
}

int main() {
    std::cout << "=== PIRANA PIR 正确性测试 ===" << std::endl;
    
    // 测试参数
    const size_t num_payloads = 100;     // 数据库中的元素数量
    const size_t payload_size = 64;      // 每个元素的字节数 (64 bytes = 512 bits)
    
    // 创建随机数生成器
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned char> dis(0, 255);
    
    // 创建测试数据库
    std::cout << "创建测试数据库..." << std::endl;
    std::vector<Element> database_items;
    for (size_t i = 0; i < num_payloads; ++i) {
        Element item(payload_size);
        // 填充随机数据
        for (size_t j = 0; j < payload_size; ++j) {
            item[j] = dis(gen);
        }
        database_items.push_back(item);
    }
    
    // 创建查询索引
    std::vector<uint32_t> query_indices = {0, 5, 10, 50, 99};
    
    std::cout << "数据库大小: " << num_payloads << " 项" << std::endl;
    std::cout << "每项大小: " << payload_size << " 字节" << std::endl;
    std::cout << "查询索引: ";
    for (auto idx : query_indices) {
        std::cout << idx << " ";
    }
    std::cout << std::endl << std::endl;
    
    // 执行PIR查询
    auto results = run_batch_pir(database_items, query_indices, num_payloads, payload_size);
    
    // 验证结果
    std::cout << "\n=== 验证查询结果 ===" << std::endl;
    bool all_correct = true;
    for (size_t i = 0; i < query_indices.size(); ++i) {
        uint32_t idx = query_indices[i];
        const auto& expected = database_items[idx];
        const auto& actual = results[i];
        
        bool match = (expected.size() == actual.size()) && 
                     (std::memcmp(expected.data(), actual.data(), expected.size()) == 0);
        
        std::cout << "查询索引 " << idx << ": ";
        if (match) {
            std::cout << "✓ 正确" << std::endl;
        } else {
            std::cout << "✗ 错误" << std::endl;
            all_correct = false;
            
            // 显示差异
            std::cout << "  期望前8字节: ";
            for (size_t b = 0; b < std::min(size_t(8), expected.size()); ++b) {
                printf("%02x", expected[b]);
            }
            std::cout << std::endl;
            
            std::cout << "  实际前8字节: ";
            for (size_t b = 0; b < std::min(size_t(8), actual.size()); ++b) {
                printf("%02x", actual[b]);
            }
            std::cout << std::endl;
        }
    }
    
    std::cout << "\n=== 测试结果 ===" << std::endl;
    if (all_correct) {
        std::cout << "✓ 所有查询结果正确！" << std::endl;
        return 0;
    } else {
        std::cout << "✗ 部分查询结果错误" << std::endl;
        return 1;
    }
}