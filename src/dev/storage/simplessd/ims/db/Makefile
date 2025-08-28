# === Directories ===
ROOT              := $(abspath .)
BUILD_DIR_BASE    := build
INSTALL_DIR       := install
NVME_PREBUILT_DIR := $(abspath src/db_nvme/lib)

# Optional toolchain (musl) file
TOOLCHAIN_FILE    := $(abspath toolchains/musl.toolchain.cmake)

# Auto parallel
NPROC             := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# === Build switches (override on CLI) ===
# Examples:
#   make asan             -> glibc + ASan 构建
#   make static-musl      -> musl + 静态构建
#   make run-asan / run-static
BUILD_TYPE        ?= Debug
ENABLE_SANITIZERS ?= OFF      # ON for ASan builds (and must be dynamic)
BUILD_STATIC      ?= OFF      # ON for final static build (musl)
GENERATOR         ?=          # e.g. -G Ninja

# === Derived ===
ifeq ($(BUILD_STATIC),ON)
  BUILD_DIR := $(BUILD_DIR_BASE)-static
else
  ifeq ($(ENABLE_SANITIZERS),ON)
    BUILD_DIR := $(BUILD_DIR_BASE)-asan
  else
    BUILD_DIR := $(BUILD_DIR_BASE)
  endif
endif

# Common CMake flags
CMAKE_FLAGS := \
  -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
  -DCMAKE_INSTALL_PREFIX=$(abspath $(INSTALL_DIR)) \
  -DNVME_PREBUILT_DIR=$(NVME_PREBUILT_DIR) \
  -DENABLE_SANITIZERS=$(ENABLE_SANITIZERS) \
  -DBUILD_STATIC=$(BUILD_STATIC)

# === Targets ===
.PHONY: all configure build install run clean rebuild \
        asan glibc default static-musl run-asan run-static print-flags

all: configure build

print-flags:
	@echo "BUILD_DIR           = $(BUILD_DIR)"
	@echo "BUILD_TYPE          = $(BUILD_TYPE)"
	@echo "ENABLE_SANITIZERS   = $(ENABLE_SANITIZERS)"
	@echo "BUILD_STATIC        = $(BUILD_STATIC)"
	@echo "NVME_PREBUILT_DIR   = $(NVME_PREBUILT_DIR)"

# ---- Profiles ----
# glibc + ASan 调试（建议用于找内存问题）
asan:
	@$(MAKE) BUILD_TYPE=RelWithDebInfo ENABLE_SANITIZERS=ON BUILD_STATIC=OFF configure build

run-asan:
	@ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:halt_on_error=1 \
	$(BUILD_DIR)/mydb_engine

# glibc 默认动态（无 ASan）
glibc:
	@$(MAKE) BUILD_TYPE=RelWithDebInfo ENABLE_SANITIZERS=OFF BUILD_STATIC=OFF configure build

default: glibc

# musl 静态最终产物
static-musl:
	@$(MAKE) BUILD_TYPE=Release ENABLE_SANITIZERS=OFF BUILD_STATIC=ON configure-static build

run-static:
	@$(BUILD_DIR)/mydb_engine

# === Configure ===
.PHONY: configure configure-static
configure:
	@echo "🔧 Configuring (glibc, dynamic) to $(BUILD_DIR) ..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. $(GENERATOR) $(CMAKE_FLAGS)

# 使用 musl toolchain：只在静态模式下调用
configure-static:
	@echo "🔧 Configuring (musl, static) to $(BUILD_DIR) ..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. $(GENERATOR) $(CMAKE_FLAGS) \
	  -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_FILE)

# === Build / Install / Run ===
build:
	@echo "🛠️  Building $(BUILD_DIR) ..."
	@cmake --build $(BUILD_DIR) -- -j$(NPROC)

install:
	@echo "📦 Installing to $(INSTALL_DIR) ..."
	@cmake --install $(BUILD_DIR)

run: build
	@echo "🚀 Running mydb_engine..."
	@$(BUILD_DIR)/mydb_engine

# === Clean / Rebuild ===
clean:
	@echo "🧹 Cleaning build + install ..."
	@rm -rf $(BUILD_DIR_BASE) $(BUILD_DIR_BASE)-asan $(BUILD_DIR_BASE)-static $(INSTALL_DIR)

rebuild: clean all
