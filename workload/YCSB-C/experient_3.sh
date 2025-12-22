#!/usr/bin/env bash
set -euo pipefail

# ==== 可調整參數（也可用環境變數覆蓋） ====
LOG_DIR="${LOG_DIR:-./logs_ycsb}"    # 日誌輸出目錄
DB_NAME="${DB_NAME:-mydb}"           # -db 參數
THREADS="${THREADS:-1}"              # -threads 參數
WORKLOAD="${WORKLOAD:-workloads/workloada.spec}"  # -P 參數
loadonly="${loadonly:-false}"
runonly="${runonly:-false}"
DISK_FILE="test.img"
# 如果你不想每次刪 log，就把這兩行註解掉第一行

rm -rf "$LOG_DIR"
mkdir -p "$LOG_DIR"

if [ "$#" -lt 1 ]; then
  echo "用法: $0 ycsbc_binary1 [ycsbc_binary2 ...]" >&2
  echo "  例如: $0 ycsbc_sp1_pt0_sim ycsbc_sp1_pt1_sim" >&2
  exit 1
fi

for bin in "$@"; do
  # rm -rf "$DISK_FILE"
  if [ ! -x "$bin" ]; then
    echo "⚠️  找不到可執行檔: $bin" >&2
    continue
  fi

  base="$(basename "$bin")"
  ts="$(date '+%m%d_%H%M%S')"

  full_log="${LOG_DIR}/${base}.${ts}.full.log"
  stats_log="${LOG_DIR}/${base}.${ts}.stats.log"

  echo "=== Running $bin ==="
  echo "  full log : $full_log"
  echo "  stats log: $stats_log"

  EXTRA_FLAGS=()
  if [ "$runonly" = "true" ]; then
    EXTRA_FLAGS+=("-runonly")
  fi
  if [ "$loadonly" = "true" ]; then
    EXTRA_FLAGS+=("-loadonly")
  fi

  # "$bin" -db "$DB_NAME" -threads "$THREADS" -P "$WORKLOAD"

  # 1. 先把完整輸出存到 full_log
  "$bin" -db "$DB_NAME" -threads "$THREADS" "${EXTRA_FLAGS[@]}" -P "$WORKLOAD" \
  >"$full_log" 2>&1

  # 2. 再從 full_log 抽出 MYDB 統計資訊，就算沒有 match 也不要讓整個 script 掛掉
  grep '\[MYDB-STAT\]' "$full_log" >"$stats_log" || true


  echo
done
