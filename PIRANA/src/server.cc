#include "server.h"

#include <assert.h>

#include <random>
#include <fstream>
#include <iostream>

Server::Server(PirParms &pir_parms, bool random_db) : _pir_parms(pir_parms) {
  _context = std::make_unique<seal::SEALContext>(pir_parms.get_seal_parms());
  _evaluator = std::make_unique<seal::Evaluator>(*_context);
  _batch_encoder = std::make_unique<seal::BatchEncoder>(*_context);
  _N = pir_parms.get_seal_parms().poly_modulus_degree();
  _p_modulus = pir_parms.get_seal_parms().plain_modulus().bit_count();
  _pre_rot_steps = pir_parms.get_rotate_step();
  _pre_rotate = pir_parms.get_pre_rotate();
  if (random_db == true) {
    gen_random_db();
    _set_db = true;
    encode_to_ntt_db();  // 只有在生成随机数据库时才自动编码
  } else {
    // 从文件读取时,需要先调用read_db_from_file(),然后手动调用encode_to_ntt_db()
    _set_db = false;
  }
};

Server::Server(PirParms &pir_parms, bool is_batch, bool random_db)
    : _pir_parms(pir_parms) {
  _context = std::make_unique<seal::SEALContext>(pir_parms.get_seal_parms());
  _evaluator = std::make_unique<seal::Evaluator>(*_context);
  _batch_encoder = std::make_unique<seal::BatchEncoder>(*_context);
  _N = pir_parms.get_seal_parms().poly_modulus_degree();
  _p_modulus = pir_parms.get_seal_parms().plain_modulus().bit_count();
  _pre_rot_steps = pir_parms.get_rotate_step();
  _pre_rotate = pir_parms.get_pre_rotate();
  if (random_db == true) {
    gen_random_db();
    _set_db = true;
  } else {
    // Todo: read_db from file
  }
  if (_pir_parms.get_is_compress()) {
    batch_encode_to_ntt_db_with_compress();
  } else {
    batch_encode_to_ntt_db_without_compress();
  }
}

Server::Server(PirParms &pir_parms, bool is_batch, bool random_db, std::vector<std::vector<uint64_t>> input_db)
    : _pir_parms(pir_parms) {
  _context = std::make_unique<seal::SEALContext>(pir_parms.get_seal_parms());
  _evaluator = std::make_unique<seal::Evaluator>(*_context);
  _batch_encoder = std::make_unique<seal::BatchEncoder>(*_context);
  _N = pir_parms.get_seal_parms().poly_modulus_degree();
  _p_modulus = pir_parms.get_seal_parms().plain_modulus().bit_count();
  _pre_rot_steps = pir_parms.get_rotate_step();
  _pre_rotate = pir_parms.get_pre_rotate();
  if (random_db == true) {
    gen_random_db();
    _set_db = true;
  } else {
    _raw_db = input_db;
    _set_db = true;
  }
  if (_pir_parms.get_is_compress()) {
    batch_encode_to_ntt_db_with_compress();
  } else {
    batch_encode_to_ntt_db_without_compress();
  }
}

Server::~Server() {}

void Server::gen_random_db() {
  //_raw_db (n, |pl|)
  auto num_payloads = _pir_parms.get_num_payloads();
  auto plain_modulus = _pir_parms.get_seal_parms().plain_modulus().value();
  auto plain_modulus_bit =
      _pir_parms.get_seal_parms().plain_modulus().bit_count();
  auto num_payload_slot = _pir_parms.get_num_payload_slot();
  uint64_t plain_mask = (1 << (plain_modulus_bit - 1)) - 1;

  // database read for encryption
  // each uint32_t save a plaintext < plain_modulus;
  std::cout << "Generate random database for test!" << std::endl;
  std::cout << "Raw database: " << num_payloads << " , (" << num_payload_slot
            << " * " << plain_modulus_bit - 1 << ") -> "
            << num_payload_slot * (plain_modulus_bit - 1) / 8 << " Bytes"
            << std::endl;
  _raw_db.resize(num_payloads);
  uint64_t pi = 0;
  for (auto &payload : _raw_db) {
    payload.resize(num_payload_slot);
    uint64_t test_i = 0;
    for (auto &data : payload) {
      // unsafe random generate, just for generating a test database
      data = rand() & plain_mask;
      // data = test_i & plain_mask;
      // data = pi & plain_mask;
      data = data == 0 ? 8888 : data;
      // assert for test
      assert(data < plain_modulus && data != 0);

      test_i++;
    }
    pi++;
  }
}

