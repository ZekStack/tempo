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
    "src/internal/tempo_scheduler/scheduler.cpp",
    '''\t\timpl_->stopExecutors(waitForRunningJobs);
\t\timpl_->service->stop();
\t\timpl_->service.reset();
''',
    '''\t\timpl_->stopExecutors(waitForRunningJobs);
\t\tif (impl_->runtime) {
\t\t\timpl_->runtime->eventQueue.store(nullptr);
\t\t\timpl_->runtime->accepting.store(false);
\t\t}
\t\timpl_->service->stop();
\t\timpl_->service.reset();
''',
)
replace_once(
    "src/internal/tempo_scheduler/scheduler.cpp",
    '''\t\tif (impl_->runtime) {
\t\t\tif (!waitForRunningJobs) {
\t\t\t\timpl_->runtime->eventQueue.store(nullptr);
\t\t\t\timpl_->runtime->accepting.store(false);
\t\t\t}
\t\t}
\t\timpl_->stopExecutors(waitForRunningJobs);
\t\tif (impl_->eventQueue) {
''',
    '''\t\tif (impl_->runtime) {
\t\t\timpl_->runtime->eventQueue.store(nullptr);
\t\t\timpl_->runtime->accepting.store(false);
\t\t}
\t\timpl_->stopExecutors(waitForRunningJobs);
\t\tif (impl_->eventQueue) {
''',
)
replace_once(
    "src/internal/tempo_scheduler/executors/worker_pool_executor.cpp",
    '''\t\tcontext->owner = this;

\t\tTaskHandle_t handle = nullptr;
\t\tbool createdWithCaps = false;
''',
    '''\t\tcontext->owner = this;
\t\tcontext->createdWithCaps = config_.usePsramStack;

\t\tTaskHandle_t handle = nullptr;
\t\tbool createdWithCaps = false;
''',
)
replace_once(
    "src/internal/tempo_scheduler/executors/worker_pool_executor.cpp",
    '''\t\tcontext->createdWithCaps = createdWithCaps;
\t\tif (!workers_.pushBack({handle, createdWithCaps})) {
''',
    '''\t\tif (createdWithCaps != context->createdWithCaps) {
\t\t\tscheduler_task_support::deleteTask(handle, createdWithCaps);
\t\t\tdelete context;
\t\t\tend(false);
\t\t\treturn false;
\t\t}
\t\tif (!workers_.pushBack({handle, createdWithCaps})) {
''',
)
replace_once(
    "src/internal/tempo_scheduler/executors/dedicated_task_executor.cpp",
    '''\tcontext->state = state;
\tstate->activeTasks.fetch_add(1);

\tTaskHandle_t handle = nullptr;
\tconst DedicatedTaskOptions &task = invocation.dedicatedTask;
''',
    '''\tcontext->state = state;
\tconst DedicatedTaskOptions &task = invocation.dedicatedTask;
\tcontext->createdWithCaps = task.usePsramStack;
\tstate->activeTasks.fetch_add(1);

\tTaskHandle_t handle = nullptr;
\tbool createdWithCaps = false;
''',
)
replace_once(
    "src/internal/tempo_scheduler/executors/dedicated_task_executor.cpp",
    '''\t    task.coreId,
\t    task.usePsramStack,
\t    context->createdWithCaps
\t);
\tif (created != pdPASS || handle == nullptr) {
''',
    '''\t    task.coreId,
\t    task.usePsramStack,
\t    createdWithCaps
\t);
\tif (created != pdPASS || handle == nullptr || createdWithCaps != context->createdWithCaps) {
''',
)

Path("scripts/fix_scheduler_shutdown_queue.py").unlink(missing_ok=True)
Path(".github/workflows/fix-scheduler-shutdown-queue.yml").unlink(missing_ok=True)
