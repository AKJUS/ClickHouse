SET max_threads=0;
SET optimize_read_in_order=1;
SET optimize_trivial_insert_select = 1;
SET read_in_order_two_level_merge_threshold=100;
SET read_in_order_use_virtual_row = 1;

DROP TABLE IF EXISTS t_read_in_order;

CREATE TABLE t_read_in_order(date Date, i UInt64, v UInt64)
ENGINE = MergeTree ORDER BY (date, i) SETTINGS index_granularity = 8192, index_granularity_bytes = '10Mi';

INSERT INTO t_read_in_order SELECT '2020-10-10', number % 10, number FROM numbers(100000);
INSERT INTO t_read_in_order SELECT '2020-10-11', number % 10, number FROM numbers(100000);

SELECT toStartOfMonth(date) as d, i FROM t_read_in_order ORDER BY d, i LIMIT 5;
EXPLAIN PIPELINE SELECT toStartOfMonth(date) as d, i FROM t_read_in_order ORDER BY d, i LIMIT 5;

SELECT toStartOfMonth(date) as d, i FROM t_read_in_order ORDER BY d DESC, -i LIMIT 5;
EXPLAIN PIPELINE SELECT toStartOfMonth(date) as d, i FROM t_read_in_order ORDER BY d DESC, -i LIMIT 5;

-- Here FinishSorting is used, because directions don't match.
SELECT toStartOfMonth(date) as d, i FROM t_read_in_order ORDER BY d, -i LIMIT 5;
EXPLAIN PIPELINE SELECT toStartOfMonth(date) as d, i FROM t_read_in_order ORDER BY d, -i LIMIT 5;

SELECT date, i FROM t_read_in_order WHERE date = '2020-10-11' ORDER BY i LIMIT 5;
EXPLAIN PIPELINE SELECT date, i FROM t_read_in_order WHERE date = '2020-10-11' ORDER BY i LIMIT 5 settings enable_analyzer=0;
EXPLAIN PIPELINE SELECT date, i FROM t_read_in_order WHERE date = '2020-10-11' ORDER BY i LIMIT 5 settings enable_analyzer=1;

SELECT * FROM t_read_in_order WHERE date = '2020-10-11' ORDER BY i, v LIMIT 5;
EXPLAIN PIPELINE SELECT * FROM t_read_in_order WHERE date = '2020-10-11' ORDER BY i, v LIMIT 5 settings enable_analyzer=0;
EXPLAIN PIPELINE SELECT * FROM t_read_in_order WHERE date = '2020-10-11' ORDER BY i, v LIMIT 5 settings enable_analyzer=1;

INSERT INTO t_read_in_order SELECT '2020-10-12', number, number FROM numbers(100000);

SELECT date, i FROM t_read_in_order WHERE date = '2020-10-12' ORDER BY i LIMIT 5;

EXPLAIN SYNTAX SELECT date, i FROM t_read_in_order WHERE date = '2020-10-12' ORDER BY i DESC LIMIT 5;
EXPLAIN PIPELINE SELECT date, i FROM t_read_in_order WHERE date = '2020-10-12' ORDER BY i DESC LIMIT 5 settings enable_analyzer=0;
EXPLAIN PIPELINE SELECT date, i FROM t_read_in_order WHERE date = '2020-10-12' ORDER BY i DESC LIMIT 5 settings enable_analyzer=1;
SELECT date, i FROM t_read_in_order WHERE date = '2020-10-12' ORDER BY i DESC LIMIT 5;

DROP TABLE IF EXISTS t_read_in_order;

CREATE TABLE t_read_in_order(a UInt32, b UInt32)
ENGINE = MergeTree ORDER BY (a, b)
SETTINGS index_granularity = 3, index_granularity_bytes = '10Mi';

SYSTEM STOP MERGES t_read_in_order;

INSERT INTO t_read_in_order VALUES (0, 100), (1, 2), (1, 3), (1, 4), (2, 5);
INSERT INTO t_read_in_order VALUES (0, 100), (1, 2), (1, 3), (1, 4), (2, 5);

SELECT a, b FROM t_read_in_order WHERE a = 1 ORDER BY b SETTINGS read_in_order_two_level_merge_threshold = 1;
SELECT '========';
SELECT a, b FROM t_read_in_order WHERE a = 1 ORDER BY b DESC SETTINGS read_in_order_two_level_merge_threshold = 1;

DROP TABLE t_read_in_order;

CREATE TABLE t_read_in_order(dt DateTime, d Decimal64(5), v UInt64)
ENGINE = MergeTree ORDER BY (toStartOfDay(dt), d) SETTINGS index_granularity = 8192, index_granularity_bytes = '10Mi';

INSERT INTO t_read_in_order SELECT toDateTime('2020-10-10 00:00:00') + number, 1 / (number % 100 + 1), number FROM numbers(1000);

EXPLAIN PIPELINE SELECT toStartOfDay(dt) as date, d FROM t_read_in_order ORDER BY date, round(d) LIMIT 5;
SELECT * from (
  SELECT toStartOfDay(dt) as date, d FROM t_read_in_order ORDER BY date, round(d) LIMIT 50000000000
  -- subquery with limit 50000000 to stabilize a test result and prevent order by d pushdown
) order by d limit 5;

EXPLAIN PIPELINE SELECT toStartOfDay(dt) as date, d FROM t_read_in_order ORDER BY date, round(d) LIMIT 5;
SELECT * from (
  SELECT toStartOfDay(dt) as date, d FROM t_read_in_order WHERE date = '2020-10-10' ORDER BY round(d) LIMIT 50000000000
  -- subquery with limit 50000000 to stabilize a test result and prevent order by d pushdown
) order by d limit 5;
