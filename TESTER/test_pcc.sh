#!/bin/bash
# Robust tester for pcc_client and pcc_server
set -e
cd "$(dirname "$0")"

SERVER=./pcc_server
CLIENT=./pcc_client
PORT=3000
SERVER_OUT=server_out.txt
CLIENTS_OUT=clients_out.txt
PYTHON=python3

# Build server and client
echo "Compiling server and client..."
gcc -Wall -O2 -o pcc_server pcc_server.c
gcc -Wall -O2 -o pcc_client pcc_client.c

# Clean up from previous runs
rm -f $SERVER_OUT $CLIENTS_OUT testfile_*

# Generate input files for base tests
echo -n "" > testfile_empty
head -c 1024 </dev/urandom > testfile_bin
printf '%s' {a..z} > testfile_printable
printf '\x01\x02\x03' > testfile_nonprint
head -c 100000 </dev/urandom | tr -dc '\x20-\x7e' > testfile_large_printable
$PYTHON -c 'import sys; sys.stdout.buffer.write(bytes(range(256)))' > testfile_all_ascii
head -c 1000 < /dev/zero | tr '\0' 'A' > testfile_1000A
head -c 1000 < /dev/zero | tr '\0' '\1' > testfile_1000nonprint

# Start server in background
$SERVER $PORT > $SERVER_OUT 2>&1 &
SERVER_PID=$!
sleep 1

run_client() {
    local file=$1
    local expected=$2
    $CLIENT 10.0.2.15 $PORT $file > client_out_tmp 2>&1
    local got=$(grep -o '[0-9]\+' client_out_tmp | tail -1)
    if [ "$got" != "$expected" ]; then
        echo "Test Failed - $file: expected $expected, got $got"
        cat client_out_tmp
        kill $SERVER_PID
        exit 1
    else
        echo "Test Passed - $file: $got printable chars"
    fi
    echo "===== $file =====" >> $CLIENTS_OUT
    cat client_out_tmp >> $CLIENTS_OUT
    echo -e '\n==========\n' >> $CLIENTS_OUT
}

calc_expected_stats() {
    rm -f tmp_server_stats.txt tmp_expected_stats.txt
    $PYTHON count_printable_per_char.py "$@" > tmp_expected_stats.txt
}

BASE_TESTS=(testfile_empty testfile_printable testfile_nonprint testfile_bin testfile_large_printable testfile_all_ascii testfile_1000A testfile_1000nonprint)
ALL_TESTS=("${BASE_TESTS[@]}" "$@")

echo "HERE..."

for file in "${ALL_TESTS[@]}"; do
    if [ ! -f "$file" ]; then continue; fi
    expected=$($PYTHON count_printable_per_char.py "$file" | $PYTHON -c "import sys; print(sum(int(line.split()[-2]) for line in sys.stdin))")
    echo "Running client test for $file, expecting $expected printable characters..."
    run_client "$file" "$expected"
done

echo "=================================================="
echo "Checking server stats after all client tests..."

calc_expected_stats "${ALL_TESTS[@]}"

kill -INT $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null
cat $SERVER_OUT | grep "char '" | sort > tmp_server_stats.txt

if $PYTHON compare_counts.py tmp_server_stats.txt tmp_expected_stats.txt; then
    echo "Test Passed - Server stats match expected counts"
else
    echo "Test Failed - Server stats do not match expected counts"
fi

echo "=================================================="
echo "Running Special SIGINT during large transfer test..."

# Special test: SIGINT during large transfer
$SERVER $PORT > server_out_sigint.txt 2>&1 &
SERVER_PID2=$!
sleep 1
$CLIENT 10.0.2.15 $PORT testfile_large_printable > /dev/null 2>&1 &
CLIENT_PID2=$!
sleep 0.5
kill -INT $SERVER_PID2 2>/dev/null || true
wait $SERVER_PID2 2>/dev/null
wait $CLIENT_PID2 2>/dev/null || true

# Only compare per-character stats, ignore total line
$PYTHON count_printable_per_char.py testfile_large_printable > tmp_expected_sigint.txt
grep "char '" server_out_sigint.txt | sort > tmp_server_sigint_stats.txt
if $PYTHON compare_counts.py tmp_server_sigint_stats.txt tmp_expected_sigint.txt; then
    echo "Test Passed - Special SIGINT test, Output matches expected (per-character only)"
else
    echo "Test Failed - Special SIGINT test, Output does not match expected (per-character only)"
fi

echo "=================================================="

rm -f testfile_*
rm -f tmp_server_stats.txt tmp_expected_stats.txt client_out_tmp server_out_sigint.txt tmp_partial_printable tmp_expected_sigint.txt tmp_server_sigint_stats.txt
kill $SERVER_PID 2>/dev/null || true

exit 0
