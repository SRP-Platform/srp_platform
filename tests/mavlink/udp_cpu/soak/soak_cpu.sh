#!/bin/sh
# CPU load and sampler of the soak test, for the CPU of `//tests/mavlink/udp_cpu` (RPi Zero 2 W, Alpine
# busybox `sh`: no stress-ng, python3 or vmstat).
#
#   soak_cpu.sh <time_s> <hogs> [<hog_start_s> [<hog_time_s>]]
#
# Samples the daemon every SOAK_INTERVAL seconds for <time_s> seconds and writes one CSV line per
# sample. <hogs> `sha256sum /dev/zero` processes (nice 0: the same priority as the daemon) run from
# <hog_start_s> (default 0) for <hog_time_s> (default: until the end); 0 hogs = sampling only.
#
# Environment:
#   SOAK_INTERVAL    seconds between samples (default 30)
#   SOAK_CSV         output file (default ${SOAK_LOG_DIR:-/logs}/soak_<date>.csv)
#   SOAK_PID_CMD     command that prints the daemon PID (default `pidof mavlink_daemon`)
#
# CSV columns (the first line of the file names them):
#   t_s            seconds since the start
#   epoch          date +%s of the CPU
#   hogs           hog processes running at the sample
#   pid            PID of the daemon (0 = not running)
#   restarts       how many times the PID changed since the first sample
#   rss_kb         VmRSS of the daemon
#   daemon_cpu     CPU of the daemon in % of ONE core (100 = a whole core) over the interval, one decimal
#   cpu_total      busy time of all cores in % of all cores (without idle and iowait), one decimal
#   load1          1-minute load average
#   temp_mc        /sys/class/thermal/thermal_zone0/temp (millidegrees C)
#   freq_khz       scaling_cur_freq of cpu0
# The last line of the file (and of stdout) is SOAK_CPU_DONE.

TIME_S=${1:?usage: soak_cpu.sh <time_s> <hogs> [<hog_start_s> [<hog_time_s>]]}
HOGS=${2:?usage: soak_cpu.sh <time_s> <hogs> [<hog_start_s> [<hog_time_s>]]}
HOG_START=${3:-0}
HOG_TIME=${4:-$TIME_S}
INTERVAL=${SOAK_INTERVAL:-30}
PID_CMD=${SOAK_PID_CMD:-pidof mavlink_daemon}
CSV=${SOAK_CSV:-${SOAK_LOG_DIR:-/logs}/soak_$(date +%Y%m%d_%H%M%S).csv}

HOG_PIDS=""

cleanup() {
    for p in $HOG_PIDS; do
        kill "$p" 2>/dev/null
    done
    HOG_PIDS=""
}

finish() {
    cleanup
    echo SOAK_CPU_DONE >> "$CSV"
    echo SOAK_CPU_DONE
    exit 0
}

trap cleanup EXIT
trap finish INT TERM

start_hogs() {
    i=0
    while [ "$i" -lt "$HOGS" ]; do
        sha256sum /dev/zero > /dev/null 2>&1 &
        HOG_PIDS="$HOG_PIDS $!"
        i=$((i + 1))
    done
}

# Sets TOTAL (all jiffies) and IDLE (idle + iowait) from /proc/stat.
read_stat() {
    read -r _ u n s id io irq sirq st _ < /proc/stat
    TOTAL=$((u + n + s + id + io + irq + sirq + st))
    IDLE=$((id + io))
}

# Sets PROC (utime + stime of PID in jiffies) and RSS (kB).
read_proc() {
    PROC=0
    RSS=0
    [ "$1" -gt 0 ] && [ -r "/proc/$1/stat" ] || return 0
    # shellcheck disable=SC2046  # the fields after "comm)" are split on purpose
    set -- "$1" $(sed 's/.*) //' "/proc/$1/stat")
    PROC=$((${13} + ${14}))  # utime and stime: fields 14 and 15 of the file, $1 holds the PID
    RSS=$(sed -n 's/^VmRSS:[[:space:]]*\([0-9]*\).*/\1/p' "/proc/$1/status" 2> /dev/null)
    RSS=${RSS:-0}
}

# 1234 -> 123.4 (the shell has integers only)
tenths() {
    echo "$(($1 / 10)).$(($1 % 10))"
}

daemon_pid() {
    # shellcheck disable=SC2046,SC2086  # the command and its output are word lists
    set -- $($PID_CMD 2> /dev/null)
    echo "${1:-0}"
}

mkdir -p "$(dirname "$CSV")" || exit 1
echo "t_s,epoch,hogs,pid,restarts,rss_kb,daemon_cpu,cpu_total,load1,temp_mc,freq_khz" > "$CSV"
echo "SOAK_CPU_CSV=$CSV"

NCPU=$(nproc 2> /dev/null || echo 1)
START=$(date +%s)
RESTARTS=0
HOGS_ON=0
read_stat
PREV_TOTAL=$TOTAL
PREV_IDLE=$IDLE
PREV_PROC=0
LAST_PID=$(daemon_pid)
KNOWN_PID=$LAST_PID
read_proc "$LAST_PID"
PREV_PROC=$PROC

while :; do
    NOW=$(date +%s)
    T=$((NOW - START))
    [ "$T" -ge "$TIME_S" ] && break
    if [ "$HOGS_ON" -eq 0 ] && [ "$HOGS" -gt 0 ] && [ "$T" -ge "$HOG_START" ] &&
        [ "$T" -lt $((HOG_START + HOG_TIME)) ]; then
        start_hogs
        HOGS_ON=$HOGS
    elif [ "$HOGS_ON" -gt 0 ] && [ "$T" -ge $((HOG_START + HOG_TIME)) ]; then
        cleanup
        HOGS_ON=0
    fi
    sleep "$INTERVAL" &
    wait $!
    NOW=$(date +%s)
    T=$((NOW - START))
    PID=$(daemon_pid)
    if [ "$PID" != "$LAST_PID" ]; then
        # a new process (also after a gap in which none ran): its CPU time starts from zero
        [ "$PID" -gt 0 ] && [ "$KNOWN_PID" -gt 0 ] && RESTARTS=$((RESTARTS + 1))
        [ "$PID" -gt 0 ] && KNOWN_PID=$PID
        LAST_PID=$PID
        PREV_PROC=0
    fi
    read_stat
    read_proc "$PID"
    DT=$((TOTAL - PREV_TOTAL))
    [ "$DT" -gt 0 ] || DT=1
    DCPU=$(tenths $((1000 * (DT - (IDLE - PREV_IDLE)) / DT)))
    DDAEMON=$(tenths $((1000 * NCPU * (PROC - PREV_PROC) / DT)))
    PREV_TOTAL=$TOTAL
    PREV_IDLE=$IDLE
    PREV_PROC=$PROC
    read -r LOAD1 _ < /proc/loadavg
    TEMP=$(cat /sys/class/thermal/thermal_zone0/temp 2> /dev/null || echo 0)
    FREQ=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2> /dev/null || echo 0)
    echo "$T,$NOW,$HOGS_ON,$PID,$RESTARTS,$RSS,$DDAEMON,$DCPU,$LOAD1,$TEMP,$FREQ" >> "$CSV"
done

finish
