/**
 * @file test_io.cpp
 * @brief On-board computer framing, frequency-hopping receiver records, configuration.
 */
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "test_util.hpp"

extern "C" {
#include "licp/app/licp_config.h"
#include "licp/hal/licp_radio_fhss.h"
#include "licp/obc/licp_obc.h"
}

using licp_test::TempDir;

/* ---------------------------- OBC framing ------------------------------- */

TEST(Obc, EncodeDecodeRoundTrip)
{
    const uint8_t payload[5] = {1, 2, 3, 4, 5};
    uint8_t out[64];
    size_t n = licp_obc_encode(LICP_OBC_STATUS, 0x1234, payload, sizeof(payload), out, sizeof(out));
    ASSERT_EQ(n, 7u + 5u + 2u);
    licp_obc_decoder_t *d = new licp_obc_decoder_t;
    licp_obc_decoder_init(d);
    size_t used = 0;
    licp_obc_msg_t m;
    ASSERT_TRUE(licp_obc_decoder_feed(d, out, n, &used, &m));
    EXPECT_EQ(used, n);
    EXPECT_EQ(m.type, LICP_OBC_STATUS);
    EXPECT_EQ(m.seq, 0x1234u);
    ASSERT_EQ(m.len, 5u);
    EXPECT_EQ(0, std::memcmp(m.payload, payload, 5));
    delete d;
}

TEST(Obc, ByteByByteWithGarbageAndTwoMessages)
{
    uint8_t a[32], b[32];
    const uint8_t pa[2] = {9, 9};
    size_t na = licp_obc_encode(LICP_OBC_FRAMES_REQUEST, 1, pa, 2, a, sizeof(a));
    size_t nb = licp_obc_encode(LICP_OBC_STATUS_REQUEST, 2, nullptr, 0, b, sizeof(b));
    std::vector<uint8_t> stream = {0x00, 0xA5, 0x11, 0x5A};
    stream.insert(stream.end(), a, a + na);
    stream.push_back(0xA5);
    stream.insert(stream.end(), b, b + nb);

    licp_obc_decoder_t *d = new licp_obc_decoder_t;
    licp_obc_decoder_init(d);
    std::vector<uint8_t> types;
    for (uint8_t byte : stream) {
        size_t used = 0;
        licp_obc_msg_t m;
        if (licp_obc_decoder_feed(d, &byte, 1, &used, &m)) {
            types.push_back(m.type);
        }
    }
    ASSERT_EQ(types.size(), 2u);
    EXPECT_EQ(types[0], LICP_OBC_FRAMES_REQUEST);
    EXPECT_EQ(types[1], LICP_OBC_STATUS_REQUEST);
    EXPECT_GT(d->resyncs, 0u);
    delete d;
}

TEST(Obc, BadChecksumIsDroppedAndNextMessageStillDecodes)
{
    uint8_t a[32], b[32];
    size_t na = licp_obc_encode(LICP_OBC_STATUS_REQUEST, 1, nullptr, 0, a, sizeof(a));
    size_t nb = licp_obc_encode(LICP_OBC_CONTACT_END, 2, nullptr, 0, b, sizeof(b));
    a[na - 1] ^= 0xFF;
    std::vector<uint8_t> s(a, a + na);
    s.insert(s.end(), b, b + nb);
    licp_obc_decoder_t *d = new licp_obc_decoder_t;
    licp_obc_decoder_init(d);
    size_t off = 0;
    int got = 0;
    uint8_t type = 0;
    while (off < s.size()) {
        size_t used = 0;
        licp_obc_msg_t m;
        bool ok = licp_obc_decoder_feed(d, s.data() + off, s.size() - off, &used, &m);
        off += used;
        if (ok) {
            ++got;
            type = m.type;
        } else if (used == 0) {
            break;
        }
    }
    EXPECT_EQ(got, 1);
    EXPECT_EQ(type, LICP_OBC_CONTACT_END);
    EXPECT_EQ(d->crc_errors, 1u);
    delete d;
}

TEST(Obc, EncodeRejectsOversizedPayload)
{
    std::vector<uint8_t> big(LICP_OBC_MAX_PAYLOAD + 1);
    std::vector<uint8_t> out(big.size() + 16);
    EXPECT_EQ(licp_obc_encode(1, 0, big.data(), (uint16_t)big.size(), out.data(), out.size()), 0u);
}

/* ---------------------- Frequency-hopping receiver ---------------------- */

TEST(Fhss, RecordRoundTrip)
{
    licp_rx_frame_t in;
    std::memset(&in, 0, sizeof(in));
    in.len = 25;
    for (size_t i = 0; i < in.len; ++i) {
        in.phy[i] = static_cast<uint8_t>(i);
    }
    in.freq_hz = 855712400u;
    in.gnss_ms = 1400000000123ull;
    in.rssi_x10 = -1301;
    in.snr_x10 = -152;
    in.datarate = 1;
    in.flags = LICP_RX_FLAG_HEADER_ONLY;
    uint8_t rec[LICP_FHSS_RECORD_LEN];
    licp_fhss_encode_record(&in, rec);
    licp_rx_frame_t out;
    ASSERT_EQ(licp_fhss_decode_record(rec, &out), LICP_OK);
    EXPECT_EQ(out.len, 25u);
    EXPECT_EQ(out.freq_hz, in.freq_hz);
    EXPECT_EQ(out.gnss_ms, in.gnss_ms);
    EXPECT_EQ(out.rssi_x10, -1301);
    EXPECT_EQ(out.flags, LICP_RX_FLAG_HEADER_ONLY);
    EXPECT_EQ(0, std::memcmp(out.phy, in.phy, 25));
    rec[0] = 0;
    EXPECT_EQ(licp_fhss_decode_record(rec, &out), LICP_ERR_FORMAT);
}

TEST(Fhss, BackEndReadsRecordsFromDevice)
{
    TempDir dir;
    std::string path = dir.file("fhss0");
    {
        std::ofstream f(path, std::ios::binary);
        for (int i = 0; i < 3; ++i) {
            licp_rx_frame_t fr;
            std::memset(&fr, 0, sizeof(fr));
            fr.len = 12 + i;
            fr.freq_hz = 855300000u;
            fr.gnss_ms = 1000u * i;
            uint8_t rec[LICP_FHSS_RECORD_LEN];
            licp_fhss_encode_record(&fr, rec);
            f.write(reinterpret_cast<const char *>(rec), sizeof(rec));
        }
        f.write("xx", 2); /* partial record: kept for the next read */
    }
    licp_radio_fhss_t fh;
    licp_radio_t radio;
    ASSERT_EQ(licp_radio_fhss_init(&fh, path.c_str(), &radio), LICP_OK);
    ASSERT_EQ(radio.ops->start(radio.ctx), LICP_OK);
    licp_rx_frame_t out[8];
    ASSERT_EQ(radio.ops->receive(radio.ctx, out, 8), 3u);
    EXPECT_EQ(out[2].len, 14u);
    EXPECT_EQ(out[2].gnss_ms, 2000u);
    EXPECT_EQ(fh.have, 2u);
    licp_tx_request_t req;
    EXPECT_EQ(radio.ops->transmit(radio.ctx, &req), LICP_ERR_UNSUPPORTED);
    radio.ops->stop(radio.ctx);
}

/* ---------------------------- Configuration ----------------------------- */

TEST(Config, DefaultsAreValid)
{
    licp_config_t c;
    licp_config_default(&c);
    EXPECT_EQ(licp_config_validate(&c), LICP_OK);
    EXPECT_EQ(c.region.channel_count, 15u);
    EXPECT_EQ(licp_sf_length_ms(&c.sf), 45500u);
}

TEST(Config, SetsTypedValues)
{
    licp_config_t c;
    licp_config_default(&c);
    EXPECT_EQ(licp_config_set(&c, "sat_id", "3"), LICP_OK);
    EXPECT_EQ(c.sat_id, 3u);
    EXPECT_EQ(licp_config_set(&c, "group_key", "000102030405060708090a0b0c0d0e0f"), LICP_OK);
    EXPECT_EQ(c.group_key[15], 0x0Fu);
    EXPECT_EQ(licp_config_set(&c, "sos_channels", "1,9"), LICP_OK);
    EXPECT_EQ(c.region.sos_count, 2u);
    EXPECT_EQ(c.region.sos_channels[1], 9u);
    EXPECT_EQ(licp_config_set(&c, "collision_high", "0.4"), LICP_OK);
    EXPECT_DOUBLE_EQ(c.load.collision_high, 0.4);
    EXPECT_EQ(licp_config_set(&c, "store_fsync_each", "no"), LICP_OK);
    EXPECT_FALSE(c.store_fsync_each);
    EXPECT_EQ(licp_config_set(&c, "downlink_power_dbm", "14"), LICP_OK);
    EXPECT_EQ(c.region.downlink_power_dbm, 14);
}

TEST(Config, RejectsBadValuesAndUnknownKeys)
{
    licp_config_t c;
    licp_config_default(&c);
    EXPECT_EQ(licp_config_set(&c, "sat_id", "256"), LICP_ERR_PARAM);
    EXPECT_EQ(licp_config_set(&c, "sat_id", "-1"), LICP_ERR_PARAM);
    EXPECT_EQ(licp_config_set(&c, "group_key", "00"), LICP_ERR_PARAM);
    EXPECT_EQ(licp_config_set(&c, "collision_high", "abc"), LICP_ERR_PARAM);
    EXPECT_EQ(licp_config_set(&c, "no_such_key", "1"), LICP_ERR_NOT_FOUND);
}

TEST(Config, LoadsFileAndReportsBadLine)
{
    TempDir dir;
    std::string p = dir.file("licp.conf");
    {
        std::ofstream f(p);
        f << "# comment\n\n  sat_id = 9 \nslot_count=12\nbogus line\n";
    }
    licp_config_t c;
    licp_config_default(&c);
    int bad = 0;
    EXPECT_EQ(licp_config_load(&c, p.c_str(), &bad), LICP_ERR_FORMAT);
    EXPECT_EQ(bad, 5);
    EXPECT_EQ(c.sat_id, 9u);
    EXPECT_EQ(licp_config_load(&c, dir.file("missing").c_str(), &bad), LICP_ERR_IO);
}

TEST(Config, ExampleFileLoadsCleanly)
{
    licp_config_t c;
    licp_config_default(&c);
    int bad = 0;
    ASSERT_EQ(licp_config_load(&c, LICP_SOURCE_DIR "/config/licp.conf.example", &bad), LICP_OK) << "line " << bad;
    EXPECT_EQ(licp_config_validate(&c), LICP_OK);
}
