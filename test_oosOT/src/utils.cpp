#include "utils.h"

PSI::PSI() {
    // 初始化椭圆曲线 (使用P-256)
    curve_group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    bn_ctx = BN_CTX_new();
    
    if (!curve_group || !bn_ctx) {
        throw std::runtime_error("Failed to initialize elliptic curve");
    }
}

PSI::~PSI() {
    if (curve_group) EC_GROUP_free(curve_group);
    if (bn_ctx) BN_CTX_free(bn_ctx);
}

// 哈希函数 H()
Element PSI::hash_H(const Element& input) {
    Element result(SHA256_DIGEST_LENGTH);
    SHA256(input.data(), input.size(), result.data());
    return result;
}

// 哈希函数 H_1()
Element PSI::hash_H1(const Element& input) {
    Element temp = hash_H(input);
    Element result(SHA256_DIGEST_LENGTH);
    SHA256(temp.data(), temp.size(), result.data());
    return result;
}

// 哈希函数 H_2()
Element PSI::hash_H2(const Element& r_k, const Element& H_x_rs) {
    Element combined = r_k;
    combined.insert(combined.end(), H_x_rs.begin(), H_x_rs.end());
    Element result(SHA256_DIGEST_LENGTH);
    SHA256(combined.data(), combined.size(), result.data());
    return result;
}

// 将字节数组映射到椭圆曲线点
EC_POINT* PSI::map_to_curve(const Element& data) {
    Element hashed = hash_H(data);
    
    EC_POINT* point = EC_POINT_new(curve_group);
    BIGNUM* x = BN_new();
    
    // 将哈希值转换为大数
    BN_bin2bn(hashed.data(), std::min(hashed.size(), size_t(32)), x);
    
    // 使用生成元的倍数来构造点
    EC_POINT_mul(curve_group, point, x, nullptr, nullptr, bn_ctx);
    
    BN_free(x);
    return point;
}

// 点转字节数组
Element PSI::point_to_bytes(const EC_POINT* point) {
    size_t len = EC_POINT_point2oct(curve_group, point, 
                                  POINT_CONVERSION_COMPRESSED, 
                                  nullptr, 0, bn_ctx);
    Element result(len);
    EC_POINT_point2oct(curve_group, point, 
                     POINT_CONVERSION_COMPRESSED, 
                     result.data(), len, bn_ctx);
    return result;
}

// 字节数组转点
EC_POINT* PSI::bytes_to_point(const Element& data) {
    EC_POINT* point = EC_POINT_new(curve_group);
    if (!EC_POINT_oct2point(curve_group, point, 
                          data.data(), data.size(), bn_ctx)) {
        EC_POINT_free(point);
        throw std::runtime_error("Failed to convert bytes to point");
    }
    return point;
}

PRP::PRP(size_t size, uint32_t seed) : rng(seed) {
    permutation.resize(size);
    for (size_t i = 0; i < size; ++i) {
        permutation[i] = i;
    }
    std::shuffle(permutation.begin(), permutation.end(), rng);
}

HashBuckets::HashBuckets(size_t n, size_t sub_bucket_num, int sub_nh) 
    : bucket_count(static_cast<size_t>(1.5 * n)), 
      sub_bucket_count(sub_bucket_num), 
      nh(sub_nh) {
    // 主桶哈希种子（原有3个）
    hash_seeds.resize(3);
    for (int i = 0; i < 3; ++i) {
        hash_seeds[i] = std::random_device{}();
    }
    // 子桶哈希种子（nh个）
    sub_hash_seeds.resize(nh);
    for (int i = 0; i < nh; ++i) {
        sub_hash_seeds[i] = std::random_device{}();
    }
}

// 计算哈希桶索引
size_t HashBuckets::compute_simple_hash_bucket(const Element& element, int hash_func_idx) const{
    if (hash_func_idx >= 3) hash_func_idx = 0;
    
    uint32_t seed = hash_seeds[hash_func_idx];
    uint32_t hash = seed;
    
    for (unsigned char byte : element) {
        hash = hash * 31 + byte;
    }
    
    return hash % bucket_count;
}

// 子桶索引计算
size_t HashBuckets::compute_sub_hash_bucket(const Element& element, int sub_hash_idx) const{
    if (sub_hash_idx >= nh) sub_hash_idx = 0;
    
    uint32_t seed = sub_hash_seeds[sub_hash_idx];
    uint32_t hash = seed;
    
    for (unsigned char byte : element) {
        hash = hash * 31 + byte;
    }
    
    return hash % sub_bucket_count;
}