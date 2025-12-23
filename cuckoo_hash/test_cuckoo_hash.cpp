#include "cuckoo_hash.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <set>
#include <random>
#include <cassert>

class CuckooHashTest {
private:
    std::mt19937 rng_;
    
public:
    CuckooHashTest() : rng_(std::random_device{}()) {}
    
    // 基本功能测试
    void test_basic_operations() {
        std::cout << "=== 测试基本操作 ===" << std::endl;
        
        for (size_t way_num = 2; way_num <= 4; ++way_num) {
            std::cout << "测试 " << way_num << "-way cuckoo hash..." << std::endl;
            
            CuckooHashTable<int> table(100, way_num, 4);
            
            // 测试插入
            std::vector<int> test_values = {1, 2, 3, 4, 5, 10, 20, 30, 40, 50};
            for (int val : test_values) {
                assert(table.insert(val));
                assert(table.contains(val));
            }
            
            // 测试重复插入
            for (int val : test_values) {
                assert(table.insert(val)); // 应该返回true（元素已存在）
            }
            
            // 测试删除
            assert(table.remove(1));
            assert(!table.contains(1));
            assert(!table.remove(1)); // 删除不存在的元素应该返回false
            
            // 测试查找不存在的元素
            assert(!table.contains(999));
            
            std::cout << "✓ " << way_num << "-way 基本操作测试通过" << std::endl;
        }
        std::cout << std::endl;
    }
    
    // 压力测试
    void test_stress_insertion() {
        std::cout << "=== 压力测试 ===" << std::endl;
        
        for (size_t way_num = 2; way_num <= 4; ++way_num) {
            std::cout << "测试 " << way_num << "-way cuckoo hash 压力插入..." << std::endl;
            
            CuckooHashTable<int> table(500, way_num, 4);
            std::set<int> inserted_values;
            
            int successful_insertions = 0;
            int total_attempts = 1000;
            
            for (int i = 0; i < total_attempts; ++i) {
                int value = rng_() % 10000;
                if (table.insert(value)) {
                    inserted_values.insert(value);
                    successful_insertions++;
                }
            }
            
            // 验证所有插入的值都能被找到
            for (int value : inserted_values) {
                assert(table.contains(value));
            }
            
            table.print_stats();
            std::cout << "成功插入: " << successful_insertions << " / " << total_attempts << std::endl;
            std::cout << "负载因子: " << table.load_factor() << std::endl;
            std::cout << "✓ " << way_num << "-way 压力测试通过" << std::endl;
            std::cout << std::endl;
        }
    }
    
