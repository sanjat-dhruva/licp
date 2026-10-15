/**
 * @file test_proto.cpp
 * @brief Orbit record, acknowledgement bitmap, beacon and bitmap broadcast formats.
 */
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

extern "C" {
#include "licp/proto/licp_beacon.h"
#include "licp/proto/licp_bitmap.h"
#include "licp/proto/licp_orbit.h"
}

namespace {
const uint8_t KEY[16] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
                         0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF};

licp_orbit_t sample_orbit()
{
    licp_orbit_t o;
    std::memset(&o, 0, sizeof(o));
    o.sat_id = 7;
    o.epoch_s = 86400u * 400u + 1234u;
    o.mean_motion_e8 = 1510000000u; /* 15.1 revolutions per day */
    o.ecc_e7 = 12345u;              /* 0.0012345 */
    o.incl_e2 = 9750u;              /* 97.50 degrees */
    o.raan_e4 = 1234567u;           /* 123.4567 degrees */
    o.argp_e4 = 3599999u;
    o.mean_anom_e4 = 0u;
    o.bstar_mant = -346;
    o.bstar_exp = -4;
    o.validity_days = 7;
    return o;
}
} // namespace

/* ----------------------------- Orbit record ----------------------------- */

TEST(Orbit, PacksToTwentySixBytesAndBack)
{
    licp_orbit_t o = sample_orbit(), back;
    std::memset(&back, 0, sizeof(back)); /* struct padding must match for memcmp */
    uint8_t rec[LICP_ORBIT_RECORD_LEN];
    ASSERT_EQ(licp_orbit_pack(&o, rec), LICP_OK);
    EXPECT_EQ(rec[0], 7u);
    EXPECT_EQ(rec[25], 7u);
    ASSERT_EQ(licp_orbit_unpack(rec, &back), LICP_OK);
    EXPECT_EQ(0, std::memcmp(&o, &back, sizeof(o)));
}

TEST(Orbit, RejectsOutOfRangeFields)
{
    licp_orbit_t o = sample_orbit();
    uint8_t rec[LICP_ORBIT_RECORD_LEN];
    o.incl_e2 = 18001;
    EXPECT_EQ(licp_orbit_pack(&o, rec), LICP_ERR_PARAM);
    o = sample_orbit();
    o.ecc_e7 = 10000000u;
    EXPECT_EQ(licp_orbit_pack(&o, rec), LICP_ERR_PARAM);
    o = sample_orbit();
    o.raan_e4 = 3600000u;
    EXPECT_EQ(licp_orbit_pack(&o, rec), LICP_ERR_PARAM);
}

TEST(Orbit, DragTermKeepsThreeSignificantDigits)
{
    int16_t m;
    int8_t e;
    ASSERT_EQ(licp_orbit_bstar_encode(3.4567e-5, &m, &e), LICP_OK);
    EXPECT_EQ(m, 346);
    EXPECT_EQ(e, -4);
    EXPECT_NEAR(licp_orbit_bstar_decode(m, e), 3.46e-5, 1e-12);
    ASSERT_EQ(licp_orbit_bstar_encode(-1.2e-3, &m, &e), LICP_OK);
    EXPECT_NEAR(licp_orbit_bstar_decode(m, e), -1.2e-3, 1e-12);
    ASSERT_EQ(licp_orbit_bstar_encode(0.0, &m, &e), LICP_OK);
    EXPECT_EQ(m, 0);
    EXPECT_EQ(licp_orbit_bstar_encode(1e-12, &m, &e), LICP_ERR_PARAM);
}

/* -------------------------------- Bitmap -------------------------------- */

