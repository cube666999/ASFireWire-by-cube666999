#include "AudioDriverConfig.hpp"

#include <DriverKit/OSArray.h>
#include <DriverKit/OSBoolean.h>
#include <DriverKit/OSDictionary.h>
#include <DriverKit/OSNumber.h>
#include <DriverKit/OSString.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ASFW::Isoch::Audio {
namespace {

[[nodiscard]] bool ReadOSBoolValue(OSObject* object, bool fallback) {
    auto* booleanObject = OSDynamicCast(OSBoolean, object);
    if (booleanObject == nullptr) {
        return fallback;
    }
    return booleanObject == kOSBooleanTrue;
}

void AppendBoolControl(ParsedAudioDriverConfig& inOutConfig,
                       const BoolControlDescriptor& descriptor) {
    if (inOutConfig.boolControlCount >= kMaxBoolControls) {
        return;
    }
    inOutConfig.boolControls[inOutConfig.boolControlCount++] = descriptor;
}

void BuildChannelNamesFromPlugs(ParsedAudioDriverConfig& inOutConfig) {
    const uint32_t maxInputChannels = std::min(inOutConfig.inputChannelCount, kMaxNamedChannels);
    const uint32_t maxOutputChannels = std::min(inOutConfig.outputChannelCount, kMaxNamedChannels);
    for (uint32_t index = 0; index < maxInputChannels; ++index) {
        snprintf(inOutConfig.inputChannelNames[index],
                 sizeof(inOutConfig.inputChannelNames[index]),
                 "%s %u",
                 inOutConfig.inputPlugName,
                 index + 1);
    }
    for (uint32_t index = 0; index < maxOutputChannels; ++index) {
        snprintf(inOutConfig.outputChannelNames[index],
                 sizeof(inOutConfig.outputChannelNames[index]),
                 "%s %u",
                 inOutConfig.outputPlugName,
                 index + 1);
    }
}

// MOTU 828 MK3 FW channel layout at 1× rate (44.1/48 kHz):
//
//   TX (device → host, 18ch = CoreAudio "Input"):
//     1–2   Analog 1-2  (front combo XLR/TRS, mic preamps)
//     3–8   Analog 3-8  (rear balanced TRS)
//     9–10  S/PDIF 1-2  (coaxial)
//    11–18  Optical 1-8 (ADAT A at 1×; TOSLINK S/PDIF if no ADAT device connected)
//
//   RX (host → device, 14ch = CoreAudio "Output"):
//     1–8   Analog 1-8  (rear balanced TRS)
//     9–10  Main L/R    (front XLR)
//    11–12  Phones L/R  (front 6.35 mm)
//    13–14  S/PDIF 1-2  (coaxial)
//
// Source: Linux kernel snd_motu_spec_828mk3_fw + motu-protocol-v3.c
// (tx_fixed_pcm_chunks={18,18,14}, rx_fixed_pcm_chunks={14,14,10} at 1× rate).
// Applies to: Vendor 0x0001F2, Model 0x000015 (828mk3 FW) and 0x000035 (828mk3 Hybrid).
static void ApplyMOTUV3ChannelNames(ParsedAudioDriverConfig& inOutConfig) {
    constexpr uint32_t kMOTUVendor     = 0x0001F2;
    constexpr uint32_t kModel828mk3FW  = 0x000015;
    constexpr uint32_t kModel828mk3Hyb = 0x000035;

    if (inOutConfig.vendorId != kMOTUVendor) {
        return;
    }
    if (inOutConfig.modelId != kModel828mk3FW && inOutConfig.modelId != kModel828mk3Hyb) {
        return;
    }

    // Input channel names (device→host, 18ch)
    static const char* kInputNames[18] = {
        "Analog 1", "Analog 2",
        "Analog 3", "Analog 4", "Analog 5", "Analog 6", "Analog 7", "Analog 8",
        "S/PDIF 1", "S/PDIF 2",
        "Optical 1", "Optical 2", "Optical 3", "Optical 4",
        "Optical 5", "Optical 6", "Optical 7", "Optical 8",
    };

    // Output channel names (host→device, 14ch)
    static const char* kOutputNames[14] = {
        "Analog 1", "Analog 2", "Analog 3", "Analog 4",
        "Analog 5", "Analog 6", "Analog 7", "Analog 8",
        "Main L",   "Main R",
        "Phones L", "Phones R",
        "S/PDIF 1", "S/PDIF 2",
    };

    const uint32_t inCount  = std::min(inOutConfig.inputChannelCount,  kMaxNamedChannels);
    const uint32_t outCount = std::min(inOutConfig.outputChannelCount, kMaxNamedChannels);

    for (uint32_t i = 0; i < inCount && i < 18; ++i) {
        strlcpy(inOutConfig.inputChannelNames[i], kInputNames[i],
                sizeof(inOutConfig.inputChannelNames[i]));
    }
    for (uint32_t i = 0; i < outCount && i < 14; ++i) {
        strlcpy(inOutConfig.outputChannelNames[i], kOutputNames[i],
                sizeof(inOutConfig.outputChannelNames[i]));
    }
}

} // namespace

void ParseAudioDriverConfigFromProperties(OSDictionary* properties,
                                          ParsedAudioDriverConfig& inOutConfig) {
    if (!properties) {
        return;
    }

    if (auto* guid = OSDynamicCast(OSNumber, properties->getObject("ASFWGUID"))) {
        inOutConfig.guid = guid->unsigned64BitValue();
    }
    if (auto* vendor = OSDynamicCast(OSNumber, properties->getObject("ASFWVendorID"))) {
        inOutConfig.vendorId = vendor->unsigned32BitValue();
    }
    if (auto* model = OSDynamicCast(OSNumber, properties->getObject("ASFWModelID"))) {
        inOutConfig.modelId = model->unsigned32BitValue();
    }
    if (auto* inputChannels = OSDynamicCast(OSNumber, properties->getObject("ASFWInputChannelCount"))) {
        inOutConfig.inputChannelCount = inputChannels->unsigned32BitValue();
    }
    if (auto* outputChannels = OSDynamicCast(OSNumber, properties->getObject("ASFWOutputChannelCount"))) {
        inOutConfig.outputChannelCount = outputChannels->unsigned32BitValue();
    }

    inOutConfig.hasPhantomOverride = ReadOSBoolValue(properties->getObject("ASFWHasPhantomOverride"), false);
    if (auto* supportedMask = OSDynamicCast(OSNumber, properties->getObject("ASFWPhantomSupportedMask"))) {
        inOutConfig.phantomSupportedMask = supportedMask->unsigned32BitValue();
    }
    if (auto* initialMask = OSDynamicCast(OSNumber, properties->getObject("ASFWPhantomInitialMask"))) {
        inOutConfig.phantomInitialMask = initialMask->unsigned32BitValue();
    }

    if (auto* name = OSDynamicCast(OSString, properties->getObject("ASFWDeviceName"))) {
        strlcpy(inOutConfig.deviceName, name->getCStringNoCopy(), sizeof(inOutConfig.deviceName));
    }

    if (auto* count = OSDynamicCast(OSNumber, properties->getObject("ASFWChannelCount"))) {
        inOutConfig.channelCount = count->unsigned32BitValue();
    }

    if (auto* rates = OSDynamicCast(OSArray, properties->getObject("ASFWSampleRates"))) {
        inOutConfig.sampleRateCount = 0;
        const uint32_t cappedCount = std::min(rates->getCount(), kMaxSampleRates);
        for (uint32_t i = 0; i < cappedCount; ++i) {
            auto* rate = OSDynamicCast(OSNumber, rates->getObject(i));
            if (rate == nullptr) {
                continue;
            }
            inOutConfig.sampleRates[inOutConfig.sampleRateCount++] =
                static_cast<double>(rate->unsigned32BitValue());
        }
    }

    if (auto* inputName = OSDynamicCast(OSString, properties->getObject("ASFWInputPlugName"))) {
        strlcpy(inOutConfig.inputPlugName, inputName->getCStringNoCopy(), sizeof(inOutConfig.inputPlugName));
    }
    if (auto* outputName = OSDynamicCast(OSString, properties->getObject("ASFWOutputPlugName"))) {
        strlcpy(inOutConfig.outputPlugName, outputName->getCStringNoCopy(), sizeof(inOutConfig.outputPlugName));
    }

    if (auto* rate = OSDynamicCast(OSNumber, properties->getObject("ASFWCurrentSampleRate"))) {
        inOutConfig.currentSampleRate = static_cast<double>(rate->unsigned32BitValue());
    }

    if (auto* mode = OSDynamicCast(OSNumber, properties->getObject("ASFWStreamMode"))) {
        inOutConfig.streamMode = (mode->unsigned32BitValue() == static_cast<uint32_t>(StreamMode::kBlocking))
            ? StreamMode::kBlocking
            : StreamMode::kNonBlocking;
    }

    if (auto* overrideArray = OSDynamicCast(OSArray, properties->getObject("ASFWBoolControlOverrides"))) {
        for (uint32_t index = 0; index < overrideArray->getCount(); ++index) {
            auto* entry = OSDynamicCast(OSDictionary, overrideArray->getObject(index));
            if (!entry) {
                continue;
            }

            auto* classNumber = OSDynamicCast(OSNumber, entry->getObject("ClassID"));
            auto* scopeNumber = OSDynamicCast(OSNumber, entry->getObject("Scope"));
            auto* elementNumber = OSDynamicCast(OSNumber, entry->getObject("Element"));
            if (classNumber == nullptr || scopeNumber == nullptr || elementNumber == nullptr) {
                continue;
            }

            const BoolControlDescriptor descriptor{
                .classIdFourCC = classNumber->unsigned32BitValue(),
                .scopeFourCC = scopeNumber->unsigned32BitValue(),
                .element = elementNumber->unsigned32BitValue(),
                .isSettable = ReadOSBoolValue(entry->getObject("Settable"), false),
                .initialValue = ReadOSBoolValue(entry->getObject("Initial"), false),
            };
            AppendBoolControl(inOutConfig, descriptor);
        }
    }

    BuildChannelNamesFromPlugs(inOutConfig);

    // Override with device-specific names when vendor/model are known.
    // Must run AFTER BuildChannelNamesFromPlugs so the generic "Input N" fallback
    // is already in place for any channels beyond the device-specific table.
    ApplyMOTUV3ChannelNames(inOutConfig);
}

} // namespace ASFW::Isoch::Audio
