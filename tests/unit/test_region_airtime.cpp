/**
 * @file test_region_airtime.cpp
 * @brief Radio plan defaults and chirp time on air (vectors from tools/gen_vectors.py).
 */
#include <gtest/gtest.h>

extern "C" {
#include "licp/lorawan/licp_airtime.h"
#include "licp/region/licp_region.h"
}

TEST(Region, DefaultsMatchNetworkNote)
{
    licp_region_t r;
    licp_region_default(&r);
    ASSERT_EQ(licp_region_validate(&r), LICP_OK);
    EXPECT_EQ(r.channel_count, 15u);
    EXPECT_EQ(licp_region_channel_hz(&r, 0), 855100000u);
    EXPECT_EQ(licp_region_channel_hz(&r, 14), 857900000u);
    EXPECT_EQ(licp_region_channel_hz(&r, 15), 0u);
    EXPECT_TRUE(licp_region_is_sos_channel(&r, 0));
    EXPECT_EQ(licp_region_channel_hz(&r, 8), 856700000u);
    EXPECT_TRUE(licp_region_is_sos_channel(&r, 8));
    EXPECT_FALSE(licp_region_is_sos_channel(&r, 7));
    EXPECT_EQ(licp_region_general_count(&r), 13u);
    EXPECT_EQ(r.downlink_hz, 856500000u);
}

TEST(Region, MapsShiftedFrequencyToChannel)
{
    licp_region_t r;
    licp_region_default(&r);
    uint8_t ch = 0xFF;
    /* 12.4 kHz frequency shift at the cone edge stays on channel 3. */
    EXPECT_EQ(licp_region_channel_of(&r, 855700000u + 12400u, 30000u, &ch), LICP_OK);
    EXPECT_EQ(ch, 3u);
    EXPECT_EQ(licp_region_channel_of(&r, 855700000u - 12400u, 30000u, &ch), LICP_OK);
    EXPECT_EQ(ch, 3u);
    EXPECT_EQ(licp_region_channel_of(&r, 855800000u, 30000u, &ch), LICP_ERR_NOT_FOUND);
}

TEST(Region, ValidationCatchesBadPlans)
{
    licp_region_t r;
    licp_region_default(&r);
    r.sos_channels[1] = 15;
    EXPECT_EQ(licp_region_validate(&r), LICP_ERR_PARAM);
    licp_region_default(&r);
    r.channel_count = 0;
    EXPECT_EQ(licp_region_validate(&r), LICP_ERR_PARAM);
    licp_region_default(&r);
    r.downlink.sf = 6;
    EXPECT_EQ(licp_region_validate(&r), LICP_ERR_PARAM);
}

struct AirCase {
    uint8_t sf;
    uint32_t len;
    bool crc;
    uint32_t expect_us;
};

class Airtime : public ::testing::TestWithParam<AirCase> {};

TEST_P(Airtime, MatchesIndependentFormula)
{
    const AirCase &c = GetParam();
    licp_lora_params_t p = {c.sf, 125000u, 1u, 8u, true, c.crc};
    uint32_t got = licp_lora_airtime_us(&p, c.len);
    EXPECT_NEAR(static_cast<double>(got), static_cast<double>(c.expect_us), 1.0);
}

INSTANTIATE_TEST_SUITE_P(Vectors, Airtime,
                         ::testing::Values(AirCase{10, 13, false, 288768}, AirCase{10, 50, false, 575488},
                                           AirCase{7, 20, true, 56576}, AirCase{12, 51, true, 2465792},
                                           AirCase{11, 30, false, 905216}));

TEST(AirtimeLimits, InvalidParametersGiveZero)
{
    licp_lora_params_t p = {6, 125000u, 1u, 8u, true, false};
    EXPECT_EQ(licp_lora_airtime_us(&p, 10), 0u);
    p.sf = 10;
    p.bw_hz = 100000u;
    EXPECT_EQ(licp_lora_airtime_us(&p, 10), 0u);
    EXPECT_EQ(licp_lora_airtime_us(nullptr, 10), 0u);
}
