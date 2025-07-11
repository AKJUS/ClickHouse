#pragma once
#include <Storages/MergeTree/MergeTreeReadPoolBase.h>
#include <Storages/MergeTree/MergeTreeSelectProcessor.h>

namespace DB
{

class MergeTreeReadPoolParallelReplicasInOrder : public MergeTreeReadPoolBase
{
public:
    MergeTreeReadPoolParallelReplicasInOrder(
        ParallelReadingExtension extension_,
        CoordinationMode mode_,
        RangesInDataParts parts_,
        MutationsSnapshotPtr mutations_snapshot_,
        VirtualFields shared_virtual_fields_,
        bool has_limit_below_one_block_,
        const StorageSnapshotPtr & storage_snapshot_,
        const PrewhereInfoPtr & prewhere_info_,
        const ExpressionActionsSettings & actions_settings_,
        const MergeTreeReaderSettings & reader_settings_,
        const Names & column_names_,
        const PoolSettings & settings_,
        const MergeTreeReadTask::BlockSizeParams & params_,
        const ContextPtr & context_);

    String getName() const override { return "ReadPoolParallelReplicasInOrder"; }
    bool preservesOrderOfRanges() const override { return true; }
    void profileFeedback(ReadBufferFromFileBase::ProfileInfo) override {}
    MergeTreeReadTaskPtr getTask(size_t task_idx, MergeTreeReadTask * previous_task) override;

private:
    LoggerPtr log = getLogger("MergeTreeReadPoolParallelReplicasInOrder");
    const ParallelReadingExtension extension;
    const CoordinationMode mode;
    const bool has_limit_below_one_block;

    size_t min_marks_per_task{0};
    bool no_more_tasks{false};
    RangesInDataPartsDescription request;
    RangesInDataPartsDescription buffered_tasks;

    mutable std::mutex mutex;
    std::vector<size_t> per_part_marks_in_range;
};

};
