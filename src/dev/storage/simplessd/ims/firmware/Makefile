# Makefile for building firmware test with CMake

BUILD_DIR := build
TARGET := test_firmware

.PHONY: all run clean rebuild

# 預設指令：編譯
all:
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake ..
	cd $(BUILD_DIR) && $(MAKE) -j

# 執行
run: all
	@./$(BUILD_DIR)/$(TARGET)

# 清除 build 資料夾
clean:
	@rm -rf $(BUILD_DIR)

# 重編譯（先清除再建）
rebuild: clean all
