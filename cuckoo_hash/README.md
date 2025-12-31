# Cuckoo Hash 算法实现

这是一个完整的Cuckoo Hash算法的C++实现，支持可配置的哈希函数数量（2、3、4路）和灵活的bucket容量。

## 特性

- **多路哈希**: 支持2-way、3-way和4-way cuckoo hashing
- **可配置bucket容量**: 每个bucket可以容纳多个元素
- **踢出机制**: 当bucket满时自动踢出元素并重新哈希
- **无stash**: 严格的cuckoo hash实现，不使用stash区域
- **模板支持**: 支持任意可哈希的数据类型
- **性能统计**: 提供负载因子和其他统计信息

## 文件结构

```
cuckoo_hash/
├── cuckoo_hash.h           # 主要的Cuckoo Hash类实现
├── test_cuckoo_hash.cpp    # 完整的测试套件
├── Makefile               # 编译配置
└── README.md              # 本文档
```

## 编译和运行

### 编译测试程序

```bash
make test
```

### 运行测试

```bash
./test_cuckoo_hash
```

### 清理编译文件

```bash
make clean
```

## API 使用说明

### 构造函数

```cpp
CuckooHashTable<T>(size_t table_size, size_t way_num = 2, size_t bucket_capacity = 4, size_t max_kick_count = 500)
```

参数说明：
- `table_size`: 哈希表的bucket数量
- `way_num`: 哈希函数数量（2、3或4）
- `bucket_capacity`: 每个bucket能容纳的元素数量
- `max_kick_count`: 最大踢出次数，防止无限循环

### 主要方法

```cpp
// 插入元素
bool insert(const T& value);

// 查找元素
bool contains(const T& value) const;

// 删除元素
bool remove(const T& value);

// 获取负载因子
double load_factor() const;

// 清空哈希表
void clear();

// 打印统计信息
void print_stats() const;
```

### 使用示例

```cpp
#include "cuckoo_hash.h"
#include <iostream>

int main() {
    // 创建一个3-way cuckoo hash表，100个bucket，每个bucket容量为4
    CuckooHashTable<int> table(100, 3, 4);
    
    // 插入元素
    for (int i = 1; i <= 50; ++i) {
        if (table.insert(i)) {
            std::cout << "插入 " << i << " 成功" << std::endl;
        } else {
            std::cout << "插入 " << i << " 失败" << std::endl;
        }
    }
    
    // 查找元素
    if (table.contains(25)) {
        std::cout << "找到元素 25" << std::endl;
    }
    
    // 删除元素
    if (table.remove(25)) {
        std::cout << "删除元素 25 成功" << std::endl;
    }
    
    // 显示统计信息
    table.print_stats();
    
    return 0;
}
```

## 算法原理

### Cuckoo Hashing 基本原理

Cuckoo Hash是一种基于踢出机制的哈希算法：

1. **多个哈希函数**: 每个元素可以被映射到way_num个不同的bucket
2. **踢出机制**: 当目标bucket满时，踢出一个现有元素，为新元素腾出空间
3. **递归重新哈希**: 被踢出的元素会尝试插入到其他可能的bucket中
4. **冲突解决**: 通过有限次数的踢出操作解决冲突

### 实现特点

- **Bucket设计**: 每个bucket可以容纳多个元素，减少踢出频率
- **哈希函数独立性**: 使用不同的种子确保哈希函数的独立性
- **循环检测**: 限制最大踢出次数防止无限循环
- **负载控制**: 监控负载因子，超过阈值时拒绝插入

## 性能特征

### 时间复杂度
- **查找**: O(way_num) - 常数时间
- **插入**: 平均O(1)，最坏情况O(max_kick_count)
- **删除**: O(way_num) - 常数时间

### 空间复杂度
- O(table_size × bucket_capacity)

### 负载因子
- 建议负载因子不超过0.8以获得最佳性能
- 高负载因子会增加踢出次数和插入失败率

## 测试说明

测试套件包含以下测试：

1. **基本操作测试**: 验证插入、查找、删除的正确性
2. **压力测试**: 大量随机数据的插入测试
3. **性能测试**: 插入和查找操作的性能基准
4. **踢出机制测试**: 验证踢出机制的正确性
5. **边界条件测试**: 空表、单元素等边界情况
6. **数据类型测试**: 验证不同数据类型的支持

## 注意事项

1. **哈希函数质量**: 算法性能很大程度上依赖于哈希函数的质量
2. **负载因子控制**: 高负载因子会显著影响性能
3. **参数调优**: 根据具体应用场景调整table_size、bucket_capacity等参数
4. **内存使用**: 每个bucket预分配空间，可能存在内存浪费

## 扩展建议

- 动态扩容机制
- 更复杂的踢出策略
- 支持范围查询
- 并发安全版本