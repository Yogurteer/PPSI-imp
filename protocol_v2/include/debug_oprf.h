#ifndef DEBUG_OPRF_H
#define DEBUG_OPRF_H

#include <vector>
#include <string>
#include <fstream>
#include <iomanip>
#include <map>
#include <openssl/bn.h>

using Element = std::vector<unsigned char>;
using ElementVector = std::vector<Element>;

class OPRFDebugger {
private:
    std::ofstream log_file;
    
    // 将Element转换为十六进制字符串
    std::string element_to_hex(const Element& elem, size_t max_bytes = 16) {
        std::stringstream ss;
        size_t len = std::min(elem.size(), max_bytes);
        for (size_t i = 0; i < len; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)elem[i];
        }
        if (elem.size() > max_bytes) {
            ss << "...";
        }
        return ss.str();
    }
    
    // 将Element转换为BIGNUM的十进制字符串
    std::string element_to_bn_string(const Element& elem) {
        BIGNUM* bn = BN_bin2bn(elem.data(), elem.size(), nullptr);
        char* bn_str = BN_bn2dec(bn);
        std::string result(bn_str);
        OPENSSL_free(bn_str);
        BN_free(bn);
        
        // 只显示前40个字符
        if (result.length() > 40) {
            return result.substr(0, 40) + "...";
        }
        return result;
    }
    
