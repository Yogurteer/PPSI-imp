#include "pir_parms.h"

#include <map>
#include <unordered_set>

#include "assert.h"
#include "utils.h"

PirParms::PirParms(const uint64_t num_payloads, const uint64_t payload_size)
    : _payload_size(payload_size), _num_query(1) {
  _num_payloads = next_power_of_2(num_payloads);
  uint64_t poly_degree = 8192;
  std::vector<int> coeff_modulus = {56, 56, 24, 24};
  uint64_t plain_prime_len = 31;

  set_seal_parms(poly_degree, coeff_modulus, plain_prime_len);
  _col_size = std::ceil(num_payloads * 1.0 / _seal_parms.poly_modulus_degree());
  if (_num_payloads < _seal_parms.poly_modulus_degree()) {
    _encoding_size = 1;
  } else {
    _encoding_size = calculate_encoding_size(_col_size);
  }

  _num_payload_slot = std::ceil(payload_size * 8.0 / (plain_prime_len - 1));

  _pre_rotate = choose_rotate_parms(poly_degree, _num_payload_slot, _col_size);

  // if n is smaller than the poly degree
  if (std::floor(poly_degree / num_payloads)) {
    // duplicate the selection vector to fill the n slots
    _pre_rotate = std::max(
        _pre_rotate, uint64_t(std::floor(poly_degree * 1.0 / num_payloads)));
  }

  assert(poly_degree % _pre_rotate == 0 && "Wrong parameters selection!");

  _rotate_step = poly_degree / _pre_rotate;
  print_seal_parms();
  print_pir_parms();
};

// index -> cw is regular;
// 初始Batch PIR参数
PirParms::PirParms(const uint64_t num_payloads, const uint64_t payload_size,
                   const uint64_t num_query, const bool is_batch,
                   const bool is_compress, const bool enable_rotate)
    : _num_payloads(num_payloads),
      _payload_size(payload_size),
      _num_query(num_query),
      _is_compress(is_compress),
      _enable_rotate(enable_rotate) {
  // Todo: add rotate version

  assert(is_batch == true && num_query > 1);

  // 原始参数设置
  uint64_t poly_degree = 4096;
  std::vector<int> coeff_modulus = {48, 32, 24};

  // 优化后参数设置,适配大规模Batch PIR,DB size 2^20起步
  // uint64_t poly_degree = 8192;
  // std::vector<int> coeff_modulus = {56, 56, 24, 24};

  uint64_t plain_prime_len = is_compress? 18 : 17;

  set_seal_parms(poly_degree, coeff_modulus, plain_prime_len);

  _num_payload_slot = std::ceil(payload_size * 8.0 / (plain_prime_len - 1));

  get_all_index_hash_result(num_payloads, num_query);

  // _rotate_step = poly_degree / _pre_rotate;
  print_seal_parms();
  print_pir_parms();
}