bool Server::read_db_from_file(const std::string& filename) {
  std::ifstream infile(filename, std::ios::binary);
  if (!infile.is_open()) {
    std::cerr << "Failed to open database file: " << filename << std::endl;
    return false;
  }
  
  std::cout << "Reading database from file: " << filename << std::endl;
  
  // 读取元数据 (num_payloads, payload_size_bytes)
  uint64_t file_num_payloads, file_payload_size_bytes;
  infile.read(reinterpret_cast<char*>(&file_num_payloads), sizeof(file_num_payloads));
  infile.read(reinterpret_cast<char*>(&file_payload_size_bytes), sizeof(file_payload_size_bytes));
  
  auto expected_num_payloads = _pir_parms.get_num_payloads();
  
  std::cout << "File metadata: " << file_num_payloads << " payloads, " 
            << file_payload_size_bytes << " bytes per payload" << std::endl;
  std::cout << "Expected: " << expected_num_payloads << " payloads" << std::endl;
  
  if (file_num_payloads != expected_num_payloads) {
    std::cerr << "Database size mismatch!" << std::endl;
    infile.close();
    return false;
  }
  
  // 计算需要的slots数量
  auto plain_modulus_bit = _pir_parms.get_seal_parms().plain_modulus().bit_count();
  auto plain_modulus = _pir_parms.get_seal_parms().plain_modulus().value();
  uint64_t bits_per_slot = plain_modulus_bit - 1;
  uint64_t bits_per_payload = file_payload_size_bytes * 8;
  uint64_t num_payload_slot = (bits_per_payload + bits_per_slot - 1) / bits_per_slot;
  uint64_t plain_mask = (1ULL << bits_per_slot) - 1;
  
  std::cout << "Converting to slots: " << num_payload_slot << " slots per payload" << std::endl;
  std::cout << "Bits per slot: " << bits_per_slot << std::endl;
  
  // 检查是否与预期的slots数量匹配
  auto expected_num_payload_slot = _pir_parms.get_num_payload_slot();
  if (num_payload_slot != expected_num_payload_slot) {
    std::cerr << "Warning: Calculated slots (" << num_payload_slot 
              << ") != expected slots (" << expected_num_payload_slot << ")" << std::endl;
    std::cerr << "Using calculated value: " << num_payload_slot << std::endl;
  }
  
  // 读取原始字节数据
  std::vector<std::vector<unsigned char>> raw_byte_db(file_num_payloads);
  for (auto& payload : raw_byte_db) {
    payload.resize(file_payload_size_bytes);
    infile.read(reinterpret_cast<char*>(payload.data()), file_payload_size_bytes);
  }
  infile.close();
  
  // 转换为 uint64_t slots 格式
  _raw_db.resize(file_num_payloads);
  for (uint64_t i = 0; i < file_num_payloads; i++) {
    _raw_db[i].resize(num_payload_slot);
    
    // 将字节转换为slots (按位打包)
    uint64_t bit_pos = 0;
    for (uint64_t slot_idx = 0; slot_idx < num_payload_slot; slot_idx++) {
      uint64_t value = 0;
      for (uint64_t bit = 0; bit < bits_per_slot && bit_pos < bits_per_payload; bit++, bit_pos++) {
        uint64_t byte_idx = bit_pos / 8;
        uint64_t bit_in_byte = bit_pos % 8;
        uint64_t bit_value = (raw_byte_db[i][byte_idx] >> bit_in_byte) & 1;
        value |= (bit_value << bit);
      }
      // 避免0值 (SEAL要求)
      _raw_db[i][slot_idx] = (value == 0) ? 1 : value;
      
      // 验证值在有效范围内
      if (_raw_db[i][slot_idx] >= plain_modulus) {
        std::cerr << "Error: Converted value " << _raw_db[i][slot_idx] 
                  << " exceeds plain_modulus " << plain_modulus << std::endl;
        return false;
      }
    }
  }
  
  std::cout << "Database loaded and converted successfully: " << file_num_payloads 
            << " payloads, " << num_payload_slot << " slots each" << std::endl;
  _set_db = true;
  return true;
}

