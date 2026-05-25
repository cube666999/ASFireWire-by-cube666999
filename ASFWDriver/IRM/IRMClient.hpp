#pragma once

#include "IRMTypes.hpp"
#include "../Async/Interfaces/IFireWireBusOps.hpp"
#include <functional>
#include <memory>
#include <array>
// #include <libkern/OSByteOrder.h>

namespace ASFW::IRM {

/**
 * Callback for IRM allocation operations.
 * Invoked asynchronously when allocation completes (success or failure).
 *
 * @param status Result of allocation operation
 */
using AllocationCallback = std::function<void(AllocationStatus status)>;

class IRMClient {
public:
    explicit IRMClient(Async::IFireWireBusOps& busOps);
    ~IRMClient();

    void SetIRMNode(uint8_t irmNodeId, Generation generation);

    /// Tell IRMClient which node is the local (self) node.
    /// Must be called alongside SetIRMNode so the client can detect the
    /// self-addressed case and use software shadow registers instead of
    /// async transactions (OHCI does not loopback self-addressed AT→AR packets).
    void SetLocalNode(uint8_t localNodeId);

    void AllocateChannel(uint8_t channel,
                        AllocationCallback callback,
                        const RetryPolicy& retryPolicy = RetryPolicy::Default());

    void ReleaseChannel(uint8_t channel,
                       AllocationCallback callback,
                       const RetryPolicy& retryPolicy = RetryPolicy::Default());

    void AllocateBandwidth(uint32_t units,
                          AllocationCallback callback,
                          const RetryPolicy& retryPolicy = RetryPolicy::Default());

    void ReleaseBandwidth(uint32_t units,
                         AllocationCallback callback,
                         const RetryPolicy& retryPolicy = RetryPolicy::Default());

    void AllocateResources(uint8_t channel,
                          uint32_t bandwidthUnits,
                          AllocationCallback callback,
                          const RetryPolicy& retryPolicy = RetryPolicy::Default());

    void ReleaseResources(uint8_t channel,
                         uint32_t bandwidthUnits,
                         AllocationCallback callback,
                         const RetryPolicy& retryPolicy = RetryPolicy::Default());

    [[nodiscard]] uint8_t GetIRMNodeID() const { return irmNodeId_; }

    [[nodiscard]] Generation GetGeneration() const { return generation_; }

private:
    Async::IFireWireBusOps& busOps_;

    uint8_t irmNodeId_{0xFF};
    uint8_t localNodeId_{0xFF};
    Generation generation_{0};

    // Software shadow registers — used when this node is the IRM.
    // OHCI does not loopback self-addressed AT→AR packets, so async reads
    // to self never complete. Shadows are reset to IEEE 1394 initial values
    // on every SetLocalNode() call (mirrors bus-reset behaviour).
    uint32_t shadowBandwidth_{kMaxBandwidthUnitsS400};
    uint32_t shadowChannelsLo_{kChannelsAvailableInitial};
    uint32_t shadowChannelsHi_{kChannelsAvailableInitial};

    [[nodiscard]] bool IsLocalIRM() const noexcept {
        return irmNodeId_ != 0xFF && irmNodeId_ == localNodeId_;
    }

    void ReadIRMQuadlet(
        uint32_t addressLo,
        std::function<void(bool success, uint32_t value)> callback);

    void CompareSwapIRMQuadlet(
        uint32_t addressLo,
        uint32_t expected,
        uint32_t desired,
        std::function<void(bool success, uint32_t oldValue)> callback);

    void PerformChannelLock(uint8_t channel, bool allocate,
                           AllocationCallback callback,
                           const RetryPolicy& retryPolicy);

    void PerformBandwidthLock(uint32_t units, bool allocate,
                             AllocationCallback callback,
                             const RetryPolicy& retryPolicy);
};

} // namespace ASFW::IRM
