#!/usr/bin/env python3

import os

import pytest

import helpers.keeper_utils as keeper_utils
from helpers.cluster import ClickHouseCluster

CURRENT_TEST_DIR = os.path.dirname(os.path.abspath(__file__))
cluster = ClickHouseCluster(__file__)

# clickhouse itself will use external zookeeper
node = cluster.add_instance(
    "node",
    main_configs=["configs/enable_keeper.xml"],
    stay_alive=True,
    # When `with_remote_database_disk` is enalbed, `with_zookeeper` might be enabled and the feature flag `CHECK_NOT_EXISTS` is enabled, causing the test `test_keeper_feature_flags` to fail
    with_remote_database_disk=False,
)


@pytest.fixture(scope="module")
def started_cluster():
    try:
        cluster.start()

        yield cluster

    finally:
        cluster.shutdown()


def get_connection_zk(nodename, timeout=30.0):
    _fake_zk_instance = keeper_utils.get_fake_zk(cluster, nodename, timeout=timeout)
    return _fake_zk_instance


def restart_clickhouse(feature_flags=[], expect_fail=False):
    node.stop_clickhouse()
    node.copy_file_to_container(
        os.path.join(CURRENT_TEST_DIR, "configs/enable_keeper.xml"),
        "/etc/clickhouse-server/config.d/enable_keeper.xml",
    )

    if len(feature_flags) > 0:
        feature_flags_config = "<feature_flags>"

        for feature, is_enabled in feature_flags:
            feature_flags_config += f"<{feature}>{is_enabled}<\\/{feature}>"

        feature_flags_config += "<\\/feature_flags>"

        node.replace_in_config(
            "/etc/clickhouse-server/config.d/enable_keeper.xml",
            "<!-- FEATURE FLAGS -->",
            feature_flags_config,
        )

    node.start_clickhouse(expected_to_fail=expect_fail)

    if not expect_fail:
        keeper_utils.wait_until_connected(cluster, node)


def test_keeper_feature_flags(started_cluster):
    restart_clickhouse()

    def assert_feature_flags(feature_flags):
        res = keeper_utils.send_4lw_cmd(started_cluster, node, "ftfl")
        node.query("SYSTEM FLUSH LOGS")
        for feature, is_enabled in feature_flags:
            node.wait_for_log_line(
                f"ZooKeeperClient: Keeper feature flag {feature.upper()}: {'enabled' if is_enabled else 'disabled'}",
                look_behind_lines="+1",
            )

            node.wait_for_log_line(
                f"KeeperContext: Keeper feature flag {feature.upper()}: {'enabled' if is_enabled else 'disabled'}",
                look_behind_lines="+1",
            )

            assert f"{feature}\t{1 if is_enabled else 0}" in res

    assert_feature_flags(
        [("filtered_list", 1), ("multi_read", 1), ("remove_recursive", 1)]
    )

    feature_flags = [
        ("multi_read", 0),
        ("remove_recursive", 1),
        ("create_if_not_exists", 1),
    ]
    restart_clickhouse(feature_flags)
    assert_feature_flags(feature_flags + [("filtered_list", 1)])

    feature_flags = [
        ("multi_read", 0),
        ("check_not_exists", 0),
        ("filtered_list", 0),
        ("create_if_not_exists", 0),
    ]
    restart_clickhouse(feature_flags)
    assert_feature_flags(feature_flags)

    restart_clickhouse([("invalid_feature", 1)], expect_fail=True)
