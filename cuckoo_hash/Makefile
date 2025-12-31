# Makefile for Cuckoo Hash
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g
TARGET = test_cuckoo_hash
SOURCE = test_cuckoo_hash.cpp
HEADER = cuckoo_hash.h

# 默认目标
all: $(TARGET)

# 编译测试程序
$(TARGET): $(SOURCE) $(HEADER)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE)

# 编译并运行测试
test: $(TARGET)
	./$(TARGET)

# 调试版本
debug: CXXFLAGS += -DDEBUG -O0
debug: $(TARGET)

# 清理编译文件
clean:
	rm -f $(TARGET)

# 重新编译
rebuild: clean all

.PHONY: all test debug clean rebuild