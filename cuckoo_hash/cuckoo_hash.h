#ifndef CUCKOO_HASH_H
#define CUCKOO_HASH_H

#include <vector>
#include <optional>
#include <functional>
#include <iostream>
#include <random>
#include <climits>
#include <cassert>

template<typename T>
class CuckooHashTable {
private:
    // Bucket结构，每个bucket可以容纳多个元素
    struct Bucket {
        std::vector<std::optional<T>> slots;
        
        Bucket(size_t capacity) : slots(capacity, std::nullopt) {}
        
        // 检查bucket是否满了
        bool is_full() const {
            for (const auto& slot : slots) {
                if (!slot.has_value()) return false;
            }
            return true;
        }
        
        // 在bucket中查找元素
        bool contains(const T& value) const {
            for (const auto& slot : slots) {
                if (slot.has_value() && slot.value() == value) {
                    return true;
                }
            }
            return false;
        }
        
        // 在bucket中插入元素，返回是否成功
        bool insert(const T& value) {
            for (auto& slot : slots) {
                if (!slot.has_value()) {
                    slot = value;
                    return true;
                }
            }
            return false; // bucket满了
        }
        
        // 删除bucket中的元素
        bool remove(const T& value) {
            for (auto& slot : slots) {
                if (slot.has_value() && slot.value() == value) {
                    slot = std::nullopt;
                    return true;
                }
            }
            return false;
        }
        
        // 踢出bucket中的第一个元素
        std::optional<T> evict_first() {
            for (auto& slot : slots) {
                if (slot.has_value()) {
                    auto value = slot.value();
                    slot = std::nullopt;
                    return value;
                }
            }
            return std::nullopt;
        }
    };

    size_t table_size_;          // 哈希表大小
    size_t way_num_;             // 哈希函数数量 (2, 3, 或 4)
    size_t bucket_capacity_;     // 每个bucket的容量
    size_t max_kick_count_;      // 最大踢出次数，防止无限循环
    
    std::vector<Bucket> table_;  // 哈希表
    std::vector<std::hash<T>> hash_funcs_;  // 哈希函数
    std::vector<uint64_t> hash_seeds_;      // 哈希种子
    
    std::mt19937 rng_;           // 随机数生成器

    // 生成不同的哈希函数
    void init_hash_functions() {
        hash_seeds_.clear();
        hash_funcs_.clear();
        
        std::random_device rd;
        std::mt19937 seed_gen(rd());
        
        for (size_t i = 0; i < way_num_; ++i) {
            hash_seeds_.push_back(seed_gen());
            hash_funcs_.emplace_back();
        }
    }
    
    // 计算第i个哈希函数的值
    size_t hash_function(const T& value, size_t func_index) const {
        assert(func_index < way_num_);
        auto base_hash = hash_funcs_[func_index](value);
        return (base_hash ^ hash_seeds_[func_index]) % table_size_;
    }

public:
    // 构造函数
    CuckooHashTable(size_t table_size, size_t way_num = 2, size_t bucket_capacity = 4, size_t max_kick_count = 500)
        : table_size_(table_size), way_num_(way_num), bucket_capacity_(bucket_capacity), 
          max_kick_count_(max_kick_count), rng_(std::random_device{}()) {
        
        assert(way_num >= 2 && way_num <= 4);
        assert(table_size > 0);
        assert(bucket_capacity > 0);
        
        // 初始化哈希表
        table_.reserve(table_size_);
        for (size_t i = 0; i < table_size_; ++i) {
            table_.emplace_back(bucket_capacity_);
        }
        
        // 初始化哈希函数
        init_hash_functions();
    }
    
    // 插入元素
    bool insert(const T& value) {
        // 首先检查元素是否已存在
        if (contains(value)) {
            return true; // 元素已存在
        }
        
        return insert_with_kick(value);
    }
    
    // 查找元素
    bool contains(const T& value) const {
        for (size_t i = 0; i < way_num_; ++i) {
            size_t bucket_index = hash_function(value, i);
            if (table_[bucket_index].contains(value)) {
                return true;
            }
        }
        return false;
    }
    
    // 删除元素
    bool remove(const T& value) {
        for (size_t i = 0; i < way_num_; ++i) {
            size_t bucket_index = hash_function(value, i);
            if (table_[bucket_index].remove(value)) {
                return true;
            }
        }
        return false;
    }
    
    // 获取负载因子
    double load_factor() const {
        size_t total_elements = 0;
        for (const auto& bucket : table_) {
            for (const auto& slot : bucket.slots) {
                if (slot.has_value()) {
                    total_elements++;
                }
            }
        }
        return static_cast<double>(total_elements) / (table_size_ * bucket_capacity_);
    }
    
    // 清空哈希表
    void clear() {
        for (auto& bucket : table_) {
            for (auto& slot : bucket.slots) {
                slot = std::nullopt;
            }
        }
    }
    
    // 获取统计信息
    void print_stats() const {
        size_t total_elements = 0;
        size_t used_buckets = 0;
        size_t full_buckets = 0;
        
        for (const auto& bucket : table_) {
            bool bucket_used = false;
            size_t bucket_elements = 0;
            
            for (const auto& slot : bucket.slots) {
                if (slot.has_value()) {
                    total_elements++;
                    bucket_elements++;
                    bucket_used = true;
                }
            }
            
            if (bucket_used) used_buckets++;
            if (bucket_elements == bucket_capacity_) full_buckets++;
        }
        
        std::cout << "=== Cuckoo Hash Table Statistics ===" << std::endl;
        std::cout << "Table size: " << table_size_ << std::endl;
        std::cout << "Way number: " << way_num_ << std::endl;
        std::cout << "Bucket capacity: " << bucket_capacity_ << std::endl;
        std::cout << "Total elements: " << total_elements << std::endl;
        std::cout << "Used buckets: " << used_buckets << " / " << table_size_ << std::endl;
        std::cout << "Full buckets: " << full_buckets << std::endl;
        std::cout << "Load factor: " << load_factor() << std::endl;
        std::cout << "=================================" << std::endl;
    }

private:
    // 带踢出机制的插入
    bool insert_with_kick(const T& value) {
        T current_value = value;
        size_t kick_count = 0;
        
        while (kick_count < max_kick_count_) {
            // 尝试所有可能的bucket
            for (size_t i = 0; i < way_num_; ++i) {
                size_t bucket_index = hash_function(current_value, i);
                
                // 如果bucket有空位，直接插入
                if (table_[bucket_index].insert(current_value)) {
                    return true;
                }
            }
            
            // 所有bucket都满了，需要踢出一个元素
            // 随机选择一个bucket进行踢出
            size_t chosen_func = rng_() % way_num_;
            size_t bucket_index = hash_function(current_value, chosen_func);
            
            // 踢出bucket中的一个元素
            auto evicted = table_[bucket_index].evict_first();
            if (!evicted.has_value()) {
                return false; // 不应该发生
            }
            
            // 插入当前元素
            if (!table_[bucket_index].insert(current_value)) {
                return false; // 不应该发生
            }
            
            // 继续尝试插入被踢出的元素
            current_value = evicted.value();
            kick_count++;
        }
        
        // 达到最大踢出次数，插入失败
        return false;
    }
};

#endif // CUCKOO_HASH_H
