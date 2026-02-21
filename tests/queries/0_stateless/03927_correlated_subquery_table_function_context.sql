-- Tags: no-fasttest
-- Correlated subquery with a table function should not fail with "Context has expired".
-- https://github.com/ClickHouse/ClickHouse/issues/92991

CREATE TABLE t0_03927 (c0 Int) ENGINE = Memory();
INSERT INTO t0_03927 VALUES (1);

SELECT (SELECT t0_03927.c0 FROM url('http://localhost:8123/?query=SELECT+1', 'CSV') tx) FROM t0_03927;

DROP TABLE t0_03927;