// used for PIR single query
void Server::encode_to_ntt_db() {
  assert(_set_db && "Database has not been loaded correctly!");
  std::cout << "Encoding database!" << std::endl;

  auto N = _pir_parms.get_seal_parms().poly_modulus_degree();
  auto n = _pir_parms.get_num_payloads();
  auto repeat_time = N < n ? 1 : N / n;

  auto real_rotate = _pre_rotate / repeat_time;

  uint32_t plaintext_size;
  if (repeat_time != 1) {
    plaintext_size =
        std::ceil(_pir_parms.get_col_size() *
                  _pir_parms.get_num_payload_slot() / (double)_pre_rotate) *
        real_rotate;
  } else {
    plaintext_size =
        _pir_parms.get_col_size() * _pir_parms.get_num_payload_slot();
  }

  _encoded_db.resize(plaintext_size);
  std::vector<uint64_t> plain_vector(N, 0);

  auto half_N = N / 2;

  int rotate_step = 0;
  auto rotate_time = 0;
  if (repeat_time == 1) {
    bool rotate_column = 0;
    for (uint64_t pl_slot_index = 0;
         pl_slot_index < _pir_parms.get_num_payload_slot(); pl_slot_index++) {
      for (uint64_t col_index = 0; col_index < _pir_parms.get_col_size();
           col_index++) {
        seal::Plaintext encoded_plain;
        for (uint32_t i = 0, rotate_index = rotate_step; i < half_N;
             i++, rotate_index++) {
          auto raw_db_index =
              rotate_column ? col_index * N + i + half_N : col_index * N + i;
          plain_vector.at(rotate_index % half_N) =
              _raw_db.at(raw_db_index).at(pl_slot_index);
        }
        for (uint32_t i = 0, rotate_index = rotate_step; i < half_N;
             i++, rotate_index++) {
          auto raw_db_index =
              rotate_column ? col_index * N + i : col_index * N + i + half_N;
          plain_vector.at(rotate_index % half_N + half_N) =
              _raw_db.at(raw_db_index).at(pl_slot_index);
        }

        _batch_encoder->encode(plain_vector, encoded_plain);
        _evaluator->transform_to_ntt_inplace(encoded_plain,
                                             _context->first_parms_id());
        _encoded_db.at(pl_slot_index * _pir_parms.get_col_size() + col_index) =
            encoded_plain;
      }

      rotate_step += _pir_parms.get_rotate_step();
      rotate_step = rotate_step % half_N;
      rotate_time++;
      if (rotate_time % (_pir_parms.get_pre_rotate() / 2) == 0) {
        rotate_column = !rotate_column;
      }
    }
  } else {
    for (uint64_t pl_slot_index = 0; pl_slot_index < plaintext_size;
         pl_slot_index++) {
      seal::Plaintext encoded_plain;
      for (uint32_t i = 0, rotate_index = rotate_step; i < half_N;
           i++, rotate_index++) {
        auto raw_db_index = i % n;
        auto slot = rotate_time + real_rotate * (i / n);

        if (slot >= _pir_parms.get_num_payload_slot()) {
          plain_vector.at(rotate_index % half_N + half_N) = 0;
          continue;
        }
        plain_vector.at(rotate_index % half_N) =
            _raw_db.at(raw_db_index).at(slot);
      }
      for (uint32_t i = 0, rotate_index = rotate_step; i < half_N;
           i++, rotate_index++) {
        auto raw_db_index = i % n;
        auto slot = rotate_time + real_rotate * (i / n) + _pre_rotate / 2;
        if (slot >= _pir_parms.get_num_payload_slot()) {
          plain_vector.at(rotate_index % half_N + half_N) = 0;
          continue;
        }
        plain_vector.at(rotate_index % half_N + half_N) =
            _raw_db.at(raw_db_index).at(slot);
      }
      _batch_encoder->encode(plain_vector, encoded_plain);
      _evaluator->transform_to_ntt_inplace(encoded_plain,
                                           _context->first_parms_id());
      _encoded_db.at(pl_slot_index) = encoded_plain;
      rotate_step += _pir_parms.get_rotate_step();
      rotate_step = rotate_step % half_N;
      rotate_time++;
      if (rotate_time % real_rotate == 0) {
        rotate_step = 0;
        rotate_time += _pre_rotate - real_rotate;
      }
    }
  }
  std::cout << "Encoding END!" << std::endl;
};

