#!/bin/bash
# The soak test of the MAVLink daemon under CPU load, from the PC (see the README, "Soak test").
#
#   soak_run.sh [--duration S] [--baseline S] [--cooldown S] [--hogs N|auto] [--host USER@HOST]
#               [--out DIR] [--pcap] [--iface IFACE] [--dlt-cmd CMD] [--log FILE]...
#               [--station-args "ARGS"] [--verdict-args "ARGS"] [--local]
#
# Phases: baseline (default 600 s) -> load (--duration, default 10800 s = 3 h) -> cooldown (600 s).
# Starts soak_station on this PC, soak_cpu.sh on the CPU over ssh (hogs = nproc by default) and,
# with --pcap, tshark into a ring of 10 files of 50 MB; at the end fetches /logs/soak_*.csv and
# srp_start.log, prints the verdict (soak_verdict) and leaves everything in --out
# (default ~/mavlink/soak/<date>). Exit 0 = PASS. It does not need a terminal: start it detached,
#
#   setsid nohup bazel-bin/tests/mavlink/udp_cpu/soak_run --duration 10800 > /dev/null 2>&1 &
#
# --local: the daemon and the example applications on this host (soak_local), the loopback in place
# of the CPU, short phases (10 + 40 + 10 s unless given), hogs on this host. For the test of the tools.
set -u

HERE=$(dirname "$0")
if [ -d "$0.runfiles" ]; then
    # started as bazel-bin/.../soak_run: the tools are in its runfiles tree (their stubs need it)
    TOOL=$(find "$0.runfiles" -path '*/tests/mavlink/udp_cpu/soak_station' -print -quit 2> /dev/null)
    [ -n "$TOOL" ] && HERE=$(dirname "$TOOL")
fi
DURATION=""
BASELINE=""
COOLDOWN=""
HOGS=auto
HOST=root@192.168.7.2
OUT=""
PCAP=0
IFACE=""
DLT_CMD=""
LOCAL=0
STATION_ARGS=""
VERDICT_ARGS=""
LOGS=()

usage() {
    sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-2}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --duration) DURATION=$2; shift 2 ;;
        --baseline) BASELINE=$2; shift 2 ;;
        --cooldown) COOLDOWN=$2; shift 2 ;;
        --hogs) HOGS=$2; shift 2 ;;
        --host) HOST=$2; shift 2 ;;
        --out) OUT=$2; shift 2 ;;
        --pcap) PCAP=1; shift ;;
        --iface) IFACE=$2; shift 2 ;;
        --dlt-cmd) DLT_CMD=$2; shift 2 ;;
        --log) LOGS+=("$2"); shift 2 ;;
        --station-args) STATION_ARGS=$2; shift 2 ;;
        --verdict-args) VERDICT_ARGS=$2; shift 2 ;;
        --local) LOCAL=1; shift ;;
        -h | --help) usage 0 ;;
        *) echo "soak_run: unknown argument $1" >&2; usage 2 ;;
    esac
done

if [ "$LOCAL" -eq 1 ]; then
    : "${BASELINE:=10}" "${DURATION:=40}" "${COOLDOWN:=10}"
    INTERVAL=${SOAK_INTERVAL:-2}
    WINDOW=5
    SKIP=5
else
    : "${BASELINE:=600}" "${DURATION:=10800}" "${COOLDOWN:=600}"
    INTERVAL=${SOAK_INTERVAL:-30}
    WINDOW=10
    SKIP=30
fi
TOTAL=$((BASELINE + DURATION + COOLDOWN))
PHASES="baseline:$BASELINE,load:$DURATION,cooldown:$COOLDOWN"
STAMP=$(date +%Y%m%d_%H%M%S)
OUT=${OUT:-$HOME/mavlink/soak/$STAMP}
mkdir -p "$OUT" || exit 2
exec > >(tee -a "$OUT/soak_run.log") 2>&1
echo "soak_run: $STAMP phases $PHASES, out $OUT, $([ "$LOCAL" -eq 1 ] && echo local || echo "$HOST")"

PIDS=()
# shellcheck disable=SC2329  # called by the trap
cleanup() {
    trap - EXIT INT TERM
    if [ "$LOCAL" -eq 0 ]; then
        ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" 'pkill -TERM -f soak_cpu.sh; pkill sha256sum' 2> /dev/null
    fi
    for p in "${PIDS[@]}"; do
        kill "$p" 2> /dev/null
    done
    wait 2> /dev/null
}
trap cleanup EXIT
trap 'exit 130' INT TERM

STATION_FLAGS=(--phases "$PHASES" --window "$WINDOW" --cooldown-skip "$SKIP"
    --jsonl "$OUT/station.jsonl" --summary "$OUT/station.json")
CSV="$OUT/soak_cpu.csv"
VERDICT_FLAGS=()

