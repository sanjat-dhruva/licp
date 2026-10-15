/**
 * @file test_service.cpp
 * @brief End-to-end tests of the on-board service with the simulated radio
 *        and a manual clock: plan upload, reception, beacon, bitmap,
 *        hand-over to the ground, release, call-in and restart.
 */
#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <vector>

#include "test_util.hpp"

extern "C" {
#include "licp/app/licp_service.h"
#include "licp/common/licp_bytes.h"
#include "licp/core/licp_plan.h"
#include "licp/hal/licp_radio_sim.h"
#include "licp/proto/licp_beacon.h"
}

using namespace licp_test;

namespace {
const uint8_t GROUP_KEY[16] = {0x5A, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
const uint32_t DEV_A = 0x26000001; /* short id 1, home channel 3, slot 4 */
const uint32_t DEV_B = 0x26000002; /* short id 2 */
const uint32_t CH3_HZ = 855700000u;
const uint32_t SOS_HZ = 856700000u;
const uint32_t SF_LEN = 45500u;
const uint32_t N = 30000000u; /* a superframe number around 2023 in GPS time */

std::map<uint8_t, uint32_t> parse_status(const licp_obc_msg_t &m)
{
    std::map<uint8_t, uint32_t> tm;
    for (size_t i = 0; i + 5 <= m.len; i += 5) {
        tm[m.payload[i]] = licp_get_u32(m.payload + i + 1);
    }
    return tm;
}
} // namespace

class Service : public ::testing::Test {
protected:
    void SetUp() override
    {
        licp_config_default(&cfg);
        std::snprintf(cfg.store_path, sizeof(cfg.store_path), "%s", dir.file("frames.log").c_str());
        std::snprintf(cfg.device_table_path, sizeof(cfg.device_table_path), "%s", dir.file("devices.bin").c_str());
        cfg.store_fsync_each = false;
        cfg.log_level = 0;
        start_service(uint64_t(N) * SF_LEN + 3500u + 4u * 3500u + 100u); /* slot 4 of superframe N */
    }
    void start_service(uint64_t t0)
    {
        licp_clock_manual_init(&mclock, t0, &clock);
        ASSERT_EQ(licp_service_init(&svc, &cfg, &clock), LICP_OK);
        licp_radio_sim_init(&sim, &radio);
        ASSERT_EQ(licp_service_add_rx(&svc, &radio), LICP_OK);
        licp_service_set_tx(&svc, &radio);
        ASSERT_EQ(licp_service_start(&svc), LICP_OK);
    }
    void TearDown() override { licp_service_shutdown(&svc); }

    /** Send a message as the on-board computer would and decode the reply. */
    bool obc(uint8_t type, const std::vector<uint8_t> &payload, licp_obc_msg_t *reply)
    {
        uint8_t wire[LICP_OBC_HDR_LEN + 4096 + 2];
        size_t n = licp_obc_encode(type, 1, payload.data(), (uint16_t)payload.size(), wire, sizeof(wire));
        licp_obc_decoder_init(&dec_in);
        size_t used;
        licp_obc_msg_t msg;
        if (!licp_obc_decoder_feed(&dec_in, wire, n, &used, &msg)) {
            return false;
        }
        size_t rn = licp_service_handle_obc(&svc, &msg, reply_buf, sizeof(reply_buf));
        if (rn == 0) {
            return false;
        }
        licp_obc_decoder_init(&dec_out);
        return licp_obc_decoder_feed(&dec_out, reply_buf, rn, &used, reply);
    }

    void upload_basic_plan(bool with_unicast = false)
    {
        std::vector<uint8_t> buf(1024);
        licp_plan_builder_t b;
        licp_plan_begin(&b, buf.data(), buf.size(), 501);
        licp_device_t a = make_device(DEV_A, 1, 2, 3, 4), bb = make_device(DEV_B, 2, 3, 5, 6);
        licp_plan_add_device(&b, &a);
        licp_plan_add_device(&b, &bb);
        licp_plan_add(&b, LICP_PLAN_GROUP_KEY, GROUP_KEY, 16);
        licp_orbit_t o;
        std::memset(&o, 0, sizeof(o));
        o.sat_id = 1;
        o.mean_motion_e8 = 1510000000u;
        o.incl_e2 = 9750;
        o.validity_days = 7;
        uint8_t rec[LICP_ORBIT_RECORD_LEN];
        ASSERT_EQ(licp_orbit_pack(&o, rec), LICP_OK);
        licp_plan_add(&b, LICP_PLAN_ORBIT, rec, sizeof(rec));
        if (with_unicast) {
            uint8_t uc[6 + 20];
            licp_put_u32(uc, DEV_A);
            licp_put_u16(uc + 4, 1);
            std::memset(uc + 6, 0x60, 20);
            licp_plan_add(&b, LICP_PLAN_UNICAST, uc, sizeof(uc));
        }
        buf.resize(licp_plan_end(&b));
        licp_obc_msg_t r;
        ASSERT_TRUE(obc(LICP_OBC_PLAN_UPLOAD, buf, &r));
        ASSERT_EQ(r.type, LICP_OBC_PLAN_RESULT);
        EXPECT_EQ(licp_get_u32(r.payload), 501u);
        EXPECT_EQ((int8_t)r.payload[4], 0);
    }

