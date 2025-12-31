#ifndef UTILS_H
#define UTILS_H

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <random>
#include <iostream>
#include <algorithm>

// 基本数据类型定义
using Element = std::vector<unsigned char>;
using ElementVector = std::vector<Element>;
using ElementMatrix = std::vector<ElementVector>;

class PSI {
private:
    // 椭圆曲线上下文
    EC_GROUP* curve_group;
    BN_CTX* bn_ctx;
    
public:
    PSI();
    ~PSI();
    
    // 获取椭圆曲线相关资源
    EC_GROUP* get_curve_group() const { return curve_group; }
    BN_CTX* get_bn_ctx() const { return bn_ctx; }
    
    // 哈希函数
    Element hash_H(const Element& input);
    Element hash_H1(const Element& input);
    Element hash_H2(const Element& r_k, const Element& H_x_rs);
    
    // 椭圆曲线操作
    EC_POINT* map_to_curve(const Element& data);
    Element point_to_bytes(const EC_POINT* point);
    EC_POINT* bytes_to_point(const Element& data);
};

// PRP 伪随机置换
class PRP {
private:
    std::vector<size_t> permutation;
    std::mt19937 rng;
    
public:
    PRP(size_t size, uint32_t seed = 0);
    
    // 打乱向量
    template<typename T>
    std::vector<T> shuffle(const std::vector<T>& input) {
        if (input.size() != permutation.size()) {
            throw std::invalid_argument("Input size doesn't match PRP size");
        }
        
        std::vector<T> result(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            result[i] = input[permutation[i]];
        }
        return result;
    }
};

// 哈希桶实现
class HashBuckets {
private:
    size_t bucket_count;
    size_t sub_bucket_count;           // 每个主桶的子桶
    int nh;                            // 子桶哈希函数数量（多路）
    std::vector<uint32_t> hash_seeds;
    std::vector<uint32_t> sub_hash_seeds;  // 子桶哈希种子
    
public:
    // HashBuckets(size_t n);
    // 新增构造函数：支持主桶数量、子桶数量、子桶哈希函数数量nh
    HashBuckets(size_t n, size_t sub_bucket_num = 8, int sub_nh = 3);
    
    // 计算哈希桶索引
    size_t compute_simple_hash_bucket(const Element& element, int hash_func_idx) const;
    
    // 计算子桶索引
    size_t compute_sub_hash_bucket(const Element& element, int sub_hash_idx) const;

    // 获取桶数量
    size_t get_bucket_count() const { return bucket_count; }

    // 获取子桶数量
    size_t get_sub_bucket_count() const { return sub_bucket_count; }
    
    // 获取子桶哈希函数数量
    int get_sub_nh() const { return nh; }
};

#endif // UTILS_H