// used for batch encode
void Server::batch_encode_to_ntt_db_without_compress() {
  assert(_set_db && "Database has not been loaded correctly!");
  std::cout << "Encode database now!" << std::endl;
  auto db_pt_size = _pir_parms.get_col_size() * _pir_parms.get_bundle_size() *
                    _pir_parms.get_num_payload_slot();
  // (col_size, num_slot)
  auto table = _pir_parms.get_cuckoo_table();
  auto bucket = _pir_parms.get_bucket();

  _encoded_db.resize(db_pt_size); // 每个元素是一个明文多项式，不是一个槽

  std::vector<uint64_t> plain_vector(_N, 0);
  for (uint64_t pl_slot_index = 0;
       pl_slot_index < _pir_parms.get_num_payload_slot(); pl_slot_index++) {
    for (uint64_t col_index = 0; col_index < _pir_parms.get_col_size();
         col_index++) {
      for (uint32_t bundle_index = 0;
           bundle_index < _pir_parms.get_bundle_size(); bundle_index++) {
        seal::Plaintext encoded_plain;
        for (uint64_t i = 0; i < _N; i++) {
          if (col_index < bucket[bundle_index * _N + i].size()) {
            auto index = bucket[bundle_index * _N + i][col_index];
            plain_vector.at(i) = _raw_db.at(index).at(pl_slot_index);
          } else {
            // Dummy slot can't be 0
            // If bundle size > 2, then cause ALL zeros plaintext;
            plain_vector.at(i) = 1;
          }
        }
        _batch_encoder->encode(plain_vector, encoded_plain);
        _evaluator->transform_to_ntt_inplace(encoded_plain,
                                             _context->first_parms_id());
        _encoded_db.at(pl_slot_index * _pir_parms.get_col_size() *
                           _pir_parms.get_bundle_size() +
                       col_index * _pir_parms.get_bundle_size() +
                       bundle_index) = encoded_plain;
      }
    }
  }
}

void Server::batch_encode_to_ntt_db_with_compress() {
  // 1. 删除断言
  // assert(_pir_parms.get_bundle_size() == 1 && ...); // 删掉这行！
  
  assert(_set_db && "Database has not been loaded correctly!");
  std::cout << "Encode database now! (Multi-Bundle Supported)" << std::endl;

  auto compress_num_slot = _pir_parms.get_batch_pir_num_compress_slot();
  auto table_size = _pir_parms.get_table_size();
  uint32_t num_slot = _pir_parms.get_num_slot(); 
  
  // 获取关键的多 Bundle 参数
  auto bundle_size = _pir_parms.get_bundle_size(); 
  
  // 数据库总大小 = 列数 * Payload分片数 * Bundle数
  // 原来的代码少乘了 bundle_size，因为他假设是 1
  auto db_pt_size = _pir_parms.get_col_size() * compress_num_slot * bundle_size;
  
  auto bucket = _pir_parms.get_bucket();

  _encoded_db.resize(db_pt_size);

  // 【核心修改】：不再只申请一个 vector，而是申请一组 vectors
  // plain_vectors[b] 代表第 b 个 bundle 的明文数据
  std::vector<std::vector<uint64_t>> plain_vectors(bundle_size, std::vector<uint64_t>(_N, 1)); // 默认填充 1 (Dummy)

  // 外层循环：遍历 Payload 的每一个分片 (0, 1, 2... 60)
  for (uint64_t pl_slot_index = 0; pl_slot_index < compress_num_slot; pl_slot_index++) {
    
    // 中层循环：遍历每一列 (对于 Direct Mode 这种深桶结构，col_index 也是重要的)
    for (uint64_t col_index = 0; col_index < _pir_parms.get_col_size(); col_index++) {
      
      // 每次处理一个新的 (Payload分片, 列) 组合时，都要重置 plain_vectors 为全 1 (Dummy)
      // 注意：PIRANA 论文建议 dummy 填 1 而不是 0，以避免某些代数攻击或为了区分空桶
      for(auto& vec : plain_vectors) {
          std::fill(vec.begin(), vec.end(), 1); 
      }

      // 内层循环：遍历每一行 (0 ~ 15359)
      for (uint32_t i = 0; i < table_size; i++) {
          
          // 获取这一行对应的 bucket 内容
          // Direct Mode 下，bucket[i] 里装的就是全局数据索引
          if (col_index < bucket.at(i).size()) {
              auto index = bucket.at(i).at(col_index);
              
              // 获取原始数据
              // _raw_db[index] 是一个 vector，存储了切分后的 slots
              // 我们要取第 pl_slot_index 个分片
              // 注意：这里简化了原代码复杂的 (pl_slot_index * num_slot + slot) 逻辑
              // 因为在大规模 Direct Mode 下，num_slot 必须为 1
              
              uint64_t val = 0;
              
              // 这里的取值逻辑要格外小心，保持与数据导入时一致
              // 如果 num_slot == 1:
              if (pl_slot_index < _raw_db.at(index).size()) {
                  val = _raw_db.at(index).at(pl_slot_index);
              }
              
              // 【核心分流逻辑】
              // 计算这一行数据 (Row i) 应该落到哪个 Bundle 的哪个 Slot
              uint64_t raw_pos = i * num_slot; // 实际上就是 i
              uint64_t bundle_idx = raw_pos / _N;
              uint64_t slot_idx = raw_pos % _N;

              // 安全检查，防止越界
              if (bundle_idx < bundle_size && slot_idx < _N) {
                  plain_vectors[bundle_idx][slot_idx] = val;
              }
          }
      }

      // 编码并 NTT 变换
      for (uint32_t b = 0; b < bundle_size; b++) {
          seal::Plaintext encoded_plain;
          _batch_encoder->encode(plain_vectors[b], encoded_plain);
          _evaluator->transform_to_ntt_inplace(encoded_plain, _context->first_parms_id());
          
          // 存入 encoded_db
          // 存储顺序展平：[Payload 0 包含的所有 Bundles] [Payload 1 包含的所有 Bundles] ...
          // 计算偏移量
          uint64_t db_index = (pl_slot_index * _pir_parms.get_col_size() * bundle_size) + 
                              (col_index * bundle_size) + b;
                              
          _encoded_db.at(db_index) = encoded_plain;
      }
    }
  }
}

