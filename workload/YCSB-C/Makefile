# ===== Build mode switches =====
# 用法：
#   make                 # 等同 BUILD=debug
#   make BUILD=debug     # 調試（動態），可 gdb/ASan
#   make BUILD=debug SAN=asan
#   make BUILD=release   # 發布（musl 靜態）
BUILD ?= debug            # debug | release
SAN   ?= none             # none  | asan
NVME_BACKEND ?= sim       # sim   | real
SP_VALUES := 1 2 3   # SELECT_POLICY
PT_VALUES := 0 1 2   # PACKING_TYPE

ifeq ($(filter $(BUILD),debug release),)
$(warning Unknown BUILD='$(BUILD)', defaulting to debug)
BUILD := debug
endif

MYDB_PREFIX ?= $(abspath mydb)
LOG_DIR := $(abspath log)

# ===== Common flags =====
CPPFLAGS := -I. -Idb -Icore -I/home/timatm/simplessd_fully_system/include -DENABLE_REDIS=0 -DUSE_TBB=0
CPPFLAGS += -I$(MYDB_PREFIX)/include
CXXFLAGS := -std=c++17 -Wall -pthread -mfpmath=sse -msse2
# 產生 .d 依賴檔，增量編譯更穩
DEPFLAGS := -MMD -MP

# MyDB 静态库（固定最后连）
LDLIBS_MYDB := -Wl,--start-group \
	$(MYDB_PREFIX)/lib/libmydb_engine.a \
	$(MYDB_PREFIX)/lib/libmydb_nvme.a \
	$(MYDB_PREFIX)/lib/libfirmware_lib.a \
	$(MYDB_PREFIX)/lib/libnvme.a \
	-Wl,--end-group

# ===== 模式切換 =====
ifeq ($(BUILD),debug)
  # glibc + 動態連結，便於 gdb / ASan
  CXX ?= g++
  CPPFLAGS += -DDEBUG
  CXXFLAGS += -O0 -g -fno-omit-frame-pointer
  LDFLAGS  := -pthread
  ifeq ($(SAN),asan)
    CXXFLAGS += -fsanitize=address
    LDFLAGS  += -fsanitize=address
  endif
  # DEBUG 模式無法使用 real !!!
#   ifeq ($(NVME_BACKEND),real)
#     LDLIBS_SYS += -lnvme -ludev -luuid -ljson-c -lssl -lcrypto
#   endif
else ifeq ($(BUILD),release)
  # musl + 全靜態（部署）
  CXX ?= /home/timatm/x86_64-linux-musl-cross/bin/x86_64-linux-musl-g++
  CPPFLAGS += -DNDEBUG
  CXXFLAGS += -O2
  LDFLAGS  := -static -static-libstdc++ -static-libgcc -pthread -no-pie
  # release 不允許 SAN=asan（ASan 不支援全靜態）
  ifneq ($(SAN),none)
    $(warning SAN ignored in release build)
  endif
  # 若使用 NVMe 實機後端，release 下需提供相容的 .a（你已有）
  ifeq ($(NVME_BACKEND),real)
    LDLIBS_SYS += $(MYDB_PREFIX)/lib/libnvme.a 
  endif
endif


