#!/bin/bash
# run_lab9.sh - 模拟 ciliphen difftest/Makefile 的 lab9 目标
# 对 lab9 目标中列出的所有 riscv-tests 二进制文件跑差分测试

set -u
TEST_DIR="/tmp/riscv-lab/difftest/test/bin/riscv-test"
RUNNER="/opt/RISC-V_Platform/build/difftest_runner"
MAX_CYCLES=2000000

if [ ! -x "$RUNNER" ]; then
    echo "ERROR: $RUNNER not found" >&2
    exit 1
fi

# 与 ciliphen lab9 完全一致的文件匹配
mapfile -t tests < <(find "$TEST_DIR" \
    \( -name "*rv64ui-p-*" -o -name "*rv64um-p-*" -o -name "*rv64mi-p-*" \) \
    | sort | grep -vE "*rv64ui-p-fence_i|*rv64mi-p-access")

total=${#tests[@]}
pass=0
fail=0
timeout=0
declare -a failed_tests

echo "================== lab9 difftest: $total tests =================="
i=0
for t in "${tests[@]}"; do
    i=$((i+1))
    name=$(basename "$t" .bin)
    out=$("$RUNNER" "$t" --max-cycles "$MAX_CYCLES" 2>&1)
    rc=$?
    if [ $rc -eq 0 ]; then
        pass=$((pass+1))
        echo "[$i/$total] PASS  $name"
    elif [ $rc -eq 2 ]; then
        timeout=$((timeout+1))
        failed_tests+=("$name (timeout)")
        echo "[$i/$total] TIMEOUT  $name"
    else
        fail=$((fail+1))
        failed_tests+=("$name")
        # 只打印一行错误摘要
        echo "[$i/$total] FAIL  $name"
        echo "$out" | grep -E "^\[FAIL\]" | head -1
    fi
done

echo ""
echo "================== lab9 summary =================="
echo "total: $total"
echo "PASS:  $pass"
echo "FAIL:  $fail"
echo "TIMEOUT: $timeout"
if [ ${#failed_tests[@]} -gt 0 ]; then
    echo ""
    echo "Failed tests:"
    for f in "${failed_tests[@]}"; do
        echo "  - $f"
    done
fi
exit $fail
