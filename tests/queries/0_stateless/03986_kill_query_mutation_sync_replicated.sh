#!/usr/bin/env bash
# Tags: replica, zookeeper, no-fasttest
# Test that KILL QUERY works for ALTER DELETE with mutations_sync=1 on ReplicatedMergeTree.
# Ref: https://github.com/ClickHouse/ClickHouse/issues/97535

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

query_id="kill_query_mutation_sync_${CLICKHOUSE_DATABASE}_$RANDOM"

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

$CLICKHOUSE_CLIENT --query "
    CREATE TABLE ${CLICKHOUSE_DATABASE}.t_kill_mutation
    (
        id UInt64,
        value String
    )
    ENGINE = ReplicatedMergeTree('/clickhouse/tables/$CLICKHOUSE_TEST_ZOOKEEPER_PREFIX/t_kill_mutation', '1')
    ORDER BY id
"

$CLICKHOUSE_CLIENT --query "INSERT INTO ${CLICKHOUSE_DATABASE}.t_kill_mutation SELECT number, toString(number) FROM numbers(100)"

# This ALTER DELETE with a subquery from system.numbers will never finish because
# the mutation reads an infinite stream. With mutations_sync=1 the query blocks
# waiting for the mutation to complete.
$CLICKHOUSE_CLIENT --query_id="$query_id" --query "
    ALTER TABLE ${CLICKHOUSE_DATABASE}.t_kill_mutation DELETE WHERE id IN (SELECT number FROM system.numbers)
    SETTINGS mutations_sync = 1
" >/dev/null 2>&1 &

wait_for_query_to_start "$query_id"

$CLICKHOUSE_CLIENT --query "KILL QUERY WHERE query_id = '$query_id' SYNC" >/dev/null

wait

# Clean up: kill the mutation itself so it doesn't keep running
$CLICKHOUSE_CLIENT --query "KILL MUTATION WHERE database = '${CLICKHOUSE_DATABASE}' AND table = 't_kill_mutation'" >/dev/null 2>&1 || true
$CLICKHOUSE_CLIENT --query "DROP TABLE IF EXISTS ${CLICKHOUSE_DATABASE}.t_kill_mutation SYNC"

echo "OK"
