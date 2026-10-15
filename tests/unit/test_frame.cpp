/**
 * @file test_frame.cpp
 * @brief Frame parsing, integrity code, counter rebuild and payload encryption.
 *
 * Vectors come from tools/gen_vectors.py (independent Python implementation).
 */
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "test_util.hpp"

extern "C" {
#include "licp/lorawan/licp_frame.h"
}

namespace {
/* Public vector: payload "test", DevAddr 0x49BE7DF1, FCnt 2. */
const uint8_t PUB_FRAME[] = {0x40, 0xF1, 0x7D, 0xBE, 0x49, 0x00, 0x02, 0x00, 0x01,
                             0x95, 0x43, 0x78, 0x76, 0x2B, 0x11, 0xFF, 0x0D};
const uint8_t PUB_NWK[16] = {0x44, 0x02, 0x42, 0x41, 0xED, 0x4C, 0xE9, 0xA6,
                             0x8C, 0x6A, 0x8B, 0xC0, 0x55, 0x23, 0x3F, 0xD3};
const uint8_t PUB_APP[16] = {0xEC, 0x92, 0x58, 0x02, 0xAE, 0x43, 0x0C, 0xA7,
                             0x7F, 0xD3, 0xDD, 0x73, 0xCB, 0x2C, 0xC5, 0x88};
/* Project vector: DevAddr 0x26011234, FCnt32 0x00010005, port 1, "hello S-MIoT", keys 00..0F / 10..1F. */
const uint8_t PRJ_FRAME[] = {0x40, 0x34, 0x12, 0x01, 0x26, 0x00, 0x05, 0x00, 0x01, 0x44, 0x3E, 0x3F, 0x73,
                             0xF8, 0x59, 0xD9, 0x20, 0x85, 0x00, 0xD8, 0x23, 0x3F, 0x90, 0xAF, 0xB6};
} // namespace

TEST(Frame, ParsesPublicVector)
{
    licp_frame_t f;
    ASSERT_EQ(licp_frame_parse(PUB_FRAME, sizeof(PUB_FRAME), &f), LICP_OK);
    EXPECT_EQ(f.mtype, LICP_MTYPE_UNCONF_DATA_UP);
    EXPECT_EQ(f.dev_addr, 0x49BE7DF1u);
    EXPECT_EQ(f.fcnt16, 2u);
    EXPECT_TRUE(f.has_port);
    EXPECT_EQ(f.fport, 1u);
    EXPECT_EQ(f.payload_len, 4u);
    EXPECT_EQ(f.fopts_len, 0u);
}

TEST(Frame, VerifiesAndDecryptsPublicVector)
{
    licp_frame_t f;
    ASSERT_EQ(licp_frame_parse(PUB_FRAME, sizeof(PUB_FRAME), &f), LICP_OK);
    EXPECT_EQ(licp_frame_verify_mic(&f, PUB_NWK, 2), LICP_OK);
    uint8_t p[4];
    std::memcpy(p, f.payload, 4);
    licp_frame_crypt_payload(PUB_APP, LICP_DIR_UP, f.dev_addr, 2, p, 4);
    EXPECT_EQ(0, std::memcmp(p, "test", 4));
}

TEST(Frame, ProjectVectorNeedsFull32BitCounter)
{
    licp_frame_t f;
    ASSERT_EQ(licp_frame_parse(PRJ_FRAME, sizeof(PRJ_FRAME), &f), LICP_OK);
    EXPECT_EQ(f.fcnt16, 0x0005u);
    /* With only the 16 bits on air the check fails; with the rebuilt counter it passes. */
    EXPECT_EQ(licp_frame_verify_mic(&f, licp_test::nwk_key(), 0x00000005u), LICP_ERR_MIC);
    uint32_t fcnt32 = licp_frame_rebuild_fcnt(0x0000FFF0u, f.fcnt16);
    EXPECT_EQ(fcnt32, 0x00010005u);
    EXPECT_EQ(licp_frame_verify_mic(&f, licp_test::nwk_key(), fcnt32), LICP_OK);
}

TEST(Frame, BuilderMatchesIndependentVector)
{
    const char *msg = "hello S-MIoT";
    uint8_t buf[64];
    size_t len = 0;
    ASSERT_EQ(licp_frame_build_data(LICP_MTYPE_UNCONF_DATA_UP, 0x26011234u, 0x00, 0x00010005u, nullptr, 0, 1, true,
                                    reinterpret_cast<const uint8_t *>(msg), std::strlen(msg),
                                    licp_test::nwk_key(), licp_test::app_key(), buf, sizeof(buf), &len),
              LICP_OK);
    ASSERT_EQ(len, sizeof(PRJ_FRAME));
    EXPECT_EQ(0, std::memcmp(buf, PRJ_FRAME, len));
}

