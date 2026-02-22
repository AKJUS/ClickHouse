#!/usr/bin/env bash
# Tags: no-fasttest, no-sanitizers-lsan, long
# Test that KILL QUERY works for queries with WITH FILL generating huge ranges.
# Ref: https://github.com/ClickHouse/ClickHouse/issues/97560

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

query_id="kill_query_with_fill_${CLICKHOUSE_DATABASE}_$RANDOM"

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

# This query generates ~3 billion rows via WITH FILL (year 2000 to 2100, step 1 second).
# Without the fix, KILL QUERY would not be able to cancel it.
$CLICKHOUSE_CLIENT --query_id="$query_id" --query "
    SELECT ts FROM (SELECT toDateTime('2050-06-15 12:00:00') AS ts)
    ORDER BY ts WITH FILL FROM toDateTime('2000-01-01 00:00:00') TO toDateTime('2100-01-01 00:00:00') STEP 1
    FORMAT Null
" >/dev/null 2>&1 &

wait_for_query_to_start "$query_id"

$CLICKHOUSE_CLIENT --query "KILL QUERY WHERE query_id = '$query_id' SYNC" >/dev/null

wait

echo "OK"