std::vector<seal::Ciphertext> Server::load_query(
    std::stringstream &query_stream, uint32_t query_ct_size) {
  std::vector<seal::Ciphertext> query(query_ct_size);
  for (auto &x : query) {
    x.load(*_context, query_stream);
  }
  return query;
};

void Server::selection_vector_debug(
    std::vector<seal::Ciphertext> &selection_vectors) {
  uint64_t N = _pir_parms.get_seal_parms().poly_modulus_degree();
  uint64_t s = selection_vectors.size();
  for (uint64_t i = 0; i < s; i++) {
    seal::Plaintext encoded_result;
    if (selection_vectors.at(i).is_ntt_form())
      _evaluator->transform_from_ntt_inplace(selection_vectors.at(i));
    _decryptor->decrypt(selection_vectors.at(i), encoded_result);
    std::vector<uint64_t> result(N, 0);
    _batch_encoder->decode(encoded_result, result);
    for (uint64_t slot = 0; slot < N; slot++) {
      if (result.at(slot) != 0) {
        std::cout << "*Debug* No. selection vectors: " << i << std::endl;
        std::cout << "*Debug* result index: " << slot << " response "
                  << result.at(slot) << std::endl;
      }
    }
  }
}

std::vector<seal::Ciphertext> Server::gen_selection_vector(
    std::vector<seal::Ciphertext> &query) {
  std::vector<seal::Ciphertext> selection_vector;
  uint64_t col_size = _pir_parms.get_col_size();
  selection_vector.resize(col_size);
  uint64_t encoding_size = _pir_parms.get_encoding_size();
  uint64_t col_index = 0;
  if (encoding_size == 1) {
    selection_vector[0] = query[0];
  } else {
    for (uint64_t i_1 = 1; i_1 < encoding_size && col_index < col_size; i_1++) {
      for (uint64_t i_2 = 0; i_2 < i_1 && col_index < col_size; i_2++) {
        multiply(*_context, query.at(i_1), query.at(i_2),
                 selection_vector.at(col_index));
        _evaluator->relinearize_inplace(selection_vector.at(col_index),
                                        _relin_keys);
        col_index++;
      }
    }
  }
  return selection_vector;
}

std::vector<seal::Ciphertext> Server::gen_selection_vector_batch(
    std::vector<seal::Ciphertext> &query) {
  std::vector<seal::Ciphertext> selection_vector;
  auto col_size = _pir_parms.get_col_size();
  auto bundle_size = _pir_parms.get_bundle_size();
  selection_vector.resize(col_size * bundle_size);
  auto encoding_size = _pir_parms.get_encoding_size();
  uint64_t col_index = 0;
  // Todo: now only support hamming weight k = 2
  // Try to support more k
  for (uint64_t i_1 = 1; i_1 < encoding_size && col_index < col_size; i_1++) {
    for (uint64_t i_2 = 0; i_2 < i_1 && col_index < col_size; i_2++) {
      for (uint32_t bundle_index = 0; bundle_index < bundle_size;
           bundle_index++) {
        multiply(*_context, query.at(i_1 * bundle_size + bundle_index),
                 query.at(i_2 * bundle_size + bundle_index),
                 selection_vector.at(col_index * bundle_size + bundle_index));
        _evaluator->relinearize_inplace(
            selection_vector.at(col_index * bundle_size + bundle_index),
            _relin_keys);
        _evaluator->transform_to_ntt_inplace(
            selection_vector.at(col_index * bundle_size + bundle_index));
      }
      col_index++;
    }
  }
  // Todo: test selection vector debug
  // selection_vector_debug(selection_vector);
  return selection_vector;
}

