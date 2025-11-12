SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c

# === Directories ===
ROOT              := $(abspath .)
BUILD_DIR_BASE    := build
INSTALL_DIR       := $(abspath ../workload/YCSB-C/mydb)
NVME_PREBUILT_DIR := $(abspath src/db_nvme/lib)
LOG_DIR           ?= logs
LOG_DIR_ABS       := $(abspath $(LOG_DIR))

# Optional toolchain (musl) file
TOOLCHAIN_FILE    := $(abspath toolchains/musl.toolchain.cmake)

# Auto parallel
NPROC             := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# === Build switches (override on CLI) ===
BUILD_TYPE        ?= Debug
ENABLE_SANITIZERS ?= OFF
BUILD_STATIC      ?= OFF
GENERATOR         ?=
RUN_ARGS          ?=
NVME_BACKEND	  ?= sim

# Predefined build dirs for different profiles
DYNAMIC_BUILD_DIR := $(BUILD_DIR_BASE)
ASAN_BUILD_DIR    := $(BUILD_DIR_BASE)-asan
STATIC_BUILD_DIR  := $(BUILD_DIR_BASE)-static

# === Derived ===
ifeq ($(BUILD_STATIC),ON)
  BUILD_DIR := $(STATIC_BUILD_DIR)
else
  ifeq ($(ENABLE_SANITIZERS),ON)
    BUILD_DIR := $(ASAN_BUILD_DIR)
  else
    BUILD_DIR := $(DYNAMIC_BUILD_DIR)
  endif
endif

# Common CMake flags
CMAKE_FLAGS := \
  -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
  -DCMAKE_INSTALL_PREFIX=$(abspath $(INSTALL_DIR)) \
  -DNVME_PREBUILT_DIR=$(NVME_PREBUILT_DIR) \
  -DENABLE_SANITIZERS=$(ENABLE_SANITIZERS) \
  -DBUILD_STATIC=$(BUILD_STATIC) \
  -DNVME_BACKEND=$(NVME_BACKEND)

# === Targets ===
.PHONY: all configure build install run clean rebuild \
        asan glibc default static-musl run-asan run-static print-flags \
        run-log run-asan-log

all: configure configure-static build

print-flags:
	@echo "BUILD_DIR           = $(BUILD_DIR)"
	@echo "ASAN_BUILD_DIR      = $(ASAN_BUILD_DIR)"
	@echo "STATIC_BUILD_DIR    = $(STATIC_BUILD_DIR)"
	@echo "BUILD_TYPE          = $(BUILD_TYPE)"
	@echo "ENABLE_SANITIZERS   = $(ENABLE_SANITIZERS)"
	@echo "BUILD_STATIC        = $(BUILD_STATIC)"
	@echo "NVME_PREBUILT_DIR   = $(NVME_PREBUILT_DIR)"
	@echo "LOG_DIR             = $(LOG_DIR_ABS)"
	@echo "RUN_ARGS            = $(RUN_ARGS)"
	@echo "DNVME_BACKEND       = $(NVME_BACKEND)"

# ---- Profiles ----
# glibc + ASan（動態）建置
# asan:
# 	@$(MAKE) BUILD_TYPE=RelWithDebInfo ENABLE_SANITIZERS=ON BUILD_STATIC=OFF configure build

# run-asan:
# 	@cd "$(ASAN_BUILD_DIR)"; \
# 	  ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:halt_on_error=1 \
# 	  ./mydb_engine $(RUN_ARGS)

# glibc（動態）預設建置
glibc:
	@$(MAKE) BUILD_TYPE=RelWithDebInfo ENABLE_SANITIZERS=OFF BUILD_STATIC=OFF configure build

default: glibc

# musl 靜態最終產物
static-musl:
	@$(MAKE) BUILD_TYPE=Release ENABLE_SANITIZERS=OFF BUILD_STATIC=ON configure-static build

run-static:
	@cd "$(STATIC_BUILD_DIR)"; ./mydb_engine $(RUN_ARGS)

# === Configure ===
.PHONY: configure 
configure:
	@echo "🔧 Configuring to $(BUILD_DIR) ..."
	@mkdir -p "$(BUILD_DIR)"
	@cd "$(BUILD_DIR)" && cmake .. $(GENERATOR) $(CMAKE_FLAGS)

configure-static:
	@echo "🔧 Configuring (musl, static) to $(STATIC_BUILD_DIR) ..."
	@mkdir -p "$(STATIC_BUILD_DIR)"
	@cd "$(STATIC_BUILD_DIR)" && cmake .. $(GENERATOR) $(CMAKE_FLAGS) \
	  -DCMAKE_TOOLCHAIN_FILE="$(TOOLCHAIN_FILE)"

# === Build / Install / Run ===
config-sim:
	@$(MAKE) NVME_BACKEND=sim configure
config-real:
	@$(MAKE) NVME_BACKEND=real configure

build:
	@echo "🛠️  Building $(BUILD_DIR) ..."
	@cmake --build "$(BUILD_DIR)" -- -j"$(NPROC)"

mysim: config-sim
	@echo "🛠️  Building $(BUILD_DIR) ... in my test enviroment"
	
	@cmake --build "$(BUILD_DIR)" -- -j"$(NPROC)"

real: config-real
	@echo "🛠️  Building $(BUILD_DIR) ... in simpleSSD enviroment"
	@cmake --build "$(BUILD_DIR)" -- -j"$(NPROC)"

install:
	@echo "📦 Installing to $(INSTALL_DIR) ..."
	@cmake --install "$(BUILD_DIR)"
install-static:
	@echo "📦 Installing (musl, static) from $(STATIC_BUILD_DIR) to $(INSTALL_DIR) ..."
	@cmake --install "$(STATIC_BUILD_DIR)"

# === Clean / Rebuild ===
clean:
	@echo "🧹 Cleaning build + install ..."
	@rm -rf "$(DYNAMIC_BUILD_DIR)" "$(ASAN_BUILD_DIR)" "$(STATIC_BUILD_DIR)" "$(INSTALL_DIR)"

rebuild: clean all
