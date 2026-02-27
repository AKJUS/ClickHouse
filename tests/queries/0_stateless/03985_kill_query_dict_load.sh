#!/usr/bin/env bash
# Tags: no-fasttest, no-sanitizers-lsan, long, no-flaky-check
# Test that KILL QUERY works for queries blocked on dictionary loading.
# Ref: https://github.com/ClickHouse/ClickHouse/issues/97559

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

query_id="kill_query_dict_load_${CLICKHOUSE_DATABASE}_$RANDOM"

function wait_for_query_to_start()
{
    local timeout=120
    local start=$EPOCHSECONDS
    while [[ $($CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SELECT count() FROM system.processes WHERE query_id = '$1' SETTINGS use_query_cache = 0") == 0 ]]; do
        if ((EPOCHSECONDS - start > timeout)); then
            echo "Timeout waiting for query to start" >&2
            exit 1
        fi
        sleep 0.1
    done
}

# Create a dictionary with a source query that takes forever to load.
$CLICKHOUSE_CLIENT --query "
    CREATE DICTIONARY IF NOT EXISTS ${CLICKHOUSE_DATABASE}.slow_dict
    (
        id UInt64,
        value String
    )
    PRIMARY KEY id
    SOURCE(CLICKHOUSE(QUERY 'SELECT number AS id, toString(number) AS value FROM system.numbers'))
    LIFETIME(0)
    LAYOUT(FLAT())
"

# This query will block waiting for the dictionary to load (which will never finish).
$CLICKHOUSE_CLIENT --query_id="$query_id" --query "
    SELECT dictGet('${CLICKHOUSE_DATABASE}.slow_dict', 'value', toUInt64(1))
" >/dev/null 2>&1 &

wait_for_query_to_start "$query_id"

# Use async KILL (without SYNC) to avoid blocking if propagation is slow.
$CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "KILL QUERY WHERE query_id = '$query_id'" >/dev/null

wait

$CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SYSTEM RELOAD DICTIONARY ${CLICKHOUSE_DATABASE}.slow_dict" >/dev/null 2>&1 || true
$CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "DROP DICTIONARY IF EXISTS ${CLICKHOUSE_DATABASE}.slow_dict"

echo "OK"
