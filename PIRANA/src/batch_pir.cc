#include "hmain.h"
#include <iomanip>

std::vector<Element> my_batch_pir_main(const uint64_t num_payloads, const uint64_t payload_size,
                    const uint64_t num_query, const bool is_batch,
                    const bool is_compress, std::vector<std::vector<uint64_t>> input_db, std::vector<uint32_t> query_indices, double& online_time) {
  std::cout << "Start my batch PIR! " << std::endl;
  Timer timer;
  timer.reset();

  std::vector<Element> results;

  PirParms pir_parms(num_payloads, payload_size, num_query, is_batch,
                     is_compress);
  
  // ===== 数据验证和预处理 =====
  auto plain_modulus = pir_parms.get_seal_parms().plain_modulus().value();
  auto plain_modulus_bit = pir_parms.get_seal_parms().plain_modulus().bit_count();
  auto expected_num_payload_slot = pir_parms.get_num_payload_slot();
  
  // std::cout << "\n========== PIR 参数验证 ==========" << std::endl;
  // std::cout << "输入: num_payloads=" << num_payloads << ", payload_size=" << payload_size << " (字节)" << std::endl;
  // std::cout << "PIR配置: plain_modulus=" << plain_modulus << " (" << plain_modulus_bit << " bits)" << std::endl;
  // std::cout << "每个payload需要的slots数: " << expected_num_payload_slot << std::endl;
  // std::cout << "实际输入数据库大小: " << input_db.size() << " payloads" << std::endl;
  
  // 验证输入数据库大小
  if (input_db.size() != num_payloads) {
    std::cerr << "错误: 输入数据库大小(" << input_db.size() 
              << ")与参数num_payloads(" << num_payloads << ")不匹配!" << std::endl;
    throw std::runtime_error("Database size mismatch");
  }
  
  // 验证和预处理每个payload
  bool has_zero_value = false;
  bool has_overflow = false;
  uint64_t max_value_found = 0;
  
  for (size_t i = 0; i < input_db.size(); i++) {
    auto& payload = input_db[i];
    
    // 检查payload的slot数量
    if (payload.size() != expected_num_payload_slot) {
      std::cerr << "错误: Payload[" << i << "]的大小(" << payload.size() 
                << ")与预期的num_payload_slot(" << expected_num_payload_slot << ")不匹配!" << std::endl;
      std::cerr << "提示: payload_size=" << payload_size << " 字节, 每个slot可存储 " 
                << (plain_modulus_bit - 1) << " bits" << std::endl;
      std::cerr << "计算公式: num_payload_slot = ceil(payload_size * 8 / (plain_modulus_bit - 1))" << std::endl;
      std::cerr << "         = ceil(" << payload_size << " * 8 / " << (plain_modulus_bit - 1) << ")" << std::endl;
      std::cerr << "         = " << expected_num_payload_slot << std::endl;
      throw std::runtime_error("Payload slot count mismatch");
    }
    
    // 检查每个值
    for (size_t j = 0; j < payload.size(); j++) {
      auto& value = payload[j];
      max_value_found = std::max(max_value_found, value);
      
      // 处理0值 (与gen_random_db保持一致)
      if (value == 0) {
        value = 8888;
        has_zero_value = true;
      }
      
      // 检查是否超出plain_modulus
      if (value >= plain_modulus) {
        has_overflow = true;
        std::cerr << "错误: Payload[" << i << "][" << j << "]的值(" << value 
                  << ")超出plain_modulus(" << plain_modulus << ")!" << std::endl;
      }
    }
  }
  
  if (has_zero_value) {
    std::cout << "提示: 检测到0值并替换为8888 (与PIR原始实现保持一致)" << std::endl;
  }
  
  if (has_overflow) {
    std::cerr << "\n数据验证失败: 发现超出plain_modulus的值!" << std::endl;
    std::cerr << "最大值: " << max_value_found << ", plain_modulus: " << plain_modulus << std::endl;
    throw std::runtime_error("Data value exceeds plain_modulus constraint");
  }
  
  std::cout << "[PIR参数预处理]数据验证通过! payload size最大值 " << max_value_found << " <  plain_modulus " << plain_modulus << std::endl;
  // std::cout << "===================================\n" << std::endl;
  
  Client batch_client(pir_parms);
  std::stringstream keys = batch_client.save_keys();
  Server batch_server(pir_parms, is_batch, false, input_db);

  batch_server.set_keys(keys);
  auto init_time = timer.elapsed();

  timer.reset();

  // random generate query
  
  std::vector<uint32_t> batch_query_index(num_query);
  batch_query_index = query_indices;
  // for (auto &q : batch_query_index) {
  //   q = rand() % num_payloads;
  // }
  std::stringstream query = batch_client.gen_batch_query(batch_query_index);
  auto query_time = timer.elapsed();

  timer.reset();
  std::stringstream response = batch_server.gen_batch_response(query);
  auto response_time = timer.elapsed();

  timer.reset();
  std::vector<std::vector<uint64_t>> answer =
      batch_client.extract_batch_answer(response);
  auto extract_time = timer.elapsed();

  test_batch_pir_correctness(batch_server, answer, batch_query_index,
                             pir_parms);
  std::cout << "\n[PIR内部验证] check pir correctness passed!" << std::endl;
  
  // 辅助函数: 从answer提取单个查询的payload slots
  auto extract_payload_slots = [&](size_t q_idx, uint32_t query_index) -> std::vector<uint64_t> {
    auto table = pir_parms.get_cuckoo_table();
    kuku::QueryResult res = table->query(kuku::make_item(0, query_index));
    auto loc = res.location();
    
    std::vector<uint64_t> real_item;
    
    if (pir_parms.get_is_compress() == false) {
      auto N = pir_parms.get_seal_parms().poly_modulus_degree();
      auto num_ct = pir_parms.get_num_payload_slot();
      auto bundle_size = pir_parms.get_bundle_size();
      real_item.resize(num_ct);
      
      for (uint32_t i = 0; i < num_ct; i++) {
        auto slot_index = loc % N;
        auto bundle_index = loc / N;
        real_item[i] = answer.at(bundle_size * i + bundle_index).at(slot_index);
      }
    } else {
      auto num_slot = pir_parms.get_num_slot();
      auto num_payload_slot = pir_parms.get_num_payload_slot();
      real_item.resize(num_payload_slot);
      
      for (uint32_t i = 0, slot = 0, ct_index = 0; i < num_payload_slot; i++, slot++) {
        if (slot == num_slot) {
          slot = 0;
          ct_index++;
        }
        real_item[i] = answer.at(ct_index).at(slot + loc * num_slot);
      }
    }
    
    return real_item;
  };
  
  // 辅助函数: 将payload slots转换回原始字节数组
  auto slots_to_bytes = [&](const std::vector<uint64_t>& slots) -> Element {
    auto plain_modulus_bit = pir_parms.get_seal_parms().plain_modulus().bit_count();
    size_t bits_per_slot = plain_modulus_bit - 1;
    
    // 获取PIR内部的payload_size
    auto pir_payload_size = pir_parms.get_payload_size();
    
    // 使用PIR内部的payload_size
    Element result(pir_payload_size, 0);
    size_t bit_offset = 0;
    
    for (size_t slot_idx = 0; slot_idx < slots.size(); slot_idx++) {
      uint64_t slot_value = slots[slot_idx];
      
      // 与编码阶段对称：将8888还原为0
      if (slot_value == 8888) {
        slot_value = 0;
      }
      
      size_t bits_in_this_slot = std::min(bits_per_slot, pir_payload_size * 8 - bit_offset);
      
      // 将 slot_value 的低 bits_in_this_slot 位写回字节数组
      for (size_t bit = 0; bit < bits_in_this_slot; bit++) {
        size_t byte_idx = (bit_offset + bit) / 8;
        size_t bit_in_byte = (bit_offset + bit) % 8;
        
        if (byte_idx < pir_payload_size) {
          uint8_t bit_value = (slot_value >> bit) & 1;
          result[byte_idx] |= (bit_value << bit_in_byte);
        }
      }
      
      // 与编码阶段对称：每次增加bits_per_slot
      bit_offset += bits_per_slot;
    }
    
    return result;
  };
  
  timer.reset();
  // 从answer提取每个查询的payload到results
  results.resize(num_query);
  
  for (size_t q_idx = 0; q_idx < batch_query_index.size(); q_idx++) {
    auto q = batch_query_index[q_idx];
    
    // 提取payload slots (每个slot存储部分bits)
    std::vector<uint64_t> payload_slots = extract_payload_slots(q_idx, q);
    
    // 将slots重新组合成原始字节数组
    results[q_idx] = slots_to_bytes(payload_slots);
  }
  auto result_convert_time = timer.elapsed();

  std::cout << "------------------------------------" << std::endl;
  std::cout << "Performance(Default Mode):: " << std::endl;
  std::cout << "Init time: " << init_time << " ms " << std::endl;
  std::cout << "Gen query time: " << query_time << " ms " << std::endl;
  std::cout << "Gen response time: " << response_time << " ms " << std::endl;
  std::cout << "Extract answer time: " << extract_time << " ms "
            << std::endl;
  std::cout << "Result convert time: " << result_convert_time << " ms "
            << std::endl;

  online_time = query_time + response_time + extract_time + result_convert_time;
  std::cout << "Batch PIR online time: " << online_time << " ms " << std::endl;

  std::cout << "Query size: " << query.str().size() / 1024.0 << " KBytes"
            << std::endl;

  std::cout << "Response size: " << response.str().size() / 1024.0 << " KBytes"
            << std::endl;
            
  return results;
}

