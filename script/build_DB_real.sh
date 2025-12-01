#!/usr/bin/env bash
set -euo pipefail

# PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && cd .. && pwd)"
# DB_DIR="${PROJECT_DIR}/db_engine"
# YCSB_DIR="${PROJECT_DIR}/workload/YCSB-C"

# cd "${DB_DIR}"
# pwd
# make clean
# make static-musl
# make install-static
# cd "${YCSB_DIR}"
# make clean
# make BUILD=release

# ===========================================================================================
# SELECT_POLICY |       0       |       1       |      2       |       3        |
#               |     worst     |       RR      |  level-aware |   My algorithm |
# ===========================================================================================
# PACKING_TYPE  |          0            |          1            |           2           |
#               |   one key per page    |       hash + mod      |   key range vertical  |
# ===========================================================================================

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && cd .. && pwd)"
DB_DIR="${PROJECT_DIR}/db_engine"
YCSB_DIR="${PROJECT_DIR}/workload/YCSB-C"

cd "${DB_DIR}"
pwd
make clean
make static-musl-variants
make install-static
cd "${YCSB_DIR}"
make clean
make BUILD=release NVME_BACKEND=sim variants
echo "done"
