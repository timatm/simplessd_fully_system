#!/bin/bash
set -euo pipefail

EXEC="./ycsbc_pack1_place2_real"
WORKLOAD_DIR="workloads"
WORKLOAD="workloadc.spec"
WORKLOAD_PATH="${WORKLOAD_DIR}/${WORKLOAD}"
mkdir -p /mnt
mountpoint -q /mnt || mount -t ext4 /dev/sdb1 /mnt

cd /mnt

"${EXEC}" -db mydb -threads 1 -runonly -P "${WORKLOAD_PATH}"
# echo "${EXEC} ${WORKLOAD} is done"
m5 exit
