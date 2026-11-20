#pragma once

#include <atomic>
#include <exception>
#include <functional>
#include <thread>

namespace mfc_tool::core {

// UI owns completion; the worker never owns a window or posts heap pointers.
class BridgeJob {
public:
    using Work = std::function<void()>;
    using Completion = std::function<void(std::exception_ptr)>;
    ~BridgeJob() { if (worker_.joinable()) worker_.join(); }
    bool Busy() const noexcept { return worker_.joinable(); }
    bool Start(Work work, Completion completion) {
        if (Busy()) return false;
        done_ = false;
        error_ = nullptr;
        completion_ = std::move(completion);
        try {
            worker_ = std::thread([this, work = std::move(work)] {
                try { work(); } catch (...) { error_ = std::current_exception(); }
                done_.store(true, std::memory_order_release);
            });
        } catch (...) { completion_ = {}; throw; }
        return true;
    }
    bool Ready() const noexcept { return Busy() && done_.load(std::memory_order_acquire); }
    bool Consume(bool discard = false) {
        if (!Ready()) return false;
        worker_.join();
        auto completion = std::move(completion_);
        completion_ = {};
        if (!discard && completion) completion(error_);
        return true;
    }
private:
    std::thread worker_;
    std::atomic<bool> done_{false};
    std::exception_ptr error_;
    Completion completion_;
};

using BridgeSubmit = std::function<bool(BridgeJob::Work, BridgeJob::Completion)>;

} // namespace mfc_tool::core
