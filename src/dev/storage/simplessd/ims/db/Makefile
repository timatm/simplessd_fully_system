# === Directories ===
BUILD_DIR := build
INSTALL_DIR := install
NVME_PREBUILT_DIR := $(abspath src/db_nvme/lib)

# === Default target ===
all: configure build

# === Configure project using CMake ===
.PHONY: configure
configure:
	@echo "🔧 Configuring project..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$(abspath $(INSTALL_DIR)) \
		-DNVME_PREBUILT_DIR=$(NVME_PREBUILT_DIR)

# === Build the project ===
.PHONY: build
build:
	@echo "🛠️  Building project..."
	cmake --build $(BUILD_DIR) -- -j


# === Clean build and install outputs ===
.PHONY: clean
clean:
	@echo "🧹 Cleaning build artifacts..."
	rm -rf $(BUILD_DIR) $(INSTALL_DIR)

# === Full rebuild ===
.PHONY: rebuild
rebuild: clean all

# ====== Tool =======
# wget https://musl.cc/x86_64-linux-musl-cross.tgz
# tar -xvzf x86_64-linux-musl-cross.tgz