#include "sender.h"
#include "receiver.h"
#include "utils.h"
#include "OT.h"

// 初始化数据集
void init_datasets(Sender& sender, Receiver& receiver) {
    // 发送方数据 (X,V)：x_i = [i, 0x01, 0x02, 0x03]，v_i = [i+10, 0x04, 0x05, 0x06]
    std::vector<std::pair<Element, Element>> sender_data;
    for (int i = 0; i < 10; ++i) {
        Element x = {static_cast<unsigned char>(i), 0x01, 0x02, 0x03};
        Element v = {static_cast<unsigned char>(i + 10), 0x04, 0x05, 0x06};
        sender_data.push_back({x, v});
        // 打印发送方原始数据（方便后续对比）
        // std::cout << "发送方 x" << i << "：";
        // for (auto b : x) printf("%02X ", b);
        // std::cout << " | v" << i << "：";
        // for (auto b : v) printf("%02X ", b);
        // std::cout << "\n";
    }
    sender.set_input(sender_data);
    
    // 接收方数据 Y：y_i = x_i（i=0-4，预期交集）
    ElementVector receiver_data;
    for (int i = 0; i < 5; ++i) {
        Element y = {static_cast<unsigned char>(i), 0x01, 0x02, 0x03};
        receiver_data.push_back(y);
        // 打印接收方原始数据
        // std::cout << "接收方 y" << i << "：";
        // for (auto b : y) printf("%02X ", b);
        // std::cout << "\n";
    }
    receiver.set_input(receiver_data);
    
    std::cout << "\n发送方大小: " << sender.get_input_size() << "\n";
    std::cout << "接收方大小: " << receiver.get_input_size() << "\n\n";
}

// 辅助函数：打印Element的十六进制（方便调试）
void print_element(const std::string& name, const Element& elem) {
    std::cout << name << "：";
    for (auto b : elem) printf("%02X ", b);
    std::cout << "\n";
}

// 更新后的验证函数（匹配新的哈希策略）
void verify_sub_bucket_in_main(const Sender& sender, const Receiver& receiver, 
                             const HashBuckets& buckets, const Element& x_prime, 
                             size_t matched_main_idx) {
    const auto& X_sub_star = sender.get_X_sub_star();
    const auto& Y_sub_star = receiver.get_Y_sub_star();
    int nh = buckets.get_sub_nh();

    // 1. 找到发送方x_prime在匹配主桶下的子桶索引（Cuckoo哈希，只在1个子桶）
    size_t sender_sub_idx = static_cast<size_t>(-1);
    const auto& sender_sub_buckets = X_sub_star[matched_main_idx];
    for (size_t sub_idx = 0; sub_idx < sender_sub_buckets.size(); ++sub_idx) {
        const auto& sub_elements = sender_sub_buckets[sub_idx];
        if (sub_elements.empty()) continue;
        
        // 检查是否包含当前x_prime
        for (const auto& data : sub_elements) {
            if (data.x_prime == x_prime) {
                sender_sub_idx = sub_idx;
                break;
            }
        }
        if (sender_sub_idx != static_cast<size_t>(-1)) break;
    }
    
    if (sender_sub_idx == static_cast<size_t>(-1)) {
        std::cout << "   ❌ 发送方在匹配主桶下未找到子桶！\n";
        return;
    }

    // 2. 计算接收方x_prime的nh个候选子桶（Simple哈希）
    std::vector<size_t> receiver_candidates(nh);
    for (int h = 0; h < nh; ++h) {
        receiver_candidates[h] = buckets.compute_sub_hash_bucket(x_prime, h);
    }

    // 3. 检查发送方的子桶是否在接收方的候选集中
    bool sub_matched = std::find(receiver_candidates.begin(), 
                                receiver_candidates.end(), 
                                sender_sub_idx) != receiver_candidates.end();

    // 4. 验证接收方确实在该子桶中存储了元素
    if (sub_matched) {
        const auto& receiver_sub_bucket = Y_sub_star[matched_main_idx][sender_sub_idx];
        bool element_exists = std::find(receiver_sub_bucket.begin(), 
                                       receiver_sub_bucket.end(), 
                                       x_prime) != receiver_sub_bucket.end();
        
        std::cout << "   发送方子桶索引：" << sender_sub_idx << "\n";
        std::cout << "   接收方" << nh << "个候选子桶：";
        for (size_t idx : receiver_candidates) std::cout << idx << " ";
        std::cout << "\n";
        
        if (element_exists) {
            std::cout << "   ✅ 子桶匹配成功\n";
        } else {
            std::cout << "   ❌ 子桶索引匹配但元素不存在\n";
        }
    } else {
        std::cout << "   ❌ 子桶匹配失败（发送方子桶不在接收方候选集中）\n";
    }
}