// 【修正后的 Direct Mode 构造函数】
PirParms::PirParms(const uint64_t num_payloads, const uint64_t payload_size,
                   const uint64_t num_query, const uint64_t direct_col_size)
    : _num_payloads(num_payloads),
      _payload_size(payload_size),
      _num_query(num_query),
      _col_size(direct_col_size),
      _is_compress(true), // Direct Mode 通常开启压缩
      _enable_rotate(false) {

  // 1. 恢复 k=2 (PIRANA 核心)
  // 注意：请确保头文件 pir_parms.h 中的 _hamming_weight 设 2

  // 2. 设置 SEAL 参数 (保持4096不变)
  // uint64_t poly_degree = 4096;
  // std::vector<int> coeff_modulus = {48, 32, 24};
  uint64_t poly_degree = 8192; 
  std::vector<int> coeff_modulus = {56, 56, 24, 24};
  uint64_t plain_prime_len = 18;
  set_seal_parms(poly_degree, coeff_modulus, plain_prime_len);

  _num_payload_slot = std::ceil(payload_size * 8.0 / (plain_prime_len - 1));

  // 3. 设置 PIRANA 结构参数
  uint32_t N = _seal_parms.poly_modulus_degree();

  _table_size = num_query; // 桶的数量 = 查询数量 (行数)
  // 【核心修复】计算需要的 Bundle 数量
  // 如果行数超过 N，我们需要多个密文来承载
  // 例如: 15360 / 8192 = 1.875 -> 需要 2 个 Bundle
  _bundle_size = std::ceil((double)_table_size / N);

  // 【核心修复】计算 Num Slot (步长)
  if (_bundle_size > 1) {
      // 如果使用了多个 Bundle，为了保证跨 Bundle 的对齐，强制 stride 为 1
      // 这意味着:
      // Row 0 -> Bundle 0, Slot 0
      // Row 8192 -> Bundle 1, Slot 0
      _num_slot = 1; 
  } else {
      // 只有 1 个 Bundle 的情况 (小规模)
      // 保持之前的逻辑: 如果极小则 compact，否则按比例对齐
      if (num_query <= N) {
          _num_slot = 1; 
      } else {
          // 这里其实不会执行，因为如果 num_query > N，bundle_size 就会 > 1
          // 但为了逻辑完备保留 fallback
           double ratio = (double)N / num_query;
           uint32_t log2_ratio = 0;
           if (ratio >= 1.0) {
               log2_ratio = std::floor(std::log2(ratio));
           }
           _num_slot = 1 << log2_ratio; 
      }
  }
  
  if (_num_slot == 0) _num_slot = 1;

  // 4. 【核心修复 - 防止 Segfault】手动构建 "完美" 桶结构
  // Server 编码时严重依赖 _bucket 和 _hash_index
  // 必须初始化它们，否则 crash！
  
  std::cout << "Direct Mode: Building deterministic buckets..." << std::endl;
  
  _bucket.resize(_table_size); 
  
  for (uint32_t row = 0; row < _table_size; ++row) {
      for (uint32_t col = 0; col < _col_size; ++col) {
          uint64_t global_idx = row * _col_size + col;
          
          // 边界检查：不要放入超出 num_payloads 的索引
          if (global_idx < _num_payloads) {
              _bucket[row].push_back(global_idx);
              
              // 【关键】Server 依赖这个 map 来反查元素的列位置
              _hash_index[std::to_string(global_idx)] = col;
          }
      }
  }

  // 5. 【核心修复 - 防止 Segfault】初始化 Codeword 索引表
  // k=2 编码必须用到 _cw_index。如果是空的，Server 访问时必定段错误。
  
  _cw_index.resize(_col_size);
  _encoding_size = calculate_encoding_size(_col_size); // 计算 m
  
  for (uint64_t index = 0; index < _col_size; index++) {
    // 预计算每一列对应的 k=2 组合
    _cw_index[index] = get_cw_code_k2(index, _encoding_size);
  }

  // 【验证打印】请确保你在运行日志中能看到下面这几行！
  std::cout << "Direct Mode Params Initialized:" << std::endl;
  std::cout << "  N: " << N << std::endl;
  std::cout << "  Table Size: " << _table_size << std::endl;
  std::cout << "  Bundle Size: " << _bundle_size << " (Expect > 1 for Large Query)" << std::endl; 
  std::cout << "  Num Slot: " << _num_slot << std::endl;
  
  print_seal_parms();
  print_pir_parms();
}

void PirParms::set_seal_parms(uint64_t poly_degree,
                              std::vector<int> coeff_modulus,
                              uint64_t prime_len) {
  _seal_parms = seal::EncryptionParameters(seal::scheme_type::bfv);
  _seal_parms.set_poly_modulus_degree(poly_degree);
  _seal_parms.set_coeff_modulus(
      seal::CoeffModulus::Create(poly_degree, coeff_modulus));
  _seal_parms.set_plain_modulus(
      seal::PlainModulus::Batching(poly_degree, prime_len));
}

void PirParms::print_seal_parms() {
  std::string scheme_name;
  if (_seal_parms.scheme() == seal::scheme_type::bfv)
    scheme_name = "BFV";
  else {
    throw std::invalid_argument("unsupported scheme");
  }

  std::cout << "/" << std::endl;
  std::cout << "|   Encryption parameters: " << std::endl;
  std::cout << "|   scheme: " << scheme_name << std::endl;
  std::cout << "|   poly_modulus_degree: " << _seal_parms.poly_modulus_degree()
            << std::endl;
  /*
    For the BFV scheme print the plain_modulus parameter.
    */
  if (_seal_parms.scheme() == seal::scheme_type::bfv) {
    std::cout << "|   plain_modulus: " << _seal_parms.plain_modulus().value()
              << std::endl;
  }
  /*
  Print the size of the true (product) coefficient modulus.
  */
  std::cout << "|   coeff_modulus size: ";
  std::cout << " (";
  uint64_t total_coeff_modulus = 0;
  auto coeff_modulus = _seal_parms.coeff_modulus();
  std::size_t coeff_modulus_size = coeff_modulus.size();
  for (std::size_t i = 0; i < coeff_modulus_size - 1; i++) {
    std::cout << coeff_modulus[i].bit_count() << " + ";
    total_coeff_modulus += coeff_modulus[i].bit_count();
  }
  std::cout << coeff_modulus.back().bit_count();
  total_coeff_modulus += coeff_modulus.back().bit_count();
  std::cout << ") " << total_coeff_modulus << " bits" << std::endl;

  std::cout << "\\" << std::endl;
};

