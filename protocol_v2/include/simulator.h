#ifndef Simulator_H
#define Simulator_H

#include <cstddef>
#include <cstdint>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include "config.h"

#include "kuku/kuku.h"
#include <vector>
#include <cstdint>

// 基本数据类型定义
using Element = std::vector<unsigned char>;

class Simulator {
public:
    Simulator();
    ~Simulator();

    // sender数据
    // receiver数据
    std::vector<std::pair<std::string, std::string>> sender_raw_data;
    std::vector<std::string> receiver_raw_data;

    // hash桶结构
    std::vector<std::vector<Element>> sender_main_buckets;
    std::vector<std::vector<std::vector<Element>>> sender_sub_buckets;
}

#endif // Simulator_H