// rotate the selection vector and transform them to ntt form
std::vector<seal::Ciphertext> Server::rotate_selection_vector(
    const std::vector<seal::Ciphertext> &selection_vectors) {
  // Todo: make sure have_done computed correctly
  // Add some explanation comments
  uint64_t have_done = std::max(1, int(_N / _pir_parms.get_num_payloads()));

  uint64_t rot_factor = _pre_rotate / have_done;

  // std::cout << "Repead times in one ciphertext: " << have_done << std::endl;
  // std::cout << "One to n: " << _pre_rotate << std::endl;
  std::vector<seal::Ciphertext> rotated_selection_vectors(
      selection_vectors.size() * rot_factor);

  if (_pre_rotate <= 1) {
    // Don't need to rotate the selection vector
    for (size_t i = 0; i < selection_vectors.size(); i++) {
      _evaluator->transform_to_ntt(selection_vectors[i],
                                   rotated_selection_vectors[i]);
    }
    return rotated_selection_vectors;
  }
  if (have_done == 1) {
    for (uint64_t sv_i = 0; sv_i < selection_vectors.size(); sv_i++) {
      rotated_selection_vectors[sv_i * _pre_rotate] = selection_vectors[sv_i];
      _evaluator->rotate_columns(
          selection_vectors[sv_i], _galois_keys,
          rotated_selection_vectors[sv_i * _pre_rotate + _pre_rotate / 2]);

      // Continuous rotating same steps can use the same Galois key.
      // [0,0,1,0,0,0,0,0]
      // [0,0,0,0,0,0,1,0]
      for (uint64_t j = 0; j < _pre_rotate / 2 - 1; j++) {
        _evaluator->rotate_rows(
            rotated_selection_vectors[sv_i * _pre_rotate + j], -_pre_rot_steps,
            _galois_keys,
            rotated_selection_vectors[sv_i * _pre_rotate + j + 1]);
      }
      for (uint64_t j = 0; j < _pre_rotate / 2 - 1; j++) {
        _evaluator->rotate_rows(
            rotated_selection_vectors[sv_i * _pre_rotate + _pre_rotate / 2 + j],
            -_pre_rot_steps, _galois_keys,
            rotated_selection_vectors[sv_i * _pre_rotate + _pre_rotate / 2 + j +
                                      1]);
      }
    }
  } else {
    // The 'have_done > 1' means poly_degree > n, and floor(poly_degree/n) > 2;
    // The first row and the second row in the BFV ciphertext is same.
    // Don't need to swap rows.
    uint64_t todo = _pre_rotate / have_done;
    assert(selection_vectors.size() == 1);
    rotated_selection_vectors[0] = selection_vectors[0];
    for (uint64_t j = 0; j < todo - 1; j++) {
      _evaluator->rotate_rows(rotated_selection_vectors[j], -_pre_rot_steps,
                              _galois_keys, rotated_selection_vectors[j + 1]);
    }
  }

  // selection_vector_debug(rotated_selection_vectors);
  for (auto &c : rotated_selection_vectors)
    _evaluator->transform_to_ntt_inplace(c);
  return rotated_selection_vectors;
}

std::vector<seal::Ciphertext> Server::mul_database_with_compress(
    const std::vector<seal::Ciphertext> &rotated_selection_vectors) {
  // preprocess the n to be a power of 2
  uint64_t sel_vector_size = _pir_parms.get_col_size();
  uint64_t num_output_ciphers =
      std::ceil((double)_pir_parms.get_num_payload_slot() / _N);

  uint64_t repeat_time = std::max(1, (int)(_N / _pir_parms.get_num_payloads()));

  uint64_t real_rotate = _pre_rotate / repeat_time;
  assert(real_rotate * _pir_parms.get_col_size() ==
         rotated_selection_vectors.size());
  uint64_t total_mul = 0;
  if (repeat_time == 1) {
    total_mul = _pir_parms.get_num_payload_slot();
  } else {
    total_mul =
        std::ceil((double)_pir_parms.get_num_payload_slot() / _pre_rotate) *
        _pre_rotate;
  }

  uint64_t rot = _N / _pre_rotate;

  uint64_t total_rot =
      std::ceil((double)_pir_parms.get_num_payload_slot() / _pre_rotate);

  std::vector<seal::Ciphertext> response(num_output_ciphers);

  // rotate_mul_res_time
  uint64_t mul_count = 0;
  for (uint64_t out_i = 0; out_i < num_output_ciphers; out_i++) {
    for (uint64_t slot_i = 0, slot_len = std::min(rot, total_rot - out_i * rot);
         slot_i < slot_len; slot_i++) {
      seal::Ciphertext mul_result;
      seal::Ciphertext sum_result;

      for (uint64_t mul_i = 0; mul_i < real_rotate && mul_count < total_mul;
           mul_i++, mul_count++) {
        for (uint64_t col_i = 0; col_i < _pir_parms.get_col_size(); col_i++) {
          _evaluator->multiply_plain(
              rotated_selection_vectors.at(col_i * real_rotate + mul_i),
              _encoded_db.at(mul_count * _pir_parms.get_col_size() + col_i),
              mul_result);
          if (col_i == 0 && mul_i == 0) {
            sum_result = mul_result;
          } else {
            _evaluator->add_inplace(sum_result, mul_result);
          }
        }
      }
      _evaluator->transform_from_ntt_inplace(sum_result);
      if (slot_i == 0) {
        response.at(out_i) = sum_result;
      } else {
        _evaluator->add_inplace(response.at(out_i), sum_result);
      }
      _evaluator->rotate_rows_inplace(response.at(out_i), 1, _galois_keys);
    }
    _evaluator->mod_switch_to_inplace(response.at(out_i),
                                      _context->last_parms_id());
    try_clear_irrelevant_bits(_context->last_context_data()->parms(),
                              response.at(out_i));
  }
  return response;
};

