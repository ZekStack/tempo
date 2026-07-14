#!/usr/bin/env python3
from pathlib import Path

path = Path("src/internal/tempo_scheduler/scheduler.cpp")
text = path.read_text(encoding="utf-8")

old_background = '''\t\timpl_->stopExecutors(waitForRunningJobs);
\t\timpl_->service->stop();
\t\timpl_->service.reset();
'''
new_background = '''\t\timpl_->stopExecutors(waitForRunningJobs);
\t\tif (impl_->runtime) {
\t\t\timpl_->runtime->eventQueue.store(nullptr);
\t\t\timpl_->runtime->accepting.store(false);
\t\t}
\t\timpl_->service->stop();
\t\timpl_->service.reset();
'''
if text.count(old_background) != 1:
    raise RuntimeError("background shutdown block did not match")
text = text.replace(old_background, new_background, 1)

old_manual = '''\t\timpl_->stopExecutors(waitForRunningJobs);
\t\tif (impl_->eventQueue) {
\t\t\tvQueueDelete(impl_->eventQueue);
'''
new_manual = '''\t\timpl_->stopExecutors(waitForRunningJobs);
\t\tif (impl_->runtime) {
\t\t\timpl_->runtime->eventQueue.store(nullptr);
\t\t\timpl_->runtime->accepting.store(false);
\t\t}
\t\tif (impl_->eventQueue) {
\t\t\tvQueueDelete(impl_->eventQueue);
'''
if text.count(old_manual) != 1:
    raise RuntimeError("manual shutdown block did not match")
text = text.replace(old_manual, new_manual, 1)

path.write_text(text, encoding="utf-8")
Path("scripts/fix_scheduler_shutdown_queue.py").unlink(missing_ok=True)
Path(".github/workflows/fix-scheduler-shutdown-queue.yml").unlink(missing_ok=True)