    void inject(uint32_t addr, uint32_t fcnt, uint32_t hz)
    {
        auto phy = make_uplink(addr, fcnt);
        licp_rx_frame_t rx = make_rx(phy, mclock.now, hz);
        ASSERT_EQ(licp_radio_sim_inject(&sim, &rx), LICP_OK);
    }

    /** Move the clock to just before superframe @p sf opens and poll once. */
    void run_until_window(uint32_t sf)
    {
        mclock.now = uint64_t(sf) * SF_LEN - 400u;
        licp_service_poll(&svc);
    }

    TempDir dir;
    licp_config_t cfg;
    licp_clock_manual_t mclock;
    licp_clock_t clock;
    licp_service_t svc;
    licp_radio_sim_t sim;
    licp_radio_t radio;
    licp_obc_decoder_t dec_in, dec_out;
    uint8_t reply_buf[LICP_OBC_HDR_LEN + LICP_OBC_MAX_PAYLOAD + LICP_OBC_CRC_LEN];
};

TEST_F(Service, PlanUploadLoadsDevicesAndKey)
{
    upload_basic_plan();
    EXPECT_EQ(licp_device_table_count(&svc.devices), 2u);
    EXPECT_EQ(0, std::memcmp(svc.cfg.group_key, GROUP_KEY, 16));
    EXPECT_EQ(svc.orbit_count, 1u);
    EXPECT_EQ(svc.plan_seq, 501u);
}

TEST_F(Service, CorruptPlanIsRejectedWithStatus)
{
    std::vector<uint8_t> junk = {'L', 'P', 'L', 'N', 1, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4};
    licp_obc_msg_t r;
    ASSERT_TRUE(obc(LICP_OBC_PLAN_UPLOAD, junk, &r));
    EXPECT_EQ((int8_t)r.payload[4], (int8_t)LICP_ERR_CRC);
    EXPECT_EQ(licp_device_table_count(&svc.devices), 0u);
}

TEST_F(Service, FullPassFromReceptionToRelease)
{
    upload_basic_plan();
    inject(DEV_A, 1, CH3_HZ);  /* routine frame in its home slot */
    inject(DEV_B, 1, SOS_HZ);  /* emergency frame */

    run_until_window(N + 1);
    ASSERT_EQ(svc.pipeline.outcomes[LICP_RX_ACCEPTED], 2u);

    /* Beacon first, then the acknowledgement bitmap. */
    ASSERT_EQ(sim.tx_count, 2u);
    const licp_tx_request_t &bt = sim.tx[0];
    EXPECT_EQ(bt.time_ms, uint64_t(N + 1) * SF_LEN + 50u);
    EXPECT_EQ(bt.freq_hz, 856500000u);
    licp_beacon_t b;
    ASSERT_EQ(licp_beacon_decode(bt.payload, bt.len, GROUP_KEY, &b), LICP_OK);
    EXPECT_EQ(b.superframe, N + 1);
    ASSERT_EQ(b.sos_ack_count, 1u);
    EXPECT_EQ(b.sos_acks[0], 2u);
    EXPECT_TRUE(b.has_orbit);
    EXPECT_EQ(b.access_p[0], 255u);

    licp_bitmap_t bm;
    uint8_t sat;
    uint16_t first, count;
    ASSERT_EQ(licp_ackmap_decode(sim.tx[1].payload, sim.tx[1].len, GROUP_KEY, &sat, &bm, &first, &count), LICP_OK);
    EXPECT_TRUE(licp_bitmap_test(&bm, 1));
    EXPECT_TRUE(licp_bitmap_test(&bm, 2));
    EXPECT_FALSE(licp_bitmap_test(&bm, 0));

    /* Polling again inside the same window plans nothing new. */
    licp_service_poll(&svc);
    EXPECT_EQ(sim.tx_count, 2u);

    /* Ground contact: request frames, emergency first. */
    licp_obc_msg_t r;
    ASSERT_TRUE(obc(LICP_OBC_FRAMES_REQUEST, {10, 0}, &r));
    ASSERT_EQ(r.type, LICP_OBC_FRAMES);
    ASSERT_EQ(licp_get_u16(r.payload), 2u);
    uint32_t seq0 = licp_get_u32(r.payload + 2);
    uint32_t dev0 = licp_get_u32(r.payload + 2 + 4 + 18);
    uint8_t prio0 = r.payload[2 + 4 + 26];
    uint8_t len0 = r.payload[2 + 4 + 28];
    EXPECT_EQ(dev0, DEV_B);
    EXPECT_EQ(prio0, 0u);
    size_t second = 2 + 4 + 28 + 1 + len0;
    uint32_t seq1 = licp_get_u32(r.payload + second);

    /* Release both; telemetry shows nothing pending. */
    std::vector<uint8_t> rel(2 + 8);
    licp_put_u16(rel.data(), 2);
    licp_put_u32(rel.data() + 2, seq0);
    licp_put_u32(rel.data() + 6, seq1);
    ASSERT_TRUE(obc(LICP_OBC_RELEASE, rel, &r));
    ASSERT_EQ(r.type, LICP_OBC_STATUS);
    auto tm = parse_status(r);
    EXPECT_EQ(tm[LICP_TM_PENDING], 0u);
    EXPECT_EQ(tm[LICP_TM_DEVICES], 2u);
    EXPECT_EQ(tm[LICP_TM_RX_ACCEPTED], 2u);
    EXPECT_EQ(tm[LICP_TM_PLAN_SEQ], 501u);
    EXPECT_EQ(tm[LICP_TM_TX_OK], 2u);
}

TEST_F(Service, ContactEndReturnsUnconfirmedFramesToPending)
{
    upload_basic_plan();
    inject(DEV_A, 1, CH3_HZ);
    run_until_window(N + 1);
    licp_obc_msg_t r;
    ASSERT_TRUE(obc(LICP_OBC_FRAMES_REQUEST, {10, 0}, &r));
    ASSERT_EQ(licp_get_u16(r.payload), 1u);
    ASSERT_TRUE(obc(LICP_OBC_FRAMES_REQUEST, {10, 0}, &r));
    EXPECT_EQ(licp_get_u16(r.payload), 0u); /* in flight */
    ASSERT_TRUE(obc(LICP_OBC_CONTACT_END, {}, &r));
    EXPECT_EQ(parse_status(r)[LICP_TM_IN_FLIGHT], 0u);
    ASSERT_TRUE(obc(LICP_OBC_FRAMES_REQUEST, {10, 0}, &r));
    EXPECT_EQ(licp_get_u16(r.payload), 1u);
}

TEST_F(Service, UnicastIsCalledInThenSentAfterDeviceAnswers)
{
    upload_basic_plan(true);
    run_until_window(N + 1);
    licp_beacon_t b;
    ASSERT_GE(sim.tx_count, 1u);
    ASSERT_EQ(licp_beacon_decode(sim.tx[0].payload, sim.tx[0].len, GROUP_KEY, &b), LICP_OK);
    ASSERT_EQ(b.call_in_count, 1u);
    EXPECT_EQ(b.call_ins[0], 1u);

    /* Device answers in the last slot before the next window. */
    mclock.now = uint64_t(N + 2) * SF_LEN - 1000u;
    inject(DEV_A, 1, CH3_HZ);
    licp_radio_sim_clear_tx(&sim);
    run_until_window(N + 2);
    bool found = false;
    for (size_t i = 0; i < sim.tx_count; ++i) {
        if (sim.tx[i].len == 20u && sim.tx[i].payload[0] == 0x60) {
            found = true;
            EXPECT_GT(sim.tx[i].time_ms, sim.tx[0].time_ms);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(Service, StateSurvivesRestart)
{
    upload_basic_plan();
    inject(DEV_A, 41, CH3_HZ);
    run_until_window(N + 1);
    uint64_t now = mclock.now;
    licp_service_shutdown(&svc);

    start_service(now + 1000u);
    EXPECT_EQ(licp_device_table_count(&svc.devices), 2u);
    licp_device_t *d = licp_device_table_find(&svc.devices, DEV_A);
    ASSERT_NE(d, nullptr);
    EXPECT_TRUE(d->fcnt_valid);
    EXPECT_EQ(d->fcnt_up_last, 41u);
    licp_store_stats_t st;
    licp_store_get_stats(svc.store, &st);
    EXPECT_EQ(st.pending, 1u);
    /* A replay of the stored frame after restart is a duplicate. */
    inject(DEV_A, 41, CH3_HZ);
    licp_service_poll(&svc);
    EXPECT_EQ(svc.pipeline.outcomes[LICP_RX_DUPLICATE], 1u);
}

TEST_F(Service, NewPassStartsAfterQuietGap)
{
    upload_basic_plan();
    inject(DEV_A, 1, CH3_HZ);
    run_until_window(N + 1);
    EXPECT_EQ(licp_bitmap_count(&svc.bitmap), 1u);
    mclock.now += cfg.pass_gap_ms + 1000u;
    licp_service_poll(&svc);
    EXPECT_EQ(svc.pass_id, 1u);
    EXPECT_EQ(licp_bitmap_count(&svc.bitmap), 0u);
}

TEST_F(Service, BeaconExtensionSectionIsBroadcast)
{
    upload_basic_plan();
    static const uint8_t data[2] = {0xCA, 0xFE};
    licp_beacon_extra_t x = {0x90, 2, data};
    ASSERT_EQ(licp_service_set_beacon_extra(&svc, &x, 1), LICP_OK);
    run_until_window(N + 1);
    ASSERT_GE(sim.tx_count, 1u);
    /* The extension is the last section, just before the 4-byte tag. */
    const licp_tx_request_t &t = sim.tx[0];
    EXPECT_EQ(t.payload[t.len - 8], 0x90);
    EXPECT_EQ(t.payload[t.len - 6], 0xCA);
}