    // 性能测试
    void test_performance() {
        std::cout << "=== 性能测试 ===" << std::endl;
        
        const int num_operations = 10000;
        std::vector<int> test_data;
        test_data.reserve(num_operations);
        
        // 生成测试数据
        for (int i = 0; i < num_operations; ++i) {
            test_data.push_back(rng_() % 50000);
        }
        
        for (size_t way_num = 2; way_num <= 4; ++way_num) {
            std::cout << "测试 " << way_num << "-way 性能..." << std::endl;
            
            CuckooHashTable<int> table(2000, way_num, 4);
            
            // 插入性能测试
            auto start = std::chrono::high_resolution_clock::now();
            int successful_inserts = 0;
            for (int value : test_data) {
                if (table.insert(value)) {
                    successful_inserts++;
                }
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto insert_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            
            // 查找性能测试
            start = std::chrono::high_resolution_clock::now();
            int found_count = 0;
            for (int value : test_data) {
                if (table.contains(value)) {
                    found_count++;
                }
            }
            end = std::chrono::high_resolution_clock::now();
            auto lookup_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            
            std::cout << "插入时间: " << insert_time.count() << " μs" << std::endl;
            std::cout << "查找时间: " << lookup_time.count() << " μs" << std::endl;
            std::cout << "成功插入: " << successful_inserts << " / " << num_operations << std::endl;
            std::cout << "查找命中: " << found_count << " / " << num_operations << std::endl;
            std::cout << "负载因子: " << table.load_factor() << std::endl;
            std::cout << std::endl;
        }
    }
    
    // 踢出机制测试
    void test_kick_mechanism() {
        std::cout << "=== 踢出机制测试 ===" << std::endl;
        
        // 使用较小的表和bucket容量来更容易触发踢出
        CuckooHashTable<int> table(10, 2, 2, 100);
        
        std::vector<int> values;
        int insert_count = 0;
        
        // 插入足够多的元素来触发踢出机制
        for (int i = 0; i < 50; ++i) {
            if (table.insert(i)) {
                values.push_back(i);
                insert_count++;
            }
        }
        
        std::cout << "成功插入 " << insert_count << " 个元素到小表中" << std::endl;
        
        // 验证所有插入的元素都能被找到
        for (int value : values) {
            assert(table.contains(value));
        }
        
        table.print_stats();
        std::cout << "✓ 踢出机制测试通过" << std::endl;
        std::cout << std::endl;
    }
    
    // 边界条件测试
    void test_edge_cases() {
        std::cout << "=== 边界条件测试 ===" << std::endl;
        
        // 测试空表
        CuckooHashTable<int> empty_table(10, 2, 4);
        assert(!empty_table.contains(1));
        assert(!empty_table.remove(1));
        assert(empty_table.load_factor() == 0.0);
        
        // 测试单元素
        assert(empty_table.insert(42));
        assert(empty_table.contains(42));
        assert(empty_table.remove(42));
        assert(!empty_table.contains(42));
        
        // 测试清空操作
        empty_table.insert(1);
        empty_table.insert(2);
        empty_table.insert(3);
        empty_table.clear();
        assert(!empty_table.contains(1));
        assert(!empty_table.contains(2));
        assert(!empty_table.contains(3));
        assert(empty_table.load_factor() == 0.0);
        
        std::cout << "✓ 边界条件测试通过" << std::endl;
        std::cout << std::endl;
    }
    
    // 不同数据类型测试
    void test_different_types() {
        std::cout << "=== 不同数据类型测试 ===" << std::endl;
        
        // 测试字符串
        CuckooHashTable<std::string> string_table(100, 3, 4);
        std::vector<std::string> strings = {"hello", "world", "cuckoo", "hash", "table"};
        
        for (const auto& str : strings) {
            assert(string_table.insert(str));
            assert(string_table.contains(str));
        }
        
        assert(string_table.remove("hello"));
        assert(!string_table.contains("hello"));
        
        // 测试double
        CuckooHashTable<double> double_table(100, 4, 4);
        std::vector<double> doubles = {3.14, 2.71, 1.41, 0.57, 9.8};
        
        for (double val : doubles) {
            assert(double_table.insert(val));
            assert(double_table.contains(val));
        }
        
        std::cout << "✓ 不同数据类型测试通过" << std::endl;
        std::cout << std::endl;
    }
    
    // 运行所有测试
    void run_all_tests() {
        std::cout << "开始运行 Cuckoo Hash 测试套件..." << std::endl;
        std::cout << "==========================================" << std::endl;
        
        test_basic_operations();
        test_edge_cases();
        test_different_types();
        test_kick_mechanism();
        test_stress_insertion();
        test_performance();
        
        std::cout << "==========================================" << std::endl;
        std::cout << "✅ 所有测试通过！" << std::endl;
    }
};

int main() {
    try {
        CuckooHashTest test;
        test.run_all_tests();
        
        // 简单的使用示例
        std::cout << std::endl << "=== 使用示例 ===" << std::endl;
        
        // 创建一个3-way cuckoo hash表
        CuckooHashTable<int> hash_table(100, 3, 4);
        
        // 插入一些元素
        for (int i = 1; i <= 20; ++i) {
            if (hash_table.insert(i)) {
                std::cout << "插入 " << i << " 成功" << std::endl;
            }
        }
        
        // 查找元素
        std::cout << "查找元素 10: " << (hash_table.contains(10) ? "找到" : "未找到") << std::endl;
        std::cout << "查找元素 25: " << (hash_table.contains(25) ? "找到" : "未找到") << std::endl;
        
        // 删除元素
        if (hash_table.remove(10)) {
            std::cout << "删除元素 10 成功" << std::endl;
        }
        
        // 显示统计信息
        hash_table.print_stats();
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}