public:
    OPRFDebugger(const std::string& filename = "oprf_debug.log") {
        log_file.open(filename, std::ios::out | std::ios::trunc);
        if (!log_file.is_open()) {
            std::cerr << "无法打开日志文件: " << filename << std::endl;
        }
    }
    
    ~OPRFDebugger() {
        if (log_file.is_open()) {
            log_file.close();
        }
    }
    
    // 记录Receiver的原始输入和OPRF Step 1结果
    void log_receiver_step1(const ElementVector& input, 
                           const ElementVector& oprf_output,
                           const BIGNUM* r_c,
                           const BIGNUM* prime_p) {
        log_file << "========== RECEIVER OPRF STEP 1 ==========" << std::endl;
        log_file << "接收方随机数 r_c: " << BN_bn2dec(r_c) << std::endl;
        log_file << std::endl;
        
        for (size_t i = 0; i < input.size(); ++i) {
            std::string input_str(input[i].begin(), input[i].end());
            log_file << "--- 元素[" << i << "] = \"" << input_str << "\" ---" << std::endl;
            log_file << "  H(y[" << i << "]) (hex): " << element_to_hex(input[i]) << std::endl;
            log_file << "  H(y[" << i << "])^r_c (hex): " << element_to_hex(oprf_output[i]) << std::endl;
            log_file << "  H(y[" << i << "])^r_c (dec): " << element_to_bn_string(oprf_output[i]) << std::endl;
            log_file << std::endl;
        }
        log_file.flush();
    }
    
    // 记录Sender的OPRF Step 2结果和打乱映射
    void log_sender_step2(const ElementVector& receiver_masked,
                         const ElementVector& sender_output,
                         const std::vector<size_t>& shuffle_map,
                         const BIGNUM* r_s,
                         const BIGNUM* prime_p) {
        log_file << "========== SENDER OPRF STEP 2 ==========" << std::endl;
        log_file << "发送方随机数 r_s: " << BN_bn2dec(r_s) << std::endl;
        log_file << std::endl;
        
        log_file << "打乱映射 (shuffle_map):" << std::endl;
        for (size_t i = 0; i < shuffle_map.size(); ++i) {
            log_file << "  位置[" << i << "] <- 原始位置[" << shuffle_map[i] << "]" << std::endl;
        }
        log_file << std::endl;
        
        for (size_t i = 0; i < receiver_masked.size(); ++i) {
            log_file << "--- 打乱前位置[" << i << "] ---" << std::endl;
            log_file << "  接收输入 H(y*)^r_c (hex): " << element_to_hex(receiver_masked[i]) << std::endl;
            log_file << "  接收输入 H(y*)^r_c (dec): " << element_to_bn_string(receiver_masked[i]) << std::endl;
            log_file << std::endl;
        }
        
        log_file << "打乱后输出:" << std::endl;
        for (size_t i = 0; i < sender_output.size(); ++i) {
            log_file << "--- 打乱后位置[" << i << "] (来自原始[" << shuffle_map[i] << "]) ---" << std::endl;
            log_file << "  (H(y*)^r_c)^r_s (hex): " << element_to_hex(sender_output[i]) << std::endl;
            log_file << "  (H(y*)^r_c)^r_s (dec): " << element_to_bn_string(sender_output[i]) << std::endl;
            log_file << std::endl;
        }
        log_file.flush();
    }
    
    // 记录Sender的输入数据和计算的H(x)^r_s
    void log_sender_data(const std::vector<std::pair<Element, Element>>& input_data,
                        const ElementVector& H_x_rs_bytes,
                        const ElementVector& X_prime,
                        const BIGNUM* r_s) {
        log_file << "========== SENDER 原始数据和 H(x)^r_s ==========" << std::endl;
        log_file << "发送方随机数 r_s: " << BN_bn2dec(r_s) << std::endl;
        log_file << std::endl;
        
        for (size_t i = 0; i < input_data.size(); ++i) {
            std::string x_str(input_data[i].first.begin(), input_data[i].first.end());
            std::string v_str(input_data[i].second.begin(), input_data[i].second.end());
            
            log_file << "--- Sender 元素[" << i << "] ---" << std::endl;
            log_file << "  关键字 x = \"" << x_str << "\"" << std::endl;
            log_file << "  值 v = \"" << v_str << "\"" << std::endl;
            log_file << "  H(x)^r_s (hex): " << element_to_hex(H_x_rs_bytes[i]) << std::endl;
            log_file << "  H(x)^r_s (dec): " << element_to_bn_string(H_x_rs_bytes[i]) << std::endl;
            log_file << "  X' = H_1(H(x)^r_s) (hex): " << element_to_hex(X_prime[i]) << std::endl;
            log_file << std::endl;
        }
        log_file.flush();
    }
    
    // 记录Receiver的Step 3结果
    void log_receiver_step3(const ElementVector& sender_output,
                           const ElementVector& H_y_rs,
                           const ElementVector& Y_prime,
                           const BIGNUM* r_c_inv) {
        log_file << "========== RECEIVER OPRF STEP 3 ==========" << std::endl;
        log_file << "接收方 r_c^-1: " << BN_bn2dec(r_c_inv) << std::endl;
        log_file << std::endl;
        
        for (size_t i = 0; i < sender_output.size(); ++i) {
            log_file << "--- 位置[" << i << "] (已打乱) ---" << std::endl;
            log_file << "  接收 (H(y*)^r_c)^r_s (hex): " << element_to_hex(sender_output[i]) << std::endl;
            log_file << "  计算 H(y*)^r_s (hex): " << element_to_hex(H_y_rs[i]) << std::endl;
            log_file << "  计算 H(y*)^r_s (dec): " << element_to_bn_string(H_y_rs[i]) << std::endl;
            log_file << "  Y' = H_1(H(y*)^r_s) (hex): " << element_to_hex(Y_prime[i]) << std::endl;
            log_file << std::endl;
        }
        log_file.flush();
    }
    
    // 验证Receiver本地计算的H(y)^r_s与OPRF结果的一致性
    void verify_receiver_oprf(const ElementVector& receiver_input,
                              const ElementVector& sender_output,
                              const std::vector<size_t>& shuffle_map,
                              const BIGNUM* r_s,
                              const BIGNUM* prime_p,
                              BN_CTX* bn_ctx) {
        log_file << "========== 验证 RECEIVER OPRF 正确性 ==========" << std::endl;
        log_file << std::endl;
        
        // Receiver本地计算H(y)^r_s (使用Sender的r_s)
        std::vector<Element> local_H_y_rs;
        
        for (size_t i = 0; i < receiver_input.size(); ++i) {
            std::string y_str(receiver_input[i].begin(), receiver_input[i].end());
            
            // 计算H(y)
            unsigned char hash[32];
            SHA256(receiver_input[i].data(), receiver_input[i].size(), hash);
            BIGNUM* bn_hash = BN_bin2bn(hash, 32, nullptr);
            BIGNUM* bn_h_y = BN_new();
            BN_mod(bn_h_y, bn_hash, prime_p, bn_ctx);
            if (BN_is_zero(bn_h_y)) {
                BN_one(bn_h_y);
            }
            
            // 计算H(y)^r_s
            BIGNUM* bn_h_y_rs = BN_new();
            BN_mod_exp(bn_h_y_rs, bn_h_y, r_s, prime_p, bn_ctx);
            
            int bn_size = BN_num_bytes(bn_h_y_rs);
            Element h_y_rs(bn_size);
            BN_bn2bin(bn_h_y_rs, h_y_rs.data());
            local_H_y_rs.push_back(h_y_rs);
            
            BN_free(bn_hash);
            BN_free(bn_h_y);
            BN_free(bn_h_y_rs);
        }
        
        // 对本地计算的结果应用相同的打乱
        std::vector<Element> shuffled_local;
        for (size_t i = 0; i < shuffle_map.size(); ++i) {
            shuffled_local.push_back(local_H_y_rs[shuffle_map[i]]);
        }
        
        // 比较
        log_file << "比较本地计算H(y)^r_s(打乱后) vs OPRF结果:" << std::endl;
        bool all_match = true;
        for (size_t i = 0; i < sender_output.size(); ++i) {
            size_t original_idx = shuffle_map[i];
            std::string y_str(receiver_input[original_idx].begin(), receiver_input[original_idx].end());
            
            bool match = (shuffled_local[i] == sender_output[i]);
            all_match = all_match && match;
            
            log_file << "位置[" << i << "] (原始y[" << original_idx << "] = \"" << y_str << "\"): "
                     << (match ? "✓ 匹配" : "✗ 不匹配") << std::endl;
            
            if (!match) {
                log_file << "  本地计算 (hex): " << element_to_hex(shuffled_local[i]) << std::endl;
                log_file << "  OPRF结果 (hex): " << element_to_hex(sender_output[i]) << std::endl;
            }
        }
        
        log_file << std::endl;
        log_file << "总体验证结果: " << (all_match ? "✓ 全部匹配" : "✗ 存在不匹配") << std::endl;
        log_file << std::endl;
        log_file.flush();
    }
    
    // 比较Sender和Receiver中相同关键字的H()^r_s结果
    void compare_common_elements(const std::vector<std::pair<Element, Element>>& sender_data,
                                 const ElementVector& sender_H_x_rs,
                                 const ElementVector& receiver_input,
                                 const ElementVector& receiver_H_y_rs,
                                 const std::vector<size_t>& shuffle_map) {
        log_file << "========== 比较相同关键字的 H()^r_s ==========" << std::endl;
        log_file << std::endl;
        
        // 构建Sender的关键字到H(x)^r_s的映射
        std::map<std::string, Element> sender_map;
        for (size_t i = 0; i < sender_data.size(); ++i) {
            std::string key(sender_data[i].first.begin(), sender_data[i].first.end());
            sender_map[key] = sender_H_x_rs[i];
        }
        
        // 检查Receiver的每个元素
        for (size_t shuffled_idx = 0; shuffled_idx < receiver_input.size(); ++shuffled_idx) {
            size_t original_idx = shuffle_map[shuffled_idx];
            std::string y_str(receiver_input[original_idx].begin(), receiver_input[original_idx].end());
            
            auto it = sender_map.find(y_str);
            if (it != sender_map.end()) {
                // 找到相同的关键字
                const Element& sender_h_rs = it->second;
                const Element& receiver_h_rs = receiver_H_y_rs[shuffled_idx];
                
                bool match = (sender_h_rs == receiver_h_rs);
                
                log_file << "关键字 \"" << y_str << "\" (Receiver打乱后位置[" << shuffled_idx 
                         << "], 原始位置[" << original_idx << "]):" << std::endl;
                log_file << "  Sender   H(x)^r_s (hex): " << element_to_hex(sender_h_rs) << std::endl;
                log_file << "  Receiver H(y)^r_s (hex): " << element_to_hex(receiver_h_rs) << std::endl;
                log_file << "  结果: " << (match ? "✓ 一致" : "✗ 不一致") << std::endl;
                log_file << std::endl;
            }
        }
        
        log_file.flush();
    }
};

#endif // DEBUG_OPRF_H
