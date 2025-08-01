---
description: 'System table containing descriptions of table engines supported by the
  server and the features they support.'
keywords: ['system table', 'table_engines']
slug: /operations/system-tables/table_engines
title: 'system.table_engines'
---

# system.table_engines

Contains description of table engines supported by server and their feature support information.

This table contains the following columns (the column type is shown in brackets):

- `name` (String) — The name of table engine.
- `supports_settings` (UInt8) — Flag that indicates if table engine supports `SETTINGS` clause.
- `supports_skipping_indices` (UInt8) — Flag that indicates if table engine supports [skipping indices](../../engines/table-engines/mergetree-family/mergetree.md#table_engine-mergetree-data_skipping-indexes).
- `supports_ttl` (UInt8) — Flag that indicates if table engine supports [TTL](../../engines/table-engines/mergetree-family/mergetree.md#table_engine-mergetree-ttl).
- `supports_sort_order` (UInt8) — Flag that indicates if table engine supports clauses `PARTITION_BY`, `PRIMARY_KEY`, `ORDER_BY` and `SAMPLE_BY`.
- `supports_replication` (UInt8) — Flag that indicates if table engine supports [data replication](../../engines/table-engines/mergetree-family/replication.md).
- `supports_duduplication` (UInt8) — Flag that indicates if table engine supports data deduplication.
- `supports_parallel_insert` (UInt8) — Flag that indicates if table engine supports parallel insert (see [`max_insert_threads`](/operations/settings/settings#max_insert_threads) setting).

Example:

```sql
SELECT *
FROM system.table_engines
WHERE name IN ('Kafka', 'MergeTree', 'ReplicatedCollapsingMergeTree')
```

```text
┌─name──────────────────────────┬─supports_settings─┬─supports_skipping_indices─┬─supports_sort_order─┬─supports_ttl─┬─supports_replication─┬─supports_deduplication─┬─supports_parallel_insert─┐
│ MergeTree                     │                 1 │                         1 │                   1 │            1 │                    0 │                      0 │                        1 │
│ Kafka                         │                 1 │                         0 │                   0 │            0 │                    0 │                      0 │                        0 │
│ ReplicatedCollapsingMergeTree │                 1 │                         1 │                   1 │            1 │                    1 │                      1 │                        1 │
└───────────────────────────────┴───────────────────┴───────────────────────────┴─────────────────────┴──────────────┴──────────────────────┴────────────────────────┴──────────────────────────┘
```

**See also**

- MergeTree family [query clauses](../../engines/table-engines/mergetree-family/mergetree.md#mergetree-query-clauses)
- Kafka [settings](/engines/table-engines/integrations/kafka#creating-a-table)
- Join [settings](../../engines/table-engines/special/join.md#join-limitations-and-settings)
