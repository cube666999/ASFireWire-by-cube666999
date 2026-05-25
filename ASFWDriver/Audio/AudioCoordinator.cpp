// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (c) 2026 ASFireWire Project

#include "AudioCoordinator.hpp"

#include "../Discovery/FWDevice.hpp"
#include "Model/ASFWAudioDevice.hpp"

#include <algorithm>

namespace ASFW::Audio {

AudioCoordinator::AudioCoordinator(IOService* driver,
                                   Discovery::IDeviceManager& deviceManager,
                                   Discovery::DeviceRegistry& registry,
                                   Driver::IsochService& isoch,
                                   Driver::HardwareInterface& hardware) noexcept
    : publisher_(driver)
    , dice_(publisher_, registry, isoch, hardware)
    , avc_(publisher_, registry, isoch, hardware)
    , motuV3_(publisher_, registry, isoch, hardware)
    , deviceManager_(deviceManager)
    , registry_(registry) {
    lock_ = IOLockAlloc();
    if (!lock_) {
        ASFW_LOG_ERROR(Audio, "AudioCoordinator: Failed to allocate lock");
    }

    deviceManager_.RegisterDeviceObserver(this);
    ASFW_LOG(Audio, "AudioCoordinator: Registered device observer");
}

AudioCoordinator::~AudioCoordinator() noexcept {
    deviceManager_.UnregisterDeviceObserver(this);

    if (lock_) {
        IOLockFree(lock_);
        lock_ = nullptr;
    }
}

void AudioCoordinator::SetCMPClient(ASFW::CMP::CMPClient* client) noexcept {
    avc_.SetCMPClient(client);
}

void AudioCoordinator::SetIRMClient(ASFW::IRM::IRMClient* client) noexcept {
    avc_.SetIRMClient(client);
    motuV3_.SetIRMClient(client);
}

void AudioCoordinator::SetBusOps(Async::IFireWireBusOps* busOps) noexcept {
    motuV3_.SetBusOps(busOps);
}

void AudioCoordinator::OnDeviceAdded(std::shared_ptr<Discovery::FWDevice> device) {
    if (!device) return;

    const uint64_t guid = device->GetGUID();

    // DICE path — hardcoded nub, same as before.
    dice_.OnDeviceRecordUpdated(guid);

    // MOTU V3 path: AV/C FCP is not supported by MOTU hardware; the AVC discovery
    // path will silently time out and never call OnAVCAudioConfigurationReady.
    // Inject a hardcoded config directly so MOTUAudioBackend::StartStreaming can
    // proceed once the UserClient requests streaming.
    const auto* record = registry_.FindByGuid(guid);
    if (!record) return;

    const uint32_t effectiveModel = DeviceProtocolFactory::EffectiveModelId(
        record->vendorId, record->modelId, record->unitSwVersion);
    if (DeviceProtocolFactory::LookupIntegrationMode(record->vendorId, effectiveModel)
            != DeviceIntegrationMode::kMOTUV3) {
        return;
    }

    const auto layout = DeviceProtocolFactory::GetMOTUV3ChannelLayout(effectiveModel);

    Model::ASFWAudioDevice config;
    config.guid               = guid;
    config.vendorId           = record->vendorId;
    config.modelId            = effectiveModel;
    config.deviceName         = DeviceProtocolFactory::GetMOTUV3DeviceName(effectiveModel);
    config.inputChannelCount  = layout.inputChannels;
    config.outputChannelCount = layout.outputChannels;
    config.channelCount       = std::max(layout.inputChannels, layout.outputChannels);
    config.currentSampleRate  = 48000u;
    // Supported sample rates confirmed via ioreg on Sequoia with MOTU 828 MK3.
    config.sampleRates        = {44100u, 48000u, 88200u, 96000u, 176400u, 192000u};
    // MOTU V3 uses blocking isochronous transfer (fixed 8-sample + NO-DATA cadence).
    config.streamMode         = Model::StreamMode::kBlocking;

    ASFW_LOG(Audio,
             "AudioCoordinator: Injecting MOTU V3 config GUID=0x%016llx model=0x%06x "
             "in=%u out=%u",
             guid, effectiveModel,
             config.inputChannelCount, config.outputChannelCount);

    motuV3_.OnAudioConfigurationReady(guid, config);
}

void AudioCoordinator::OnDeviceResumed(std::shared_ptr<Discovery::FWDevice> device) {
    if (!device) return;
    const uint64_t guid = device->GetGUID();

    dice_.OnDeviceRecordUpdated(guid);

    bool shouldRestart = false;
    if (lock_) {
        IOLockLock(lock_);
        auto it = suspendedGuids_.find(guid);
        if (it != suspendedGuids_.end()) {
            suspendedGuids_.erase(it);
            shouldRestart = true;
        }
        IOLockUnlock(lock_);
    }

    if (!shouldRestart) return;

    ASFW_LOG(Audio,
             "AudioCoordinator: Restarting streaming after bus reset GUID=0x%016llx",
             guid);
    const IOReturn kr = StartStreaming(guid);
    if (kr != kIOReturnSuccess) {
        ASFW_LOG_WARNING(Audio,
                         "AudioCoordinator: Restart after bus reset failed GUID=0x%016llx kr=0x%x",
                         guid, kr);
    }
}

void AudioCoordinator::OnDeviceSuspended(std::shared_ptr<Discovery::FWDevice> device) {
    if (!device) return;
    const uint64_t guid = device->GetGUID();

    bool wasStreaming = false;
    if (lock_) {
        IOLockLock(lock_);
        if (activeGuid_ == guid) {
            activeGuid_ = 0;
            suspendedGuids_.insert(guid);
            wasStreaming = true;
        }
        IOLockUnlock(lock_);
    }

    if (!wasStreaming) return;

    // Bus reset terminates all isochronous connections per IEEE 1394 §8.3.
    // Stop IR/IT and release IRM/CMP resources so they can be reallocated on resume.
    ASFW_LOG(Audio,
             "AudioCoordinator: Bus reset during streaming GUID=0x%016llx — stopping backend",
             guid);
    auto* backend = BackendForGuid(guid);
    if (backend) {
        (void)backend->StopStreaming(guid);
    }
}

void AudioCoordinator::OnDeviceRemoved(Discovery::Guid64 guid) {
    if (guid == 0) return;

    // Ensure isoch transport is stopped (best-effort) and nubs are terminated.
    dice_.OnDeviceRemoved(guid);
    avc_.OnDeviceRemoved(guid);
    motuV3_.OnDeviceRemoved(guid);

    if (lock_) {
        IOLockLock(lock_);
        if (activeGuid_ == guid) {
            activeGuid_ = 0;
        }
        IOLockUnlock(lock_);
    }
}

void AudioCoordinator::OnAVCAudioConfigurationReady(uint64_t guid,
                                                   const Model::ASFWAudioDevice& config) noexcept {
    // Route to correct backend based on device vendor/model.
    const auto* record = registry_.FindByGuid(guid);
    if (record) {
        const uint32_t effectiveModel = DeviceProtocolFactory::EffectiveModelId(
            record->vendorId, record->modelId, record->unitSwVersion);
        const auto integration = DeviceProtocolFactory::LookupIntegrationMode(
            record->vendorId, effectiveModel);
        if (integration == DeviceIntegrationMode::kMOTUV3) {
            motuV3_.OnAudioConfigurationReady(guid, config);
            return;
        }
    }
    avc_.OnAudioConfigurationReady(guid, config);
}

IAudioBackend* AudioCoordinator::BackendForGuid(uint64_t guid) noexcept {
    if (guid == 0) return nullptr;

    const auto* record = registry_.FindByGuid(guid);
    if (!record) {
        return &avc_;
    }

    const uint32_t effectiveModel = DeviceProtocolFactory::EffectiveModelId(
        record->vendorId, record->modelId, record->unitSwVersion);
    const auto integration = DeviceProtocolFactory::LookupIntegrationMode(record->vendorId, effectiveModel);
    if (integration == DeviceIntegrationMode::kHardcodedNub) {
        return &dice_;
    }
    if (integration == DeviceIntegrationMode::kMOTUV3) {
        return &motuV3_;
    }

    return &avc_;
}

IOReturn AudioCoordinator::StartStreaming(uint64_t guid) noexcept {
    if (guid == 0) return kIOReturnBadArgument;

    bool setActive = false;
    if (lock_) {
        IOLockLock(lock_);
        if (activeGuid_ == 0) {
            activeGuid_ = guid;
            setActive = true;
        } else if (activeGuid_ == guid) {
            IOLockUnlock(lock_);
            // Idempotent start: avoid reconfiguring already-running IR/IT contexts.
            return kIOReturnSuccess;
        } else {
            const uint64_t active = activeGuid_;
            IOLockUnlock(lock_);

            ASFW_LOG_WARNING(Audio,
                             "AudioCoordinator: StartStreaming busy requested=0x%016llx active=0x%016llx",
                             guid,
                             active);
            // TODO(ASFW-MULTIDEVICE): Multi-device streaming is not implemented.
            // We currently have a single global IR/IT transport and single external SYT clock bridge.
            // Supporting multiple devices requires per-GUID IR/IT contexts, per-device queue wiring,
            // and a GUID-keyed clock discipline pipeline.
            return kIOReturnBusy;
        }
        IOLockUnlock(lock_);
    }

    auto* backend = BackendForGuid(guid);
    if (!backend) {
        if (setActive && lock_) {
            IOLockLock(lock_);
            if (activeGuid_ == guid) activeGuid_ = 0;
            IOLockUnlock(lock_);
        }
        return kIOReturnNotReady;
    }

    const IOReturn kr = backend->StartStreaming(guid);
    if (kr != kIOReturnSuccess) {
        ASFW_LOG_ERROR(Audio,
                       "AudioCoordinator: StartStreaming failed backend=%{public}s GUID=0x%016llx kr=0x%x",
                       backend->Name(),
                       guid,
                       kr);
        if (setActive && lock_) {
            IOLockLock(lock_);
            if (activeGuid_ == guid) activeGuid_ = 0;
            IOLockUnlock(lock_);
        }
        return kr;
    }

    ASFW_LOG(Audio,
             "AudioCoordinator: StartStreaming ok backend=%{public}s GUID=0x%016llx",
             backend->Name(),
             guid);
    return kIOReturnSuccess;
}

IOReturn AudioCoordinator::StopStreaming(uint64_t guid) noexcept {
    if (guid == 0) return kIOReturnBadArgument;

    if (lock_) {
        IOLockLock(lock_);
        if (activeGuid_ != 0 && activeGuid_ != guid) {
            const uint64_t active = activeGuid_;
            IOLockUnlock(lock_);
            ASFW_LOG_WARNING(Audio,
                             "AudioCoordinator: StopStreaming busy requested=0x%016llx active=0x%016llx",
                             guid,
                             active);
            return kIOReturnBusy;
        }
        IOLockUnlock(lock_);
    }

    auto* backend = BackendForGuid(guid);
    if (!backend) return kIOReturnNotReady;

    const IOReturn kr = backend->StopStreaming(guid);
    if (kr != kIOReturnSuccess) {
        ASFW_LOG_ERROR(Audio,
                       "AudioCoordinator: StopStreaming failed backend=%{public}s GUID=0x%016llx kr=0x%x",
                       backend->Name(),
                       guid,
                       kr);
        return kr;
    }

    if (lock_) {
        IOLockLock(lock_);
        if (activeGuid_ == guid) activeGuid_ = 0;
        IOLockUnlock(lock_);
    }

    ASFW_LOG(Audio,
             "AudioCoordinator: StopStreaming ok backend=%{public}s GUID=0x%016llx",
             backend->Name(),
             guid);
    return kIOReturnSuccess;
}

std::optional<uint64_t> AudioCoordinator::GetSinglePublishedGuid() const noexcept {
    // AudioNubPublisher is the source of truth for published audio endpoints.
    // This is intentionally used only for debug paths that still lack GUID selection.
    return publisher_.GetSingleGuid();
}

} // namespace ASFW::Audio