TEST(Bitmap, HundredDevicesFitInThirteenBytes)
{
    licp_bitmap_t bm;
    ASSERT_EQ(licp_bitmap_reset(&bm, 100, 3), LICP_OK);
    for (uint16_t i = 0; i < 100; i += 3) {
        ASSERT_EQ(licp_bitmap_set(&bm, i), LICP_OK);
    }
    EXPECT_EQ(licp_bitmap_count(&bm), 34u);
    EXPECT_TRUE(licp_bitmap_test(&bm, 99));
    EXPECT_FALSE(licp_bitmap_test(&bm, 98));
    EXPECT_EQ(licp_bitmap_set(&bm, 100), LICP_ERR_PARAM);
    uint8_t out[16];
    EXPECT_EQ(licp_bitmap_extract(&bm, 0, 100, out, sizeof(out)), 13u);
    EXPECT_EQ(out[0], 0x49u); /* bits 0, 3, 6 */
}

/* -------------------------------- Beacon -------------------------------- */

TEST(Beacon, RoundTripWithAllSections)
{
    licp_beacon_t b;
    std::memset(&b, 0, sizeof(b));
    b.sat_id = 1;
    b.superframe = 123456;
    b.gnss_s = 1400000000u;
    b.gnss_ms = 50;
    b.access_p[0] = 255;
    b.access_p[1] = 204;
    b.access_p[2] = 128;
    b.access_p[3] = 77;
    b.sos_ack_count = 2;
    b.sos_acks[0] = 17;
    b.sos_acks[1] = 42;
    b.call_in_count = 1;
    b.call_ins[0] = 99;
    b.broadcast_count = 1;
    b.broadcast_groups[0] = 5;
    b.has_orbit = true;
    b.orbit = sample_orbit();

    uint8_t frame[255];
    size_t len = 0;
    licp_beacon_fit_t fit;
    ASSERT_EQ(licp_beacon_encode(&b, KEY, 115, frame, sizeof(frame), &len, &fit), LICP_OK);
    EXPECT_EQ(fit.sos_acks_sent, 2u);
    EXPECT_EQ(fit.call_ins_sent, 1u);
    EXPECT_TRUE(fit.broadcast_sent);
    EXPECT_TRUE(fit.orbit_sent);
    /* 18 fixed + (2+4) + (2+2) + (2+1) + (2+26) + 4 tag = 63 bytes */
    EXPECT_EQ(len, 63u);
    EXPECT_EQ(frame[0], LICP_PFRAME_MHDR);

    licp_beacon_t d;
    ASSERT_EQ(licp_beacon_decode(frame, len, KEY, &d), LICP_OK);
    EXPECT_EQ(d.superframe, 123456u);
    EXPECT_EQ(d.gnss_s, 1400000000u);
    EXPECT_EQ(d.access_p[3], 77u);
    EXPECT_EQ(d.sos_ack_count, 2u);
    EXPECT_EQ(d.sos_acks[1], 42u);
    EXPECT_EQ(d.call_ins[0], 99u);
    EXPECT_EQ(d.broadcast_groups[0], 5u);
    ASSERT_TRUE(d.has_orbit);
    EXPECT_EQ(d.orbit.raan_e4, 1234567u);
}

TEST(Beacon, MinimalBeaconIsTwentyTwoBytes)
{
    licp_beacon_t b;
    std::memset(&b, 0, sizeof(b));
    uint8_t frame[64];
    size_t len = 0;
    ASSERT_EQ(licp_beacon_encode(&b, KEY, 64, frame, sizeof(frame), &len, nullptr), LICP_OK);
    EXPECT_EQ(len, 22u);
}

TEST(Beacon, WrongKeyOrTamperingIsRejected)
{
    licp_beacon_t b;
    std::memset(&b, 0, sizeof(b));
    b.superframe = 9;
    uint8_t frame[64];
    size_t len = 0;
    ASSERT_EQ(licp_beacon_encode(&b, KEY, 64, frame, sizeof(frame), &len, nullptr), LICP_OK);
    uint8_t other[16] = {0};
    licp_beacon_t d;
    EXPECT_EQ(licp_beacon_decode(frame, len, other, &d), LICP_ERR_MIC);
    frame[5] ^= 1;
    EXPECT_EQ(licp_beacon_decode(frame, len, KEY, &d), LICP_ERR_MIC);
}

