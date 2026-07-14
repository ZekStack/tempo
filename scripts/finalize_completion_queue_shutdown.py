#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}\n--- old ---\n{old}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "src/internal/tempo_scheduler/executors/scheduler_executor.h",
    '''struct SchedulerExecutorRuntime {
\tstd::atomic<bool> accepting{true};
\tstd::atomic<QueueHandle_t> eventQueue{nullptr};
};
''',
    '''struct SchedulerExecutorRuntime {
\tstd::atomic<bool> accepting{true};
\tstd::atomic<QueueHandle_t> eventQueue{nullptr};
\tstd::atomic<size_t> completionSenders{0};
};
''',
)

old_completion = '''bool postCompletion(
    const std::shared_ptr<SchedulerExecutorRuntime> &runtime,
    uint32_t jobId,
    uint32_t generation,
    size_t slotIndex
) {
\tif (!runtime) {
\t\treturn false;
\t}
\tSchedulerEvent event{};
\tevent.kind = SchedulerEventKind::JobFinished;
\tevent.jobId = jobId;
\tevent.generation = generation;
\tevent.slotIndex = slotIndex;
\twhile (runtime->accepting.load(std::memory_order_acquire)) {
\t\tQueueHandle_t queue = runtime->eventQueue.load(std::memory_order_acquire);
\t\tif (!queue) {
\t\t\treturn false;
\t\t}
\t\tif (xQueueSend(queue, &event, pdMS_TO_TICKS(50)) == pdTRUE) {
\t\t\treturn true;
\t\t}
\t}
\treturn false;
}
'''
new_completion = '''bool postCompletion(
    const std::shared_ptr<SchedulerExecutorRuntime> &runtime,
    uint32_t jobId,
    uint32_t generation,
    size_t slotIndex
) {
\tif (!runtime) {
\t\treturn false;
\t}
\truntime->completionSenders.fetch_add(1, std::memory_order_acq_rel);

\tbool posted = false;
\tSchedulerEvent event{};
\tevent.kind = SchedulerEventKind::JobFinished;
\tevent.jobId = jobId;
\tevent.generation = generation;
\tevent.slotIndex = slotIndex;
\twhile (runtime->accepting.load(std::memory_order_acquire)) {
\t\tQueueHandle_t queue = runtime->eventQueue.load(std::memory_order_acquire);
\t\tif (!queue) {
\t\t\tbreak;
\t\t}
\t\tif (xQueueSend(queue, &event, pdMS_TO_TICKS(50)) == pdTRUE) {
\t\t\tposted = true;
\t\t\tbreak;
\t\t}
\t}

\truntime->completionSenders.fetch_sub(1, std::memory_order_acq_rel);
\treturn posted;
}
'''
replace_once(
    "src/internal/tempo_scheduler/executors/worker_pool_executor.cpp",
    old_completion,
    new_completion,
)
replace_once(
    "src/internal/tempo_scheduler/executors/dedicated_task_executor.cpp",
    old_completion,
    new_completion,
)

replace_once(
    "src/internal/tempo_scheduler/scheduler.cpp",
    '''bool validSchedulerConfig(const SchedulerConfig &config) {
\treturn config.service.commandQueueDepth > 0 && config.service.eventQueueDepth > 0 &&
\t       config.service.taskStackSize > 0 && config.service.controlTimeoutMs > 0 &&
\t       config.defaultWorkerPool.workerCount > 0 &&
\t       config.defaultWorkerPool.queueDepth > 0 && config.defaultWorkerPool.stackSize > 0 &&
\t       config.defaultDedicatedTask.stackSize > 0;
}
''',
    '''bool validSchedulerConfig(const SchedulerConfig &config) {
\treturn config.service.commandQueueDepth > 0 && config.service.eventQueueDepth > 0 &&
\t       config.service.taskStackSize > 0 && config.service.controlTimeoutMs > 0 &&
\t       config.defaultWorkerPool.workerCount > 0 &&
\t       config.defaultWorkerPool.queueDepth > 0 && config.defaultWorkerPool.stackSize > 0 &&
\t       config.defaultDedicatedTask.stackSize > 0;
}

void closeCompletionQueue(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) {
\tif (!runtime) {
\t\treturn;
\t}
\truntime->accepting.store(false, std::memory_order_release);
\truntime->eventQueue.store(nullptr, std::memory_order_release);
\twhile (runtime->completionSenders.load(std::memory_order_acquire) > 0) {
\t\tvTaskDelay(pdMS_TO_TICKS(1));
\t}
}
''',
)
replace_once(
    "src/internal/tempo_scheduler/scheduler.cpp",
    '''\t\tif (impl_->runtime) {
\t\t\timpl_->runtime->accepting.store(waitForRunningJobs);
\t\t\tif (!waitForRunningJobs) {
\t\t\t\timpl_->runtime->eventQueue.store(nullptr);
\t\t\t\timpl_->runtime->accepting.store(false);
\t\t\t}
\t\t}
\t\timpl_->stopExecutors(waitForRunningJobs);
\t\tif (impl_->runtime) {
\t\t\timpl_->runtime->eventQueue.store(nullptr);
\t\t\timpl_->runtime->accepting.store(false);
\t\t}
''',
    '''\t\tcloseCompletionQueue(impl_->runtime);
\t\timpl_->stopExecutors(waitForRunningJobs);
''',
)
replace_once(
    "src/internal/tempo_scheduler/scheduler.cpp",
    '''\t\tif (impl_->runtime) {
\t\t\timpl_->runtime->eventQueue.store(nullptr);
\t\t\timpl_->runtime->accepting.store(false);
\t\t}
\t\timpl_->stopExecutors(waitForRunningJobs);
''',
    '''\t\tcloseCompletionQueue(impl_->runtime);
\t\timpl_->stopExecutors(waitForRunningJobs);
''',
)

Path("scripts/finalize_completion_queue_shutdown.py").unlink(missing_ok=True)
Path(".github/workflows/finalize-completion-queue-shutdown.yml").unlink(missing_ok=True)