uint64_t get_bucket_size(std::vector<std::vector<uint32_t>> &bucket) {
  uint64_t max_size = 0;
  for (auto &b : bucket) {
    max_size = std::max(b.size(), max_size);
  }
  return max_size;
}

// Using cuckoo factor to balance between the insert failure and comm cost
// cuckoo table size
// num_slot
// bundle_size
void PirParms::get_all_index_hash_result(const uint64_t num_payloads,
                                         const uint64_t num_query,
                                         const double cuckoo_factor) {
  
  std::cout << "Preprocessing cuckoo hash!" << std::endl;

  uint32_t N = _seal_parms.poly_modulus_degree();
  // If the number of query is large enough, the response utilization rate is
  // high. There is no more space to futher compress.
  if (static_cast<uint32_t>(num_query * cuckoo_factor) >= N) {
    _is_compress = false;
  }

  if (_is_compress == false) {
    // One response ciphertext only has one slot payload in each bucket;
    _bundle_size = 0;
    while (_bundle_size * N <
           static_cast<uint32_t>(num_query * cuckoo_factor)) {
      _bundle_size++;
    }
    _table_size = _bundle_size * N;
    _num_slot = 1;
  } else {
    _table_size = static_cast<uint32_t>(num_query * cuckoo_factor);
    assert(_table_size < N);
    _num_slot = std::floor(N / _table_size);
    // 1. Ensure cuckoo hash table size > cuckoo factor * the number of query
    // 2. Make the most of space as much as possible
    // e.g. N = 4096, query = 256
    // cuckoo table size : B = 256 * 1.5 = 384
    // For compress : using 4096 / 384 = 10 slot to carry result
    // Enlarge the table size to 409 -> low cuckoo hash failure rate and low col
    // size
    // -> low communication cost and low computation cost
    _table_size = std::floor(N / _num_slot);
    _bundle_size = 1;
  }
  auto stash_size = static_cast<uint32_t>(0);

  uint8_t hash_count = 3;
  kuku::item_type hash_seed = kuku::make_item(1, 0);
  uint64_t max_probe = 100;
  kuku::item_type empty_item = kuku::make_item(0xFFFF, 0);
  std::cout << "Pir Cuckoo parameters: " << std::endl;
  // print N _num_slot _bundle_size 
  std::cout << "  Poly modulus degree (N): " << N << std::endl;
  std::cout << "  Number of slots per ciphertext: " << _num_slot << std::endl;
  std::cout << "  Bundle size: " << _bundle_size << std::endl;
  std::cout << "  Table size (B = 1.5L): " << _table_size << std::endl;
  _table = std::make_shared<kuku::KukuTable>(
      _table_size, stash_size, hash_count, hash_seed, max_probe, empty_item);

  _bucket.resize(_table_size);
  for (uint64_t index = 0; index < num_payloads; index++) {
    auto result = _table->all_locations(kuku::make_item(0, index));
    for (auto &position : result) {
      _bucket[position].push_back(index);
      if (_hash_index.count(std::to_string(index * _table_size + position)) !=
          0) {
        std::cout << "hash string error: " << index << std::endl;
      }
      assert(_hash_index.count(
                 std::to_string(index * _table_size + position)) == 0);
      _hash_index[std::to_string(index * _table_size + position)] =
          _bucket[position].size() - 1;
    }
  }

  _col_size = get_bucket_size(_bucket);
  _cw_index.resize(_col_size);
  _encoding_size = calculate_encoding_size(_col_size);
  for (uint64_t index = 0; index < _col_size; index++) {
    _cw_index[index] = get_cw_code_k2(index, _encoding_size);
  }
  std::cout << "Cuckoo hash done!" << std::endl;
}

void PirParms::print_pir_parms() {
  std::cout << "--------------------------------------" << std::endl;
  std::cout << "/" << std::endl;
  std::cout << "|   PIR parameters: " << std::endl;
  std::cout << "|   Number of payloads (n): " << _num_payloads << std::endl;
  std::cout << "|   Payload size (|pl|): " << _payload_size << " Bytes"
            << std::endl;
  std::cout << "|   Number of payload slot: " << _num_payload_slot << std::endl;
  std::cout << "|   Number of query (L): " << _num_query << std::endl;
  std::cout << "|   Col Size: " << _col_size << std::endl;

  std::cout << "|   Hamming weight (k): " << _hamming_weight << std::endl;
  std::cout << "|   Encoding size (m): " << _encoding_size << std::endl;
  std::cout << "\\" << std::endl;
}