void verify_hash_matching(const Sender& sender, const Receiver& receiver, 
                         const HashBuckets& buckets) {
    const auto& X_star = sender.get_X_star();
    const auto& Y_star = receiver.get_Y_star();
    const auto& X_prime = sender.get_X_prime();
    const auto& Y_prime = receiver.get_Y_prime();

    std::cout << "\n=== 验证主桶匹配（接收方Cuckoo vs 发送方Simple） ===\n";
    bool all_matched = true;

    for (size_t j = 0; j < Y_prime.size(); ++j) {
        const auto& y_prime = Y_prime[j];
        
        // 1. 找到接收方y_prime所在的主桶（Cuckoo哈希，只在1个桶）
        size_t receiver_main_idx = static_cast<size_t>(-1);
        for (size_t main_idx = 0; main_idx < Y_star.size(); ++main_idx) {
            const auto& bucket = Y_star[main_idx];
            if (std::find(bucket.begin(), bucket.end(), y_prime) != bucket.end()) {
                receiver_main_idx = main_idx;
                break;
            }
        }
        
        if (receiver_main_idx == static_cast<size_t>(-1)) {
            std::cout << "❌ 接收方y" << j << "未找到所在主桶！\n";
            all_matched = false;
            continue;
        }

        // 2. 找到发送方匹配的x_prime
        bool found = false;
        for (size_t i = 0; i < X_prime.size(); ++i) {
            const auto& x_prime = X_prime[i];
            if (x_prime != y_prime) continue;

            // 3. 计算发送方x_prime的3个候选主桶（Simple哈希）
            std::vector<size_t> sender_candidates(3);
            for (int h = 0; h < 3; ++h) {
                sender_candidates[h] = buckets.compute_simple_hash_bucket(x_prime, h);
            }

            // 4. 检查接收方主桶是否在发送方候选集中
            bool main_matched = std::find(sender_candidates.begin(), 
                                        sender_candidates.end(), 
                                        receiver_main_idx) != sender_candidates.end();

            std::cout << "\n交集元素：x" << i << " <-> y" << j << "\n";
            std::cout << "发送方3个候选主桶：";
            for (size_t idx : sender_candidates) std::cout << idx << " ";
            std::cout << "\n接收方主桶：" << receiver_main_idx << "\n";

            if (main_matched) {
                std::cout << "✅ 主桶匹配成功\n";
                verify_sub_bucket_in_main(sender, receiver, buckets, x_prime, receiver_main_idx);
                found = true;
            } else {
                std::cout << "❌ 主桶匹配失败\n";
                all_matched = false;
            }
            break;
        }

        if (!found) {
            std::cout << "❌ 接收方y" << j << "未找到匹配的发送方元素！\n";
            all_matched = false;
        }
    }

    std::cout << (all_matched ? "\n=== ✅ 所有匹配通过！===" : "\n=== ❌ 存在匹配失败 ===") << "\n";
}

// 执行DH-OPRF+PRP阶段
void phase1(Sender& sender, Receiver& receiver) {
    // 接收方计算H(y_i)^r_c
    ElementVector step1_result = receiver.dh_oprf_step1();
    
    // 发送方PRP打乱并计算(H(y_i*)^r_c)^r_s
    ElementVector step2_result = sender.dh_oprf_step2(step1_result);
    
    // 接收方计算最终的Y'
    receiver.dh_oprf_step3(step2_result);

    // 发送方计算对应的X'
    sender.compute_X_prime();

    std::cout << "Test1 complete!\n";

}

// 执行哈希桶阶段
void phase2(Sender& sender, Receiver& receiver) {
    // 初始化哈希桶：指定主桶基于接收方数据量，子桶数量10，子桶哈希函数数量3
    HashBuckets buckets(receiver.get_input_size(), 15, 3);
    
    // 发送方：使用simple hash
    sender.hash_buckets_phase(buckets);
    std::cout << "发送方simple hash完成\n";
    
    // 接收方：使用cuckoo hash
    receiver.hash_buckets_phase(buckets);
    std::cout << "接收方cuckoo hash完成\n";

    // 子桶哈希阶段（多路cuckoo hash）
    sender.sub_hash_buckets_phase(buckets);
    std::cout << "发送方子桶哈希完成\n";

    receiver.sub_hash_buckets_phase(buckets);
    std::cout << "接收方子桶哈希完成\n";
    
    std::cout << "主桶数量: " << sender.get_X_star().size() << "\n";
    std::cout << "每个主桶子桶数量: " << buckets.get_sub_bucket_count() << "\n";
    std::cout << "子桶哈希函数数量(nh): " << buckets.get_sub_nh() << "\n";
    std::cout << "Test2 complete!\n";

    verify_hash_matching(sender, receiver, buckets);
}

// wty实现版本,DH-OPRF+PRP+Cuckoo/Simple Hash+Store values
int test_v0() {
        // 初始化
        PSI crypto;
        Sender sender(&crypto);
        Receiver receiver(&crypto);
        
        // Sys init
        
        // 发送方数据 (X,V)
        std::vector<std::pair<Element, Element>> sender_data;
        for (int i = 0; i < 10; ++i) {
            Element x = {static_cast<unsigned char>(i), 0x01, 0x02, 0x03};
            Element v = {static_cast<unsigned char>(i + 10), 0x04, 0x05, 0x06};
            sender_data.push_back({x, v});
        }
        sender.set_input(sender_data);
        
        // 接收方数据 Y 
        ElementVector receiver_data;
        for (int i = 0; i < 5; ++i) {
            Element y = {static_cast<unsigned char>(i), 0x01, 0x02, 0x03};
            receiver_data.push_back(y);
        }
        receiver.set_input(receiver_data);
        
        std::cout << "发送方大小: " << sender.get_input_size() << "\n";
        std::cout << "接收方大小: " << receiver.get_input_size() << "\n";
        
        // DH-OPRF+PRP
        phase1(sender, receiver);
        
        // Hash buckets+Store values
        phase2(sender, receiver);
        
        std::cout << "\n完成目前测试\n";
        
        // ...
        
        return 0;
}

// need fix
int test_v1() {
    // S1 实例化对象
    PSI crypto;
    Sender sender(&crypto);
    Receiver receiver(&crypto);
    
    // S2 初始化数据集
    init_datasets(sender, receiver);
    
    // S3 DH-OPRF+PRP
    phase1(sender, receiver);
    
    // S4 Batch Keyword PIRANA
    phase2(sender, receiver);

    // S5 t times 1-n OT extension

    // S6 decry and charge
    
    std::cout << "\n完成目前测试\n";

    return 0;
}

int main() {
    // test_v0();
    // test_v1();
    test1();
    return 0;
}