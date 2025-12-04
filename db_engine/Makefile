SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c

# === Directories ===
ROOT              	:= $(abspath .)
BUILD_DIR_BASE    	:= build
INSTALL_DIR       	:= $(abspath ../workload/YCSB-C/mydb)
NVME_PREBUILT_DIR	:= $(abspath src/db_nvme/lib)
LOG_DIR           	?= logs
LOG_DIR_ABS       	:= $(abspath $(LOG_DIR))
LIB_VARIANT_DIR   	:= $(INSTALL_DIR)/lib_variants
FIRMWARE_BUILD_DIR 	= $(BUILD_DIR)/firmware_build
# Optional toolchain (musl) file
TOOLCHAIN_FILE    := $(abspath toolchains/musl.toolchain.cmake)

# Auto parallel
NPROC             := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# === Build switches (override on CLI) ===
BUILD_TYPE        ?= Release
ENABLE_SANITIZERS ?= OFF
BUILD_STATIC      ?= OFF
GENERATOR         ?=
RUN_ARGS          ?=
NVME_BACKEND	  ?= sim
SELECT_POLICY     ?= 3
PACKING_TYPE      ?= 0

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
# NVME_DRIVER: 0 = my NVMe driver, 1 = SimpleSSD NVMe driver
CMAKE_FLAGS := \
  -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
  -DCMAKE_INSTALL_PREFIX=$(abspath $(INSTALL_DIR)) \
  -DNVME_PREBUILT_DIR=$(NVME_PREBUILT_DIR) \
  -DENABLE_SANITIZERS=$(ENABLE_SANITIZERS) \
  -DBUILD_STATIC=$(BUILD_STATIC) \
  -DNVME_BACKEND=$(NVME_BACKEND) \
  -DSELECT_POLICY=$(SELECT_POLICY) \
  -DPACKING_TYPE=$(PACKING_TYPE)

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
asan:
	@$(MAKE) BUILD_TYPE=RelWithDebInfo ENABLE_SANITIZERS=ON BUILD_STATIC=OFF configure build

run-asan:
	@cd "$(ASAN_BUILD_DIR)"; \
	  ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:halt_on_error=1 \
	  ./mydb_engine $(RUN_ARGS)

# glibc（動態）預設建置
glibc:
	@$(MAKE) BUILD_TYPE=RelWithDebInfo ENABLE_SANITIZERS=OFF BUILD_STATIC=OFF configure build

default: glibc

# musl 靜態最終產物
static-musl:
	@$(MAKE) BUILD_TYPE=Release ENABLE_SANITIZERS=OFF BUILD_STATIC=ON configure-static build

static-musl-variants:
	@$(MAKE) BUILD_TYPE=Release ENABLE_SANITIZERS=OFF BUILD_STATIC=ON configure-static
	@$(MAKE) BUILD_TYPE=Release ENABLE_SANITIZERS=OFF BUILD_STATIC=ON NVME_BACKEND=real build-variants-real

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
	@$(MAKE) NVME_BACKEND=sim build-variants-sim

real: config-real
	@$(MAKE) NVME_BACKEND=real build-variants-sim


build-variants-sim:
	@mkdir -p "$(LIB_VARIANT_DIR)"
	@for sp in 1 2 3; do \
	  for pt in 0 1 2; do \
	    echo "=== Building NVME_BACKEND=$(NVME_BACKEND) SELECT_POLICY=$$sp PACKING_TYPE=$$pt ==="; \
	    $(MAKE) SELECT_POLICY=$$sp PACKING_TYPE=$$pt configure; \
	    $(MAKE) build; \
	    out_engine="$(LIB_VARIANT_DIR)/db_engine_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a"; \
	    out_nvme="$(LIB_VARIANT_DIR)/db_nvme_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a"; \
	    out_fw="$(LIB_VARIANT_DIR)/firmware_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a"; \
	    echo "cp libmydb_engine -> $$out_engine"; \
	    echo "cp libmydb_nvme   -> $$out_nvme"; \
	    echo "cp firmware_lib   -> $$out_fw"; \
	    cp "$(BUILD_DIR)/libmydb_engine.a"  "$$out_engine"; \
	    cp "$(BUILD_DIR)/libmydb_nvme.a"    "$$out_nvme"; \
	    cp "$(FIRMWARE_BUILD_DIR)/libfirmware_lib.a" "$$out_fw"; \
	  done; \
	done

build-variants-real:
	@mkdir -p "$(LIB_VARIANT_DIR)"
	@for sp in 1 2 3; do \
	  for pt in 0 1 2; do \
	    echo "=== Building NVME_BACKEND=$(NVME_BACKEND) SELECT_POLICY=$$sp PACKING_TYPE=$$pt ==="; \
	    $(MAKE) SELECT_POLICY=$$sp PACKING_TYPE=$$pt configure; \
	    $(MAKE) build; \
	    out_engine="$(LIB_VARIANT_DIR)/db_engine_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a"; \
	    out_nvme="$(LIB_VARIANT_DIR)/db_nvme_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a"; \
	    echo "cp libmydb_engine -> $$out_engine"; \
	    echo "cp libmydb_nvme   -> $$out_nvme"; \
	    cp "$(BUILD_DIR)/libmydb_engine.a"  "$$out_engine"; \
	    cp "$(BUILD_DIR)/libmydb_nvme.a"    "$$out_nvme"; \
	  done; \
	done

install:
	@echo "📦 Installing to $(INSTALL_DIR) ..."
	@cmake --install "$(BUILD_DIR)"
install-static:
	@echo "📦 Installing (musl, static) from $(STATIC_BUILD_DIR) to $(INSTALL_DIR) ..."
	@cmake --install "$(STATIC_BUILD_DIR)"
install-asan:
	@echo "📦 Installing (musl, static) from $(ASAN_BUILD_DIR) to $(INSTALL_DIR) ..."
	@cmake --install "$(ASAN_BUILD_DIR)"
# === Clean / Rebuild ===
clean:
	@echo "🧹 Cleaning build + install ..."
	@rm -rf "$(DYNAMIC_BUILD_DIR)" "$(ASAN_BUILD_DIR)" "$(STATIC_BUILD_DIR)" "$(INSTALL_DIR)"

rebuild: clean all