// ==========================================
// Direct Batch PIR 主流程入口
// ==========================================
std::vector<Element> my_direct_batch_pir_main(
    const uint64_t num_payloads, 
    const uint64_t payload_size,
    const uint64_t num_query, 
    const bool is_batch,     
    const bool is_compress,  
    std::vector<std::vector<uint64_t>> input_db, 
    std::vector<uint32_t> query_indices, 
    uint64_t col_size, 
    double& online_time) {

  std::cout << "Start Direct Batch PIR (Skip Cuckoo)! " << std::endl;
  Timer timer;
  timer.reset();

  // 1. 初始化参数 (Direct Mode)
  uint64_t num_rows = query_indices.size(); 
  
  // 处理 Padding 问题: 确保 DB 大小严格等于 num_rows * col_size
  uint64_t expected_db_size = num_rows * col_size;
  if (input_db.size() < expected_db_size) {
      std::cerr << "Error: Input DB size (" << input_db.size() 
                << ") is smaller than required (" << expected_db_size << ")" << std::endl;
      throw std::runtime_error("Input DB size is too small for Direct PIR!");
  }
  if (input_db.size() > expected_db_size) {
      // 截断多余的 Padding
      // std::cout << "[PIR Warning] Truncating DB from " << input_db.size() 
      //           << " to " << expected_db_size << " (removing padding)" << std::endl;
      input_db.resize(expected_db_size);
  }

  // 使用 Direct Mode 构造函数初始化参数
  PirParms pir_parms(expected_db_size, payload_size, num_rows, col_size);
  
  // 2. 初始化 Client 和 Server
  Client batch_client(pir_parms);
  std::stringstream keys = batch_client.save_keys();
  
  // Server batch_server(pir_parms, false); // false = not random db
  // batch_server.set_database(input_db);   // 填入数据
  // // batch_server.direct_encode_to_ntt_db(); // 调用 Direct 编码
  // batch_server.encode_to_ntt_db(); // 直接调用通用编码
  bool random_db = false;
  Server batch_server(pir_parms, true, random_db, input_db);
  batch_server.set_keys(keys);
  
  auto init_time = timer.elapsed();
  timer.reset();

  // 3. 生成查询 (Direct)
  std::vector<uint32_t> offsets(num_query);
  for(size_t i=0; i<num_query; ++i) {
      offsets[i] = query_indices[i] % col_size;
  }

  std::stringstream query = batch_client.gen_direct_batch_query(offsets);
  auto query_time = timer.elapsed();

  // 4. 生成响应
  timer.reset();
  std::stringstream response = batch_server.gen_direct_batch_response_no_cuckoo(query);
  auto response_time = timer.elapsed();

  // 5. 提取结果 (得到 slots 形式)
  timer.reset();
  std::vector<std::vector<uint64_t>> answer = batch_client.extract_direct_batch_answer_no_cuckoo(response);
  auto extract_time = timer.elapsed();

  // 6. 验证正确性
  test_direct_batch_pir_correctness(batch_server, answer, query_indices, pir_parms);

  // 7. 结果转换 (Slots -> Bytes)
  timer.reset();
  
  // 辅助 lambda: 提取单个查询的 payload slots
  auto extract_payload_slots = [&](uint32_t loc) -> std::vector<uint64_t> {
    std::vector<uint64_t> real_item;
    
    if (!pir_parms.get_is_compress()) {
      // ==============================================
      // 【关键修复】非压缩模式提取逻辑 (与验证函数保持一致)
      // ==============================================
      auto N = pir_parms.get_seal_parms().poly_modulus_degree();
      auto num_ct = pir_parms.get_num_payload_slot();
      auto bundle_size = pir_parms.get_bundle_size();
      auto num_slot = pir_parms.get_num_slot(); // 必须获取这个参数

      real_item.resize(num_ct);
      
      for (uint32_t i = 0; i < num_ct; i++) {
        uint64_t slot_pos = loc * num_slot; // 计算行起始位置
        auto slot_index = slot_pos % N;
        auto bundle_index = slot_pos / N;
        
        real_item[i] = answer.at(bundle_size * i + bundle_index).at(slot_index);
      }
    } else {
      // 压缩模式提取逻辑
      auto num_slot = pir_parms.get_num_slot();
      auto num_payload_slot = pir_parms.get_num_payload_slot();
      real_item.resize(num_payload_slot);
      
      for (uint32_t i = 0, slot = 0, ct_index = 0; i < num_payload_slot; i++, slot++) {
        if (slot == num_slot) {
          slot = 0;
          ct_index++;
        }
        real_item[i] = answer.at(ct_index).at(slot + loc * num_slot);
      }
    }
    return real_item;
  };

  // 辅助 lambda: Slots -> Bytes (保持不变)
  auto slots_to_bytes = [&](const std::vector<uint64_t>& slots) -> Element {
    auto plain_modulus_bit = pir_parms.get_seal_parms().plain_modulus().bit_count();
    size_t bits_per_slot = plain_modulus_bit - 1;
    auto pir_payload_size = pir_parms.get_payload_size();
    
    Element result(pir_payload_size, 0);
    size_t bit_offset = 0;
    
    for (size_t slot_idx = 0; slot_idx < slots.size(); slot_idx++) {
      uint64_t slot_value = slots[slot_idx];
      if (slot_value == 8888) slot_value = 0;
      
      size_t bits_in_this_slot = std::min(bits_per_slot, pir_payload_size * 8 - bit_offset);
      
      for (size_t bit = 0; bit < bits_in_this_slot; bit++) {
        size_t byte_idx = (bit_offset + bit) / 8;
        size_t bit_in_byte = (bit_offset + bit) % 8;
        
        if (byte_idx < pir_payload_size) {
          uint8_t bit_value = (slot_value >> bit) & 1;
          result[byte_idx] |= (bit_value << bit_in_byte);
        }
      }
      bit_offset += bits_per_slot;
    }
    return result;
  };

  // 转换所有结果
  std::vector<Element> results(num_query);
  for (size_t i = 0; i < num_query; i++) {
    // 在 Direct Mode 下，查询 i 的结果就在逻辑位置 i (loc = i)
    std::vector<uint64_t> payload_slots = extract_payload_slots(i);
    results[i] = slots_to_bytes(payload_slots);
  }
  
  auto result_convert_time = timer.elapsed();

  // 8. 打印性能统计
  std::cout << "------------------------------------" << std::endl;
  std::cout << "Performance (Direct Mode): " << std::endl;
  std::cout << "Init time: " << init_time << " ms " << std::endl;
  std::cout << "Gen query time: " << query_time << " ms " << std::endl;
  std::cout << "Gen response time: " << response_time << " ms " << std::endl;
  std::cout << "Extract answer time: " << extract_time << " ms " << std::endl;
  std::cout << "Result convert time: " << result_convert_time << " ms " << std::endl;

  online_time = query_time + response_time + extract_time + result_convert_time;
  std::cout << "Direct Batch PIR online time: " << online_time << " ms " << std::endl;
  std::cout << "Query size: " << query.str().size() / 1024.0 << " KBytes" << std::endl;
  std::cout << "Response size: " << response.str().size() / 1024.0 << " KBytes" << std::endl;

  return results;
}