TEST(Beacon, TruncatesAcknowledgementsToFitAndReportsIt)
{
    licp_beacon_t b;
    std::memset(&b, 0, sizeof(b));
    b.sos_ack_count = 16;
    for (uint16_t i = 0; i < 16; ++i) {
        b.sos_acks[i] = i;
    }
    b.has_orbit = true;
    b.orbit = sample_orbit();
    uint8_t frame[255];
    size_t len = 0;
    licp_beacon_fit_t fit;
    /* 40-byte limit: 18 fixed + 4 tag leaves 18 bytes: section header 2 + 8 acks. Orbit cannot fit. */
    ASSERT_EQ(licp_beacon_encode(&b, KEY, 40, frame, sizeof(frame), &len, &fit), LICP_OK);
    EXPECT_EQ(fit.sos_acks_sent, 8u);
    EXPECT_FALSE(fit.orbit_sent);
    EXPECT_LE(len, 40u);
    licp_beacon_t d;
    ASSERT_EQ(licp_beacon_decode(frame, len, KEY, &d), LICP_OK);
    EXPECT_EQ(d.sos_ack_count, 8u);
    EXPECT_FALSE(d.has_orbit);
}

TEST(Beacon, UnknownExtensionSectionsAreSkippedByDecoder)
{
    licp_beacon_t b;
    std::memset(&b, 0, sizeof(b));
    const uint8_t ext[3] = {1, 2, 3};
    b.extra_count = 1;
    b.extra[0].type = 0x90;
    b.extra[0].len = 3;
    b.extra[0].data = ext;
    b.broadcast_count = 1;
    b.broadcast_groups[0] = 2;
    uint8_t frame[64];
    size_t len = 0;
    licp_beacon_fit_t fit;
    ASSERT_EQ(licp_beacon_encode(&b, KEY, 64, frame, sizeof(frame), &len, &fit), LICP_OK);
    EXPECT_EQ(fit.extra_sent, 1u);
    licp_beacon_t d;
    ASSERT_EQ(licp_beacon_decode(frame, len, KEY, &d), LICP_OK);
    EXPECT_EQ(d.broadcast_count, 1u);
}

TEST(Beacon, ProbabilityConversion)
{
    EXPECT_EQ(licp_prob_to_u8(1.0), 255u);
    EXPECT_EQ(licp_prob_to_u8(0.0), 0u);
    EXPECT_EQ(licp_prob_to_u8(0.5), 128u);
    EXPECT_NEAR(licp_prob_from_u8(255), 1.0, 1e-9);
}

/* ---------------------- Acknowledgement-bitmap frame --------------------- */

TEST(AckMap, RoundTrip)
{
    licp_bitmap_t bm;
    ASSERT_EQ(licp_bitmap_reset(&bm, 100, 12), LICP_OK);
    (void)licp_bitmap_set(&bm, 0);
    (void)licp_bitmap_set(&bm, 57);
    (void)licp_bitmap_set(&bm, 99);
    uint8_t frame[64];
    size_t len = 0;
    ASSERT_EQ(licp_ackmap_encode(3, &bm, 0, 100, KEY, frame, sizeof(frame), &len), LICP_OK);
    EXPECT_EQ(len, 3u + 7u + 13u + 4u);

    licp_bitmap_t out;
    uint8_t sat = 0;
    uint16_t first = 0, count = 0;
    ASSERT_EQ(licp_ackmap_decode(frame, len, KEY, &sat, &out, &first, &count), LICP_OK);
    EXPECT_EQ(sat, 3u);
    EXPECT_EQ(out.pass_id, 12u);
    EXPECT_EQ(count, 100u);
    EXPECT_TRUE(licp_bitmap_test(&out, 57));
    EXPECT_TRUE(licp_bitmap_test(&out, 99));
    EXPECT_FALSE(licp_bitmap_test(&out, 58));
    frame[12] ^= 0x10;
    EXPECT_EQ(licp_ackmap_decode(frame, len, KEY, &sat, &out, &first, &count), LICP_ERR_MIC);
}
