# env configs
TZ ?= Asia/Taipei
export TZ

BDIR 	:= /tmp/sss-build
GEM5DIR := ./build
M5DIR	:= ${HOME}/m5
LOG_DIR := ./logs


TIME	:= $(shell date +%m%d-%H%M)

export M5_PATH=${M5DIR}

# ISA configs
ISA 	:= X86

DISK_EX	:= ${M5DIR}/disks/disk.img

ifeq (${ISA},X86)
KERNEL	:= x86_64-vmlinux-4.9.92
DISK	:= ${M5DIR}/disks/x86root.img ${DISK_EX}

else
KERNEL	:= aarch64-vmlinux-4.9.92
DISK	:= ${M5DIR}/disks/linaro-aarch64-linux.img
endif

SCRIPT_DIR  := script
SCRIPT_PATH := ${SCRIPT_DIR}/exec_ycsb.sh
SCRIPT_FLAG := --script ${SCRIPT_PATH}

# hardware configs

# CPU Type:
# TimingSimpleCPU
# AtomicSimpleCPU
CPU	:= TimingSimpleCPU
CORES	:= 1
CLK	:= 3GHz
CACHE	:= --caches --l2cache
MEM	:= DDR4_2400_8x8
MEM_GB	:= 4
DUAL	:=

# debug configs
DPRINT_FLAGS	:= M5Print
DEBUG_FLAGS	:= --debug-flags=${DPRINT_FLAGS} --debug-file=debug.txt --listener-mode=on
# DPRINT_FLAGS	:=
# DEBUG_FLAGS	:= 

M5_LOG_SUFFIX	:=
M5_DEBUG_LOG	:= ${LOG_DIR}/${TIME}${M5_LOG_SUFFIX}.debug.log
M5_STAT_LOG	:= ${LOG_DIR}/${TIME}${M5_LOG_SUFFIX}.stat.txt
M5_HOST_LOG	:= ${LOG_DIR}/${TIME}${M5_LOG_SUFFIX}.host.log

REDIR_FLAGS := -d ${LOG_DIR} -r -e  --stderr-file=${M5_DEBUG_LOG}

GDB_BIN		:= gdb
GDB_LOGGING	:= on
_GDB_LOGGING	= "set logging ${GDB_LOGGING}" #"set trace-commands ${GDB_LOGGING}"
GDB_LOG_FILE	:= ${LOG_DIR}/gdb-${TIME}.log
_GDB_LOG_FILE	= "set logging file ${GDB_LOG_FILE}"

GDB_PAGINATION	:= off
_GDB_PAGINATION	= "set pagination ${GDB_PAGINATION}"

GDB_STOP_SIG	:= SIGUSR1
_GDB_STOP_SIG	= "handle ${GDB_STOP_SIG} nopass stop"

GDB_EX_OPTIONS	= -ex ${_GDB_LOG_FILE} -ex ${_GDB_LOGGING} -ex ${_GDB_PAGINATION} -ex ${_GDB_STOP_SIG} -ex "set print object on"
_GDB_EX_OPTIONS = ${GDB_EX_OPTIONS}

# gem5 configs
VARIANT 	:= opt
GEM5_SCRIPT	:=
GEM5_CFG	:= ./configs/example/fs.py $(addprefix --script ,${GEM5_SCRIPT})
SSS_CFG		:= ./src/dev/storage/simplessd/config/sample.cfg

PORT		:= 3456

HW_FLAGS	= --num-cpu=${CORES} --cpu-clock=${CLK} ${CACHE} --cpu-type=${CPU} --mem-size=${MEM_GB}GB --mem-type=${MEM}
SYS_FLAGS	= --kernel=${KERNEL} $(addprefix --disk-image=,${DISK}) ${DUAL} ${HW_FLAGS}
SIMPLESSD_FLAGS	:= --ssd-interface=nvme --ssd-config=${SSS_CFG}


#### config done ####

GEM5_TARGET	= ${GEM5DIR}/${ISA}/gem5.${VARIANT}
GEM5_EXEC_CMD = ${GEM5_TARGET} ${DEBUG_FLAGS} ${GEM5_CFG} ${SYS_FLAGS} ${SIMPLESSD_FLAGS}
GEM5_EXEC_CMD_WITH_SCRIPT = ${GEM5_TARGET} ${DEBUG_FLAGS} ${GEM5_CFG} ${SYS_FLAGS} ${SIMPLESSD_FLAGS} ${SCRIPT_FLAG}
build: setup
	scons ${GEM5_TARGET} -j 8 --ignore-style