std::stringstream Server::inner_product(
    const std::vector<seal::Ciphertext> &selection_vector) {
  std::stringstream result;
  auto col_size = _pir_parms.get_col_size();
  auto bundle_size = _pir_parms.get_bundle_size();
  auto num_ct = _pir_parms.get_batch_pir_num_compress_slot();

  std::vector<seal::Ciphertext> multi_add_res(num_ct * bundle_size);
  std::vector<seal::Ciphertext> result_cipher(num_ct * bundle_size);

  std::vector<seal::Ciphertext> tmp_multi_res(col_size);
  for (uint32_t ct = 0; ct < num_ct; ct++) {
    for (uint32_t bundle = 0; bundle < bundle_size; bundle++) {
      for (uint32_t col = 0; col < col_size; col++) {
        // encode_db (num_plot, col_size, bundle_size)
        uint32_t plain_index =
            ct * bundle_size * col_size + bundle + col * bundle_size;
        uint32_t sv_index = bundle + bundle_size * col;
        _evaluator->multiply_plain(selection_vector.at(sv_index),
                                   _encoded_db.at(plain_index),
                                   tmp_multi_res.at(col));
      }
      _evaluator->add_many(tmp_multi_res,
                           multi_add_res.at(ct * bundle_size + bundle));
      _evaluator->transform_from_ntt_inplace(
          multi_add_res.at(ct * bundle_size + bundle));
      _evaluator->mod_switch_to_inplace(
          multi_add_res.at(ct * bundle_size + bundle),
          _context->last_parms_id());
      try_clear_irrelevant_bits(_context->last_context_data()->parms(),
                                multi_add_res.at(ct * bundle_size + bundle));
    }
  }
  for (auto &i : multi_add_res) {
    i.save(result);
  }
  return result;
}

std::stringstream Server::my_inner_product(
    const std::vector<seal::Ciphertext> &selection_vector) {
    
  std::cout << "Server: Executing Inner Product..." << std::endl;
  
  auto bundle_size = _pir_parms.get_bundle_size();
  auto col_size = _pir_parms.get_col_size();
  auto compress_num_slot = _pir_parms.get_batch_pir_num_compress_slot();
  std::stringstream response;

  // 遍历 Payload 分片
  for (uint64_t pl = 0; pl < compress_num_slot; pl++) {
      
      // 遍历每一个 Bundle (这是为了支持 Direct Mode 大规模数据的核心循环)
      for (uint32_t b = 0; b < bundle_size; b++) {
          
          seal::Ciphertext sum;
          bool first = true;

          // 遍历每一列 (Column)
          for (uint64_t col = 0; col < col_size; col++) {
              
              // 1. 获取对应的选择向量
              // Selection Vector Layout: [Col0_B0, Col0_B1...], [Col1_B0...]
              // 这与我们 gen_selection_vector_batch 的输出是一致的
              uint64_t sel_idx = col * bundle_size + b;
              
              // 2. 获取对应的数据库分片
              // Encoded DB Layout: [Payload][Col][Bundle]
              // 这与我们在 batch_encode...with_compress 中写的顺序是一致的
              uint64_t db_idx = (pl * col_size * bundle_size) + (col * bundle_size) + b;

              // 3. 乘法累加 (Ciphertext * Plaintext)
              seal::Ciphertext prod;
              
              // 原生 gen_selection_vector_batch 生成的是 NTT 形式的密文
              // 我们的 _encoded_db 也是 NTT 形式的明文
              // 所以直接 multiply_plain 是安全的！
              _evaluator->multiply_plain(selection_vector[sel_idx], _encoded_db[db_idx], prod);

              if (first) {
                  sum = prod;
                  first = false;
              } else {
                  _evaluator->add_inplace(sum, prod);
              }
          }

          // 4. 后处理：转回系数域并降噪
          _evaluator->transform_from_ntt_inplace(sum);
          
          // 确保降噪到最后一层 (适配 Client 的解密预期)
          while (sum.parms_id() != _context->last_parms_id()) {
               _evaluator->mod_switch_to_next_inplace(sum);
          }

          sum.save(response);
      }
  }
  return response;
}

