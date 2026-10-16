#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "zb/guest_thread.h"
#include "zb/input_backend.h"
#include "zb/jni_handles.h"
#include "zb/library_runtime.h"

namespace zb {

// AInputQueue_* and the input event accessors.
//
// Queues and events are host objects the framework owns; the guest sees 32-bit handles. An event
// handle is valid from AInputQueue_getEvent until AInputQueue_finishEvent, and an accessor on a
// handle that is not live is rejected and recorded rather than followed: a guest that keeps an
// event past its life would otherwise read framework memory that has been reused.
class HostInput {
public:
    // Called when the guest attaches a queue to its looper: the calling guest thread must end up
    // with a real Android looper, because a host AInputQueue has no descriptor we could poll.
    // Without one (the host build, or a platform that refuses) the attach is recorded and the
    // queue simply never delivers, which the report says.
    // (thread, the looper handle the guest named, out: the real looper to attach to).
    using RealLooperRequest = std::function<bool(GuestThread& thread, std::uint32_t looper, std::uint64_t& real)>;
    // The guest tid that owns a guest looper handle, for the report.
    using LooperOwner = std::function<std::int32_t(std::uint32_t looper)>;

    HostInput(LibraryRuntime& runtime, InputBackend& backend, RealLooperRequest real_looper = {},
              LooperOwner looper_owner = {})
        : runtime_(runtime), backend_(backend), real_looper_(std::move(real_looper)),
          looper_owner_(std::move(looper_owner)) {}
    HostInput(const HostInput&) = delete;
    HostInput& operator=(const HostInput&) = delete;

    // Serves ZB_INPUT_HC_* indices; returns false for any other index.
    bool handle_host_call(std::uint32_t index, GuestThread& thread);

    // The framework created or destroyed a queue (ANativeActivity's onInputQueueCreated and
    // onInputQueueDestroyed). The handle is what the guest callback receives.
    std::uint32_t add_queue(void* queue);
    // Drops the queue and every event still outstanding on it, and says how many those were.
    std::size_t remove_queue(std::uint32_t handle);

    // The host queue behind a handle, for the glue; nullptr when the handle is not live.
    void* queue_for(std::uint32_t handle) const;

    // What an attached queue reports to the looper it was attached to.
    struct Ready {
        std::int32_t ident = 0;
        std::uint32_t data = 0;
    };
    // The queue attached to `real_looper` that has events waiting, if any. An input queue does
    // not only make a descriptor readable: an event that arrived through Java is put on the
    // queue and the looper is simply woken, so a guest that waits for its ident would wait
    // forever. The poll asks this after every wake-up.
    std::optional<Ready> ready_on(std::uint64_t real_looper);

    // How long the guest may go without touching any input call before the keeper below starts
    // finishing events for it. Android's input-dispatch ANR is five seconds; this stays well
    // under it, and is long enough that a guest which is merely idle between frames is never
    // affected (it drains nothing while its queue is empty, and any poll resets the clock).
    static constexpr std::int64_t kGuestInputStallMs = 3500;

private:
    const void* live_event(std::uint32_t handle) const;
    void* live_queue(std::uint32_t handle) const;
    void report_queue_state();
    // Records that the guest made progress through its input path; resets the keeper's clock.
    void note_input_progress();
    // Starts the one detached keeper thread, on first use.
    void start_input_keeper();
    // The keeper: while the guest has stopped calling its input path, takes events out of every
    // attached queue and finishes them, so the framework's five-second input-dispatch timeout can
    // never fire and kill the process. A touch dropped this way is lost; the alternative the
    // platform offers for not finishing one is an ANR, which is a crash.
    void input_keeper_loop();
    // Records the rejection and answers the call with zero. Always returns true: the call was
    // served, and the guest sees what a real device returns for an event it no longer owns.
    bool reject(const char* function, std::uint32_t handle);

    LibraryRuntime& runtime_;
    InputBackend& backend_;
    RealLooperRequest real_looper_;
    LooperOwner looper_owner_;
    GlobalHandles queues_{HandleKind::Global};
    GlobalHandles events_{HandleKind::Global};
    mutable std::mutex mutex_;
    // Every live event handle and the queue it came from, so a destroyed queue takes its
    // outstanding events with it instead of leaving handles that point at freed memory.
    std::unordered_map<std::uint32_t, std::uint32_t> live_events_;
    std::string first_rejection_;
    struct Attachment {
        std::uint64_t real_looper = 0;
        std::int32_t ident = 0;
        std::uint32_t data = 0;
    };
    std::unordered_map<std::uint32_t, Attachment> attachments_;  // by queue handle
    // Steady-clock milliseconds of the guest's last input call, and the keeper's own state.
    std::atomic<std::int64_t> last_input_progress_ms_{0};
    std::atomic<bool> input_keeper_started_{false};
    std::size_t stalled_events_finished_ = 0;
    std::size_t rejections_ = 0;
    std::size_t events_gotten_ = 0;
    std::size_t events_pre_dispatched_ = 0;
    std::size_t events_finished_ = 0;
    std::size_t events_released_ = 0;
};

}  // namespace zb