run-timing: CPU = TimingSimpleCPU
run-timing: CORES = 1
run-timing: run



run: setup
	echo "M5_PATH at $$M5_PATH"
	touch ${M5_STAT_LOG}
	ln -srf ${M5_STAT_LOG} m5out/stats.txt
	${GEM5_EXEC_CMD} | tee ${M5_DEBUG_LOG}


YCSB_PATH := $(CURDIR)/workload/YCSB-C
# YCSB_SCRIPTS := exec_ycsbc.sh exec_ycsbd.sh exec_ycsbf.sh
YCSB_SCRIPTS := exec_ycsba.sh exec_ycsbb.sh exec_ycsbc.sh exec_ycsbd.sh exec_ycsbf.sh
# YCSB_SCRIPTS := exec_ycsb25.sh exec_ycsb75.sh 

DISK_IMG := $(YCSB_PATH)/test.img
SIMPLESSD_DISK_DIR := $(CURDIR)/cp2m5/disks
SUMMARY_DIR := $(LOG_DIR)/summary
TERMINAL_OUT_DIR := $(CURDIR)/m5out
TERMINAL_OUT := $(TERMINAL_OUT_DIR)/system.pc.com_1.device
TERMIAL_LOG := ${LOG_DIR}/${TIME}${M5_LOG_SUFFIX}.terminal.log

term_log := ${LOG_DIR}/${TIME}${M5_LOG_SUFFIX}.terminal.log

run-script: setup
	echo "M5_PATH at $$M5_PATH"
	touch ${M5_STAT_LOG}
	ln -sf "$(abspath ${M5_STAT_LOG})" m5out/stats.txt
	ln -sf "$(abspath ${term_log})" m5out/system.pc.com_1.device
	${GEM5_EXEC_CMD_WITH_SCRIPT} 2> ${M5_DEBUG_LOG}
# ${GEM5_EXEC_CMD_WITH_SCRIPT} | tee ${M5_DEBUG_LOG}
.PHONY: run-script-5
run-script-5: setup
	@mkdir -p "$(SUMMARY_DIR)"; \
	runid=$$(date +%m%d-%H%M%S); \
	agg="$(SUMMARY_DIR)/MYDB-STAT.$$runid.log"; \
	: > "$$agg"; \
	\
	for s in $(YCSB_SCRIPTS); do \
		tag=$${s%.sh}; \
		ts=$$(date +%m%d-%H%M%S); \
		suffix=".$$tag"; \
		echo "=== [$$tag] reset disk and run (ts=$$ts) ==="; \
		\
		cp -f "$(DISK_IMG)" "$(SIMPLESSD_DISK_DIR)/test.img"; \
		\
		$(MAKE) run-script \
			SCRIPT_PATH="$(SCRIPT_DIR)/$$s" \
			M5_LOG_SUFFIX="$$suffix" \
			TIME="$$ts" \
			|| exit $$?; \
		\
		term_log="$(LOG_DIR)/$$ts$$suffix.terminal.log"; \
		if [ -f "$(TERMINAL_OUT)" ]; then \
			cp -f "$(TERMINAL_OUT)" "$$term_log"; \
		else \
			echo "[WARN] missing terminal out: $(TERMINAL_OUT)" >> "$$term_log"; \
		fi; \
		\
		{ \
			echo "----- ts=$$ts tag=$$tag -----"; \
			grep '\[MYDB-STAT\]' "$$term_log" || true; \
			echo; \
		} >> "$$agg"; \
	done; \
	\
	echo "✅ MYDB-STAT aggregated to: $$agg"



m5term:
	${MAKE} -C util/term
	./util/term/m5term localhost ${PORT}

.PHONY: socat
socat:
	./socat -R ${M5_HOST_LOG} -,raw,echo=0 tcp:localhost:${PORT}

socat-background:
	./socat -u tcp:localhost:${PORT} open:${M5_HOST_LOG},creat,append

gdb:
	${GDB_BIN} -q ${_GDB_EX_OPTIONS} --args ${GEM5_EXEC_CMD}

gdb-stop:
	pkill -${GDB_STOP_SIG} -o $(shell basename ${GEM5_TARGET})

setup:
	mkdir -p "${BDIR}"
	mkdir -p "${LOG_DIR}"
	ln -nsrf "${BDIR}" build
	ln -nsrf cp2m5 "${M5DIR}"
clean:
	# clear all intermediate files
	rm parsetab.py **/*.pyc