TEST(Frame, TamperedFrameFailsIntegrityCheck)
{
    std::vector<uint8_t> b(PUB_FRAME, PUB_FRAME + sizeof(PUB_FRAME));
    b[10] ^= 0x01;
    licp_frame_t f;
    ASSERT_EQ(licp_frame_parse(b.data(), b.size(), &f), LICP_OK);
    EXPECT_EQ(licp_frame_verify_mic(&f, PUB_NWK, 2), LICP_ERR_MIC);
}

TEST(Frame, RejectsShortAndNonDataFrames)
{
    licp_frame_t f;
    EXPECT_EQ(licp_frame_parse(PUB_FRAME, 11, &f), LICP_ERR_LENGTH);
    uint8_t join[23] = {0x00};
    EXPECT_EQ(licp_frame_parse(join, sizeof(join), &f), LICP_ERR_FORMAT);
    uint8_t major1[17];
    std::memcpy(major1, PUB_FRAME, sizeof(major1));
    major1[0] = 0x41;
    EXPECT_EQ(licp_frame_parse(major1, sizeof(major1), &f), LICP_ERR_FORMAT);
}

TEST(Frame, RejectsFOptsLongerThanFrame)
{
    uint8_t b[12];
    std::memcpy(b, PUB_FRAME, 8);
    b[5] = 0x0F; /* claims 15 bytes of MAC commands */
    std::memset(b + 8, 0, 4);
    licp_frame_t f;
    EXPECT_EQ(licp_frame_parse(b, sizeof(b), &f), LICP_ERR_LENGTH);
}

TEST(Frame, RejectsPortZeroWithFOpts)
{
    const uint8_t fopts[2] = {0x02, 0x00};
    const uint8_t mac[1] = {0x02};
    uint8_t buf[64];
    size_t len = 0;
    ASSERT_EQ(licp_frame_build_data(LICP_MTYPE_UNCONF_DATA_UP, 1, 0x02, 1, fopts, 2, 0, true, mac, 1,
                                    licp_test::nwk_key(), licp_test::nwk_key(), buf, sizeof(buf), &len),
              LICP_OK);
    licp_frame_t f;
    EXPECT_EQ(licp_frame_parse(buf, len, &f), LICP_ERR_FORMAT);
}

TEST(Frame, ParsesFOptsAndEmptyPort)
{
    const uint8_t fopts[3] = {0x03, 0x07, 0x01};
    uint8_t buf[64];
    size_t len = 0;
    ASSERT_EQ(licp_frame_build_data(LICP_MTYPE_CONF_DATA_UP, 0xAABBCCDDu, 0x83, 7, fopts, 3, 0, false, nullptr, 0,
                                    licp_test::nwk_key(), nullptr, buf, sizeof(buf), &len),
              LICP_OK);
    licp_frame_t f;
    ASSERT_EQ(licp_frame_parse(buf, len, &f), LICP_OK);
    EXPECT_EQ(f.mtype, LICP_MTYPE_CONF_DATA_UP);
    EXPECT_TRUE(f.adr);
    EXPECT_EQ(f.fopts_len, 3u);
    EXPECT_FALSE(f.has_port);
    EXPECT_EQ(licp_frame_verify_mic(&f, licp_test::nwk_key(), 7), LICP_OK);
}

TEST(FrameCounter, RebuildCases)
{
    EXPECT_EQ(licp_frame_rebuild_fcnt(10, 11), 11u);              /* normal step */
    EXPECT_EQ(licp_frame_rebuild_fcnt(10, 10), 10u);              /* repeat -> duplicate */
    EXPECT_EQ(licp_frame_rebuild_fcnt(0x0000FFFFu, 0), 0x00010000u); /* roll-over of low half */
    EXPECT_EQ(licp_frame_rebuild_fcnt(0x00020010u, 0x0012), 0x00020012u);
    EXPECT_EQ(licp_frame_rebuild_fcnt(0x00020010u, 0x0001), 0x00030001u); /* older value looks like a big jump */
}

TEST(Frame, PayloadCryptIsItsOwnInverse)
{
    uint8_t data[40];
    for (int i = 0; i < 40; ++i) {
        data[i] = static_cast<uint8_t>(i * 7);
    }
    uint8_t copy[40];
    std::memcpy(copy, data, 40);
    licp_frame_crypt_payload(licp_test::app_key(), LICP_DIR_DOWN, 0x1234, 99, data, 40);
    EXPECT_NE(0, std::memcmp(data, copy, 40));
    licp_frame_crypt_payload(licp_test::app_key(), LICP_DIR_DOWN, 0x1234, 99, data, 40);
    EXPECT_EQ(0, std::memcmp(data, copy, 40));
}
