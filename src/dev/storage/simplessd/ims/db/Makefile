# === Directories ===
BUILD_DIR        := build
INSTALL_DIR      := install
TOOLCHAIN_FILE   := $(abspath toolchains/musl.toolchain.cmake)
NVME_PREBUILT_DIR := $(abspath src/db_nvme/lib)

# === Flags ===
CMAKE_FLAGS := \
	-DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_INSTALL_PREFIX=$(abspath $(INSTALL_DIR)) \
	-DNVME_PREBUILT_DIR=$(NVME_PREBUILT_DIR) \
	-DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_FILE)

# === Default target ===
.PHONY: all
all: configure build

# === Configure project using CMake ===
.PHONY: configure
configure:
	@echo "🔧 Configuring project..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. $(CMAKE_FLAGS)

# === Build the project ===
.PHONY: build
build:
	@echo "🛠️  Building project..."
	cmake --build $(BUILD_DIR) -- -j

# === Run the project executable ===
.PHONY: run
run: build
	@echo "🚀 Running mydb_engine..."
	@$(BUILD_DIR)/mydb_engine

# === Clean build and install outputs ===
.PHONY: clean
clean:
	@echo "🧹 Cleaning build artifacts..."
	rm -rf $(BUILD_DIR) $(INSTALL_DIR)

# === Full rebuild ===
.PHONY: rebuild
rebuild: clean all
