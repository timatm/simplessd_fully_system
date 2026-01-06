#!/bin/bash
set -euo pipefail
trap 'echo "[ERROR] line=$LINENO cmd=$BASH_COMMAND exit=$?" >&2' ERR
set -x



DOCKER_DIR="/ws"
YCSB_DIR="${DOCKER_DIR}/workload/YCSB-C"
DISK_DIR="${DOCKER_DIR}/cp2m5/disk"
EXEC="./ycsbc_pack1_place2_sim"

cd "${YCSB_DIR}"
# ./experient_build.sh "${EXEC}"
set +e
./experient_build.sh "${EXEC}"
rc=$?
set -e
echo "experient_build.sh exit code = $rc"

cp test.img "${DISK_DIR}"/test.img

cd "${DOCKER_DIR}"
make run-script

echo "Experient done"
