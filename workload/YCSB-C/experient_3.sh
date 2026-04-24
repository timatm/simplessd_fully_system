#  USE: ./experient_3.sh ./ycsbc_pack1_place1_sim ./ycsbc_pack1_place2_sim 

#!/usr/bin/env bash
set -euo pipefail

# ==== 可調整參數（也可用環境變數覆蓋） ====
# LOG_DIR="${LOG_DIR:-./logs_ycsb}"
LOG_DIR="${LOG_DIR:-./host_logs_ycsb}"
DB_NAME="${DB_NAME:-mydb}"
THREADS="${THREADS:-1}"
DISK_FILE="${DISK_FILE:-test.img}"

LOAD_SPEC_PATH="${LOAD_SPEC_PATH:-workloads/workload.spec}"

# WORKLOAD_SET="${WORKLOAD_SET:-A B C D F}"
# WORKLOAD_SET="${WORKLOAD_SET:-A B C D F 25 75}"
WORKLOAD_SET="${WORKLOAD_SET:-75}"
# WORKLOAD_SET="${WORKLOAD_SET:-0 10 20 30 40 50 60 70 80 90}"
# WORKLOAD_SET="${WORKLOAD_SET:-0}"
WORKLOAD_SPEC_FMT="${WORKLOAD_SPEC_FMT:-workloads/workload%s.spec}"

# 是否清空 LOG_DIR
CLEAN_LOGS="${CLEAN_LOGS:-true}"

if [ "$#" -lt 1 ]; then
  echo "用法: $0 ycsbc_binary1 [ycsbc_binary2 ...]" >&2
  exit 1
fi

# 檢查 load spec 是否存在（必需）
if [ ! -f "$LOAD_SPEC_PATH" ]; then
  echo "❌ 找不到 load spec: $LOAD_SPEC_PATH" >&2
  echo "   你可以用環境變數改路徑，例如：" >&2
  echo "   LOAD_SPEC_PATH=workloads/workload.spec $0 <bins...>" >&2
  exit 1
fi

if [ "$CLEAN_LOGS" = "true" ]; then
  rm -rf "$LOG_DIR"
fi
mkdir -p "$LOG_DIR"

ALL_STATS_LOG="${LOG_DIR}/ALL.stats.log"
: > "$ALL_STATS_LOG"

ts_global="$(date '+%m%d_%H%M')"

for bin in "$@"; do
  if [ ! -x "$bin" ]; then
    echo "⚠️  找不到可執行檔: $bin" >&2
    continue
  fi

  base="$(basename "$bin")"

  for wl in $WORKLOAD_SET; do
    wl_lc="$(echo "$wl" | tr '[:upper:]' '[:lower:]')"
    workload_spec="$(printf "$WORKLOAD_SPEC_FMT" "$wl_lc")"

    if [ ! -f "$workload_spec" ]; then
      echo "⚠️  找不到 workload spec: $workload_spec (workload=$wl)" >&2
      continue
    fi

    out_dir="${LOG_DIR}/${base}/workload${wl_lc}"
    mkdir -p "$out_dir"

    rm -rf "$DISK_FILE"

    load_full_log="${out_dir}/${base}.workload${wl_lc}.${ts_global}.LOAD.full.log"
    load_stats_log="${out_dir}/${base}.workload${wl_lc}.${ts_global}.LOAD.stats.log"

    run_full_log="${out_dir}/${base}.workload${wl_lc}.${ts_global}.RUN.full.log"
    run_stats_log="${out_dir}/${base}.workload${wl_lc}.${ts_global}.RUN.stats.log"

    echo "=== Running $base | workload $wl ==="
    echo "  load spec : $LOAD_SPEC_PATH"
    echo "  run spec  : $workload_spec"
    echo "  load full : $load_full_log"
    echo "  run  full : $run_full_log"

    "$bin" -db "$DB_NAME" -threads "$THREADS" -loadonly -P "$LOAD_SPEC_PATH" \
      >"$load_full_log" 2>&1

    grep '\[MYDB-STAT\]' "$load_full_log" >"$load_stats_log" || true

    {
      echo "----- ${ts_global} | bin=${base} | workload=${wl} | phase=LOAD | spec=${LOAD_SPEC_PATH} -----"
      if [ -s "$load_stats_log" ]; then
        cat "$load_stats_log"
      else
        echo "(no [MYDB-STAT] lines)"
      fi
      echo
    } >> "$ALL_STATS_LOG"

    "$bin" -db "$DB_NAME" -threads "$THREADS" -runonly -P "$workload_spec" \
      >"$run_full_log" 2>&1

    grep '\[MYDB-STAT\]' "$run_full_log" >"$run_stats_log" || true

    {
      echo "----- ${ts_global} | bin=${base} | workload=${wl} | phase=RUN  | spec=${workload_spec} -----"
      if [ -s "$run_stats_log" ]; then
        cat "$run_stats_log"
      else
        echo "(no [MYDB-STAT] lines)"
      fi
      echo
    } >> "$ALL_STATS_LOG"

    echo
  done
done

echo "✅ Done."
echo "📌 Per-run logs under: $LOG_DIR/<bin>/workload*/"
echo "📌 Aggregated stats  : $ALL_STATS_LOG"