std::stringstream Server::gen_response(std::stringstream &query_stream) {
  std::stringstream response;
  std::vector<seal::Ciphertext> query =
      load_query(query_stream, _pir_parms.get_encoding_size());

  std::vector<seal::Ciphertext> selection_vector = gen_selection_vector(query);
  std::vector<seal::Ciphertext> rotated_selection_vectors =
      rotate_selection_vector(selection_vector);

  std::vector<seal::Ciphertext> response_cipher =
      mul_database_with_compress(rotated_selection_vectors);

  for (auto &r : response_cipher) {
    r.save(response);
  }
  return response;
};

std::stringstream Server::gen_batch_response(std::stringstream &query_stream) {
  std::vector<seal::Ciphertext> query =
      load_query(query_stream,
                 _pir_parms.get_encoding_size() * _pir_parms.get_bundle_size());

  std::vector<seal::Ciphertext> selection_vector =
      gen_selection_vector_batch(query);
  std::stringstream response;
  response = inner_product(selection_vector);
  return response;
};

std::stringstream Server::gen_direct_batch_response_no_cuckoo(std::stringstream &query_stream) {
  std::cout << "Server: Generating Direct Batch Response (Direct Mode)..." << std::endl;

  auto bundle_size = _pir_parms.get_bundle_size();
  auto encoding_size = _pir_parms.get_encoding_size(); 
  auto compress_num_slot = _pir_parms.get_batch_pir_num_compress_slot();
  auto col_size = _pir_parms.get_col_size();

  // 1. 加载所有查询 (大小: encoding_size * bundle_size)
  // Client 布局: [m0_b0, m0_b1, m1_b0, m1_b1, ..., m(m-1)_b0, m(m-1)_b1]
  std::vector<seal::Ciphertext> all_queries = 
      load_query(query_stream, encoding_size * bundle_size);

  // 2. 生成选择向量 (使用完整的查询，不切分)
  // gen_selection_vector_batch 期望输入: encoding_size * bundle_size
  // 输出: col_size * bundle_size
  std::vector<seal::Ciphertext> selection_vector = 
      gen_selection_vector_batch(all_queries);

  std::stringstream response;

  // 3. 计算内积 (遍历 Payload 分片 -> Bundle -> 列)
  // 输出顺序: [Payload0_Bundle0, Payload0_Bundle1, ..., Payload1_Bundle0, ...]
  for (uint64_t pl = 0; pl < compress_num_slot; pl++) {
      
      // 遍历每一个 Bundle
      for (uint32_t b = 0; b < bundle_size; b++) {
          
          seal::Ciphertext sum;
          bool first = true;
          
          // 遍历每一列
          for (uint64_t col = 0; col < col_size; col++) {
              
              // 【关键修复】选择向量的索引
              // selection_vector 布局: [Col0_B0, Col0_B1, Col1_B0, Col1_B1, ...]
              // 对于第 col 列的第 b 个 Bundle: col * bundle_size + b
              uint64_t sel_idx = col * bundle_size + b;
              
              // 数据库索引
              // DB 布局: [Payload][Col][Bundle]
              uint64_t db_idx = (pl * col_size * bundle_size) + (col * bundle_size) + b;
              
              seal::Ciphertext prod;
              _evaluator->multiply_plain(selection_vector[sel_idx], _encoded_db[db_idx], prod);
              
              if (first) {
                  sum = prod;
                  first = false;
              } else {
                  _evaluator->add_inplace(sum, prod);
              }
          }
          
          // 后处理: NTT 逆变换 + 模数切换
          _evaluator->transform_from_ntt_inplace(sum);
          while (sum.parms_id() != _context->last_parms_id()) {
               _evaluator->mod_switch_to_next_inplace(sum);
          }
          
          sum.save(response);
      }
  }
  
  return response;
}