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
    // Confirmed from Sequoia diagnostic: fNumFWOutputChannels=14 fNumFWInputChannels=18.
    const auto layout = DeviceProtocolFactory::GetMOTUV3ChannelLayout(
        DeviceProtocolFactory::kMOTU828MK3FWModel);
    EXPECT_EQ(layout.inputChannels,  14u);  // device → host (IR)
    EXPECT_EQ(layout.outputChannels, 18u);  // host → device (IT)

    // Hybrid variant has the same layout.
    const auto layoutHyb = DeviceProtocolFactory::GetMOTUV3ChannelLayout(
        DeviceProtocolFactory::kMOTU828MK3HybModel);
    EXPECT_EQ(layoutHyb.inputChannels,  14u);
    EXPECT_EQ(layoutHyb.outputChannels, 18u);
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
