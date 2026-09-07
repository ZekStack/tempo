#pragma once

#include <atomic>
#include <memory>

#include "../scheduler.h"
#include "../service/scheduler_events.h"

enum class CallbackKind : uint8_t {
	RawFunction = 0,
	OwningFunction,
};

struct SchedulerExecutorRuntime {
	using PostEventFn = bool (*)(void *context, const SchedulerEvent &event);

	std::atomic<bool> accepting{true};
	std::atomic<PostEventFn> postEvent{nullptr};
	std::atomic<void *> postEventContext{nullptr};

	bool publish(const SchedulerEvent &event) const {
		if (!accepting.load(std::memory_order_acquire)) {
			return false;
		}
		PostEventFn fn = postEvent.load(std::memory_order_acquire);
		void *context = postEventContext.load(std::memory_order_acquire);
		return fn != nullptr && fn(context, event);
	}
};

struct CallbackRef {
	CallbackKind kind = CallbackKind::RawFunction;
	SchedulerCallbackFn rawFn = nullptr;
	void *userData = nullptr;
	std::shared_ptr<SchedulerFunction> owningFn{};

	bool valid() const {
		return rawFn != nullptr || static_cast<bool>(owningFn);
	}

	void invoke() const {
#if defined(__cpp_exceptions)
		try {
#endif
			if (owningFn) {
				(*owningFn)(userData);
				return;
			}
			if (rawFn) {
				rawFn(userData);
			}
#if defined(__cpp_exceptions)
		} catch (...) {
		}
#endif
	}
};

struct JobInvocation {
	uint32_t jobId = 0;
	uint32_t generation = 0;
	size_t slotIndex = 0;
	const char *name = nullptr;
	CallbackRef callback{};
	DedicatedTaskOptions dedicatedTask{};
	std::shared_ptr<SchedulerExecutorRuntime> runtime{};
};

class ISchedulerExecutor {
  public:
	virtual ~ISchedulerExecutor() = default;

	virtual bool begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) = 0;
	virtual void end(bool drainRunningJobs) = 0;
	virtual bool submit(const JobInvocation &invocation) = 0;
	virtual void reapCompleted() {
	}
	virtual const char *name() const = 0;
};

class IExecutorResolver {
  public:
	virtual ~IExecutorResolver() = default;
	virtual ISchedulerExecutor *inlineExecutor() = 0;
	virtual ISchedulerExecutor *executorFor(uint8_t executorId) = 0;
	virtual void reapCompletedExecutors() = 0;
};
