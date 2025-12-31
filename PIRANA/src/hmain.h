#ifndef __MAIN_H
#define __MAIN_H

#include <assert.h>
#include <getopt.h>

#include <iostream>
#include <map>
#include <unordered_set>

#include "client.h"
#include "pir_parms.h"
#include "server.h"
#include "test.h"

using Element = std::vector<unsigned char>;

std::vector<Element> my_batch_pir_main(const uint64_t num_payloads, const uint64_t payload_size,
                    const uint64_t num_query, const bool is_batch,
                    const bool is_compress, std::vector<std::vector<uint64_t>> input_db, std::vector<uint32_t> query_indices, double& online_time);

std::vector<Element> my_direct_batch_pir_main(
    const uint64_t num_payloads, 
    const uint64_t payload_size,
    const uint64_t num_query, 
    const bool is_batch,     // 虽然叫 is_batch，但在 Direct Mode 下隐含为 true
    const bool is_compress,  // 通常为 false
    std::vector<std::vector<uint64_t>> input_db, 
    std::vector<uint32_t> query_indices,
    size_t col_size, 
    double& online_time);
    
void batch_pir_main(const uint64_t num_payloads, const uint64_t payload_size,
                    const uint64_t num_query, const bool is_batch,
                    const bool is_compress);

#endif