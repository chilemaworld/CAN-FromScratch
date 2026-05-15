TARGET   := can_parser_demo
CC       := g++
CFLAGS   := -Wall -Wextra -O2 -std=c++11 -pthread

SRC_DIR  := src
INC_DIR  := include
OBJ_DIR  := obj
GEN_HPP  := $(INC_DIR)/generated_can_network.hpp

# 1. 自动扫描所有的 .cpp 文件
SRCS     := $(wildcard $(SRC_DIR)/*.cpp)
# 2. 将所有的 .cpp 映射到 obj/ 目录下的 .o 文件
OBJS     := $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# 核心伪目标
.PHONY: all clean run

# 🌟 终极入口：all 必须依赖最终的可执行文件 $(TARGET)
all: $(TARGET)

# 🤖 核心规则 1：生成的头文件依赖于 DBC 矩阵和 Python 脚本
$(GEN_HPP): protocol/test_matrix.dbc scripts/dbc_codegen.py
	@echo "🤖 Running DBC Code Generator..."
	@mkdir -p $(INC_DIR)
	python3 scripts/dbc_codegen.py

# 🏗️ 核心规则 2：最终的可执行文件，依赖于所有的 .o 目标文件
# 这里把 $(GEN_HPP) 放在前面，强制要求链接前必须先确保头文件生成完毕！
$(TARGET): $(GEN_HPP) $(OBJS)
	@echo "🔗 Linking final executable: $(TARGET)..."
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

# 📦 核心规则 3：编译每个 .cpp 文件到 .o 文件
# 注意：每个 .cpp 的编译都隐式依赖 $(GEN_HPP)，防止 main.cpp 编译时找不到头文件
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp $(GEN_HPP)
	@mkdir -p $(OBJ_DIR)
	@echo "🧱 Compiling $< -> $@..."
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# 🧹 清理缓存，把生成的头文件一并干掉
clean:
	@echo "🧹 Cleaning up engineering environment..."
	rm -rf $(OBJ_DIR) $(TARGET) $(GEN_HPP)