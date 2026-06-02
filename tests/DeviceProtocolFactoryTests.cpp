// SPDX-License-Identifier: LGPL-3.0-or-later

#include <gtest/gtest.h>

#include "Protocols/Audio/DeviceProtocolFactory.hpp"

namespace {

using ASFW::Audio::DeviceIntegrationMode;
using ASFW::Audio::DeviceProtocolFactory;

TEST(DeviceProtocolFactoryTests, SelectsIntegrationModeForKnownDevices) {
    EXPECT_EQ(DeviceProtocolFactory::LookupIntegrationMode(
                  DeviceProtocolFactory::kFocusriteVendorId,
                  DeviceProtocolFactory::kSPro24DspModelId),
              DeviceIntegrationMode::kHardcodedNub);

    EXPECT_EQ(DeviceProtocolFactory::LookupIntegrationMode(
                  DeviceProtocolFactory::kApogeeVendorId,
                  DeviceProtocolFactory::kApogeeDuetModelId),
              DeviceIntegrationMode::kAVCDriven);
}

TEST(DeviceProtocolFactoryTests, RejectsUnknownDevices) {
    EXPECT_EQ(DeviceProtocolFactory::LookupIntegrationMode(0x00ABCDEF, 0x00001234),
              DeviceIntegrationMode::kNone);
    EXPECT_FALSE(DeviceProtocolFactory::IsKnownDevice(0x00ABCDEF, 0x00001234));
}

TEST(DeviceProtocolFactoryTests, RecognizesKnownVendorModelPairs) {
    EXPECT_TRUE(DeviceProtocolFactory::IsKnownDevice(
        DeviceProtocolFactory::kFocusriteVendorId,
        DeviceProtocolFactory::kSPro24DspModelId));

    EXPECT_TRUE(DeviceProtocolFactory::IsKnownDevice(
        DeviceProtocolFactory::kApogeeVendorId,
        DeviceProtocolFactory::kApogeeDuetModelId));
}

TEST(DeviceProtocolFactoryTests, SelectsMOTUV3ForAllV3Models) {
    EXPECT_EQ(DeviceProtocolFactory::LookupIntegrationMode(
                  DeviceProtocolFactory::kMOTUVendorId,
                  DeviceProtocolFactory::kMOTU828MK3FWModel),
              DeviceIntegrationMode::kMOTUV3);

    EXPECT_EQ(DeviceProtocolFactory::LookupIntegrationMode(
                  DeviceProtocolFactory::kMOTUVendorId,
                  DeviceProtocolFactory::kMOTU828MK3HybModel),
              DeviceIntegrationMode::kMOTUV3);

    EXPECT_EQ(DeviceProtocolFactory::LookupIntegrationMode(
                  DeviceProtocolFactory::kMOTUVendorId,
                  DeviceProtocolFactory::kMOTU896MK3Model),
              DeviceIntegrationMode::kMOTUV3);

    // Unknown MOTU model → kNone (not kMOTUV3).
    EXPECT_EQ(DeviceProtocolFactory::LookupIntegrationMode(
                  DeviceProtocolFactory::kMOTUVendorId,
                  0x0000FFu),
              DeviceIntegrationMode::kNone);
}

TEST(DeviceProtocolFactoryTests, EffectiveModelIdUsesSWVersionForMOTU) {
    // MOTU 828 MK3 reports rootModelId=0x000000 and unitSwVersion=0x000015.
    // EffectiveModelId must return unitSwVersion so LookupIntegrationMode works.
    const uint32_t effective = DeviceProtocolFactory::EffectiveModelId(
        DeviceProtocolFactory::kMOTUVendorId,
        /*rootModelId=*/0x000000u,
        /*unitSwVersion=*/DeviceProtocolFactory::kMOTU828MK3FWModel);
    EXPECT_EQ(effective, DeviceProtocolFactory::kMOTU828MK3FWModel);

    EXPECT_EQ(DeviceProtocolFactory::LookupIntegrationMode(
                  DeviceProtocolFactory::kMOTUVendorId, effective),
              DeviceIntegrationMode::kMOTUV3);
}

TEST(DeviceProtocolFactoryTests, MOTU828MK3ChannelLayout) {
    // Fix 34: corrected from {21, 21} to {18, 14} — wire DBS=21 is NOT the PCM
    // channel count.  Authoritative source: Linux motu-protocol-v3.c
    //   snd_motu_spec_828mk3_fw.tx_fixed_pcm_chunks[0] = 18  (device→host, IR)
    //   snd_motu_spec_828mk3_fw.rx_fixed_pcm_chunks[0] = 14  (host→device, IT)
    // Confirmed by MOTU kext Sequoia diagnostic:
    //   fNumFWInputChannels=18 (IR), fNumFWOutputChannels=14 (IT).
    // Wire DBS=21 stays in MOTUAudioBackend (kMOTUV3WireDbs48k) — it reflects
    // 2 msg slots + 24 rounded PCM slots on the wire, not usable PCM count.
    const auto layout = DeviceProtocolFactory::GetMOTUV3ChannelLayout(
        DeviceProtocolFactory::kMOTU828MK3FWModel);
    EXPECT_EQ(layout.inputChannels,  18u);  // device → host (IR): 18 PCM channels at 48 kHz
    EXPECT_EQ(layout.outputChannels, 14u);  // host → device (IT): 14 PCM channels at 48 kHz

    // Hybrid variant has the same FireWire channel layout.
    const auto layoutHyb = DeviceProtocolFactory::GetMOTUV3ChannelLayout(
        DeviceProtocolFactory::kMOTU828MK3HybModel);
    EXPECT_EQ(layoutHyb.inputChannels,  18u);
    EXPECT_EQ(layoutHyb.outputChannels, 14u);
}

TEST(DeviceProtocolFactoryTests, UnknownMOTUModelFallsBackToSafeDefaults) {
    const auto layout = DeviceProtocolFactory::GetMOTUV3ChannelLayout(0xDEADBEu);
    EXPECT_EQ(layout.inputChannels,  2u);
    EXPECT_EQ(layout.outputChannels, 2u);
}

TEST(DeviceProtocolFactoryTests, MOTU828MK3DeviceName) {
    EXPECT_STREQ(DeviceProtocolFactory::GetMOTUV3DeviceName(
                     DeviceProtocolFactory::kMOTU828MK3FWModel),
                 "MOTU 828mk3");
    EXPECT_STREQ(DeviceProtocolFactory::GetMOTUV3DeviceName(0xDEADBEu),
                 "MOTU FireWire");
}

} // namespace