# ===== Sources =====
DISABLED_SRCS := db/redis_db.cc
SUBSRCS_ALL := $(wildcard core/*.cc) $(wildcard db/*.cc)
SUBSRCS     := $(filter-out $(DISABLED_SRCS),$(SUBSRCS_ALL))
OBJECTS     := $(SUBSRCS:.cc=.o)
DEPS        := $(OBJECTS:.o=.d)

TOP_SRCS    := $(wildcard *.cc)
TOP_OBJS    := $(TOP_SRCS:.cc=.o)
TOP_DEPS    := $(TOP_OBJS:.o=.d)

EXEC        := ycsbc

.PHONY: all clean workloadA-log debug release

all: $(EXEC)

debug:
	@$(MAKE) clean
	@$(MAKE) BUILD=debug $(if $(filter asan,$(SAN)),SAN=asan,)

release:
	@$(MAKE) clean
	@$(MAKE) BUILD=release

# 編譯規則（帶 .d 依賴）
%.o: %.cc
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

# 連結：對象檔 + 系統庫 + MyDB 靜態庫
$(EXEC): $(OBJECTS) $(TOP_OBJS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS_SYS) $(LDLIBS_MYDB) -o $@

# # 讀入依賴（若存在）
# -include $(DEPS) $(TOP_DEPS)
 
.PHONY: variants-sim variants-real

# 產生 9 個 ycsbc_SPx_PTy 可執行檔
# $(LDLIBS_SYS)
variants-sim: $(OBJECTS) $(TOP_OBJS)
	@for sp in $(SP_VALUES); do \
	  for pt in $(PT_VALUES); do \
	    exe="ycsbc_pack$${pt}_place$${sp}_$(NVME_BACKEND)"; \
	    echo "=== Linking $$exe (SELECT_POLICY=$${sp}, PACKING_TYPE=$${pt}, NVME_BACKEND=$(NVME_BACKEND)) ==="; \
	    $(CXX) $(LDFLAGS) $^ \
	      $(MYDB_PREFIX)/lib_variants/db_engine_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a \
	      $(MYDB_PREFIX)/lib_variants/db_nvme_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a \
	      $(MYDB_PREFIX)/lib_variants/firmware_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a \
	      -o $$exe; \
	  done; \
	done
	@for sp in 1; do \
	  for pt in 3; do \
	    exe="ycsbc_pack$${pt}_place$${sp}_$(NVME_BACKEND)"; \
	    echo "=== Linking $$exe (SELECT_POLICY=$${sp}, PACKING_TYPE=$${pt}, NVME_BACKEND=$(NVME_BACKEND)) ==="; \
	    $(CXX) $(LDFLAGS) $^ \
	      $(MYDB_PREFIX)/lib_variants/db_engine_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a \
	      $(MYDB_PREFIX)/lib_variants/db_nvme_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a \
	      $(MYDB_PREFIX)/lib_variants/firmware_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a \
	      -o $$exe; \
	  done; \
	done
variants-real: $(OBJECTS) $(TOP_OBJS)
	@for sp in $(SP_VALUES); do \
	  for pt in $(PT_VALUES); do \
	    exe="ycsbc_pack$${pt}_place$${sp}_$(NVME_BACKEND)"; \
	    echo "=== Linking $$exe (SELECT_POLICY=$${sp}, PACKING_TYPE=$${pt}, NVME_BACKEND=$(NVME_BACKEND)) ==="; \
	    $(CXX) $(LDFLAGS) $^ \
	      $(MYDB_PREFIX)/lib_variants/db_engine_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a \
	      $(MYDB_PREFIX)/lib_variants/db_nvme_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a \
		  $(MYDB_PREFIX)/lib/libnvme.a \
	      -o $$exe; \
	  done; \
	done
	@for sp in 1; do \
	  for pt in 3; do \
	    exe="ycsbc_pack$${pt}_place$${sp}_$(NVME_BACKEND)"; \
	    echo "=== Linking $$exe (SELECT_POLICY=$${sp}, PACKING_TYPE=$${pt}, NVME_BACKEND=$(NVME_BACKEND)) ==="; \
	    $(CXX) $(LDFLAGS) $^ \
	      $(MYDB_PREFIX)/lib_variants/db_engine_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a \
	      $(MYDB_PREFIX)/lib_variants/db_nvme_$(NVME_BACKEND)_pack$${pt}_place$${sp}.a \
		  $(MYDB_PREFIX)/lib/libnvme.a \
	      -o $$exe; \
	  done; \
	done
clean:
	$(RM) $(OBJECTS) $(TOP_OBJS) $(EXEC) $(DEPS) $(TOP_DEPS)

workloadA-log: $(EXEC)
	@mkdir -p $(LOG_DIR)
	@ts=$$(date "+%m%d_%H:%M:%S"); \
	out="$(LOG_DIR)/workloadA.$$ts.log"; \
	echo "[ycsbc] writing to $$out"; \
	./ycsbc -db mydb -threads 1 -P workloads/workloada.spec >"$$out" 2>&1