if [ "$LOCAL" -eq 1 ]; then
    WORK="$OUT/local"
    "$HERE/soak_local" --workdir "$WORK" > "$OUT/soak_local.log" 2>&1 &
    PIDS+=($!)
    for _ in $(seq 1 200); do
        [ -s "$WORK/local.env" ] && break
        sleep 0.1
    done
    [ -s "$WORK/local.env" ] || { echo "soak_run: the local CPU did not start" >&2; exit 2; }
    # shellcheck disable=SC1091  # written by soak_local
    . "$WORK/local.env"
    STATION_FLAGS+=(--listen-host 127.0.0.1 --target 127.0.0.1
        --listen-port-a "$STATION_PORT_A" --peer-port-a "$PEER_PORT_A"
        --listen-port-b "$STATION_PORT_B" --peer-port-b "$PEER_PORT_B"
        --daemon-config "$DAEMON_CONFIG")
    LOGS+=("$WORK/daemon.err")
    HOG_COUNT=$HOGS
    [ "$HOG_COUNT" = auto ] && HOG_COUNT=$(nproc)  # nproc honours a cgroup limit (agent-res)
    # the share of the cores the hogs can fill, not 95 % of a host that is shared with others
    VERDICT_FLAGS+=(--min-cpu-total $((90 * HOG_COUNT / $(getconf _NPROCESSORS_ONLN))))
else
    ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" true || { echo "soak_run: no ssh to $HOST" >&2; exit 2; }
    ssh "$HOST" 'cat > /tmp/soak_cpu.sh && chmod +x /tmp/soak_cpu.sh' < "$HERE/soak_cpu" || exit 2
    [ -n "$IFACE" ] || IFACE=$(ip route get "${HOST#*@}" 2> /dev/null | sed -n 's/.* dev \([^ ]*\).*/\1/p' | head -n 1)
fi

# shellcheck disable=SC2086  # STATION_ARGS is a word list
"$HERE/soak_station" "${STATION_FLAGS[@]}" $STATION_ARGS > "$OUT/station.out" 2>&1 &
STATION=$!
PIDS+=("$STATION")

if [ "$PCAP" -eq 1 ]; then
    tshark -i "${IFACE:-lo}" -f "udp port 14550 or udp port 14551" -b filesize:50000 -b files:10 \
        -w "$OUT/soak.pcap" > "$OUT/tshark.log" 2>&1 &
    PIDS+=($!)
    sleep 2
    kill -0 "${PIDS[-1]}" 2> /dev/null || echo "soak_run: tshark did not start, see $OUT/tshark.log" >&2
fi
if [ -n "$DLT_CMD" ]; then
    bash -c "$DLT_CMD" > "$OUT/dlt.log" 2>&1 &
    PIDS+=($!)
    LOGS+=("$OUT/dlt.log")
fi

if [ "$LOCAL" -eq 1 ]; then
    SOAK_CSV=$CSV SOAK_INTERVAL=$INTERVAL SOAK_PID_CMD="cat $WORK/daemon.pid" \
        sh "$HERE/soak_cpu" "$TOTAL" "$HOG_COUNT" "$BASELINE" "$DURATION" > "$OUT/soak_cpu.out" 2>&1 &
    PIDS+=($!)
else
    REMOTE_CSV=/logs/soak_$STAMP.csv
    # shellcheck disable=SC2029  # the values are expanded here on purpose
    ssh "$HOST" "hogs=$HOGS; [ \$hogs = auto ] && hogs=\$(nproc);
        SOAK_CSV=$REMOTE_CSV SOAK_INTERVAL=$INTERVAL nohup sh /tmp/soak_cpu.sh $TOTAL \$hogs $BASELINE $DURATION \
        > /tmp/soak_cpu.out 2>&1 < /dev/null &" || exit 2
fi

wait "$STATION"
STATION_RC=$?
echo "soak_run: the station finished (rc $STATION_RC)"

if [ "$LOCAL" -eq 0 ]; then
    for _ in $(seq 1 $((120 / 5))); do
        # shellcheck disable=SC2029  # REMOTE_CSV is expanded here on purpose
        ssh "$HOST" "tail -n 1 $REMOTE_CSV" 2> /dev/null | grep -q SOAK_CPU_DONE && break
        sleep 5
    done
    # shellcheck disable=SC2029  # REMOTE_CSV is expanded here on purpose
    ssh "$HOST" "cat $REMOTE_CSV" > "$CSV" || echo "soak_run: no CSV on the CPU" >&2
    if ssh "$HOST" 'cat /logs/srp_start.log' > "$OUT/srp_start.log" 2> /dev/null; then
        LOGS+=("$OUT/srp_start.log")
    else
        rm -f "$OUT/srp_start.log"
    fi
else
    for _ in $(seq 1 100); do
        grep -q SOAK_CPU_DONE "$CSV" 2> /dev/null && break
        sleep 0.2
    done
fi
[ -n "$DLT_CMD" ] && kill "${PIDS[-1]}" 2> /dev/null

LOG_FLAGS=()
for l in "${LOGS[@]}"; do
    LOG_FLAGS+=(--log "$l")
done
cat "$OUT/station.out"
echo
echo "== verdict =="
# shellcheck disable=SC2086  # VERDICT_ARGS is a word list
"$HERE/soak_verdict" --summary "$OUT/station.json" --csv "$CSV" --phases "$PHASES" \
    --interval-skip $((2 * INTERVAL)) --out "$OUT/verdict.json" "${VERDICT_FLAGS[@]}" "${LOG_FLAGS[@]}" $VERDICT_ARGS
exit $?
