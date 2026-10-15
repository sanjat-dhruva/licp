/**
 * @file test_pipeline.cpp
 * @brief Per-frame processing: every branch of the "checked, then stored" flow.
 */
#include <gtest/gtest.h>

#include <cstring>

#include "test_util.hpp"

extern "C" {
#include "licp/core/licp_pipeline.h"
}

using namespace licp_test;

namespace {
const uint64_t EPOCH = 0;
/* Home slot 4 of superframe 10: 10*45500 + 3500 + 4*3500 = 472500 .. 476000 */
const uint64_t T_HOME = 10u * 45500u + 3500u + 4u * 3500u + 500u;
const uint64_t T_OTHER_SLOT = 10u * 45500u + 3500u + 6u * 3500u + 500u;
const uint32_t CH3_HZ = 855700000u;  /* home channel 3 */
const uint32_t CH0_HZ = 855100000u;  /* emergency channel */
} // namespace

class Pipeline : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_EQ(licp_device_table_init(&devs, 16), LICP_OK);
        path = dir.file("frames.log");
        licp_store_config_t sc = {path.c_str(), 1024 * 1024, false};
        ASSERT_EQ(licp_store_open(&store, &sc), LICP_OK);
        licp_load_config_t lc;
        licp_load_default_config(&lc);
        ASSERT_EQ(licp_load_init(&load, &lc), LICP_OK);
        ASSERT_EQ(licp_bitmap_reset(&bm, 128, 1), LICP_OK);
        licp_region_default(&region);
        licp_sf_default(&sf);
        sf.epoch_ms = EPOCH;
        licp_pipeline_config_t pc;
        licp_pipeline_default_config(&pc);
        ASSERT_EQ(licp_pipeline_init(&p, &pc, &devs, store, &load, &bm, &region, &sf), LICP_OK);
        licp_device_t d = make_device(0x26000001, 5, 2, 3, 4);
        ASSERT_EQ(licp_device_table_upsert(&devs, &d), LICP_OK);
    }
    void TearDown() override
    {
        licp_store_close(store);
        licp_device_table_free(&devs);
    }
    licp_rx_outcome_t send(uint32_t addr, uint32_t fcnt, uint64_t t = T_HOME, uint32_t hz = CH3_HZ)
    {
        auto phy = make_uplink(addr, fcnt);
        licp_rx_frame_t rx = make_rx(phy, t, hz);
        return licp_pipeline_process(&p, &rx);
    }
    licp_store_stats_t stats()
    {
        licp_store_stats_t st;
        licp_store_get_stats(store, &st);
        return st;
    }
    TempDir dir;
    std::string path;
    licp_device_table_t devs;
    licp_store_t *store = nullptr;
    licp_load_t load;
    licp_bitmap_t bm;
    licp_region_t region;
    licp_sf_config_t sf;
    licp_pipeline_t p;
};

TEST_F(Pipeline, AcceptsValidFrameAndUpdatesState)
{
    EXPECT_EQ(send(0x26000001, 1), LICP_RX_ACCEPTED);
    licp_device_t *d = licp_device_table_find(&devs, 0x26000001);
    EXPECT_TRUE(d->fcnt_valid);
    EXPECT_EQ(d->fcnt_up_last, 1u);
    EXPECT_TRUE(licp_bitmap_test(&bm, 5));
    EXPECT_EQ(stats().pending_by_class[2], 1u);
    EXPECT_EQ(load.cur.decoded_home, 1u); /* home slot, home channel */
}

TEST_F(Pipeline, FrameOutsideHomeSlotCountsAsSharedLoad)
{
    EXPECT_EQ(send(0x26000001, 1, T_OTHER_SLOT), LICP_RX_ACCEPTED);
    EXPECT_EQ(load.cur.decoded_shared[2], 1u);
}

TEST_F(Pipeline, DuplicateIsDropped)
{
    EXPECT_EQ(send(0x26000001, 7), LICP_RX_ACCEPTED);
    EXPECT_EQ(send(0x26000001, 7), LICP_RX_DUPLICATE);
    EXPECT_EQ(stats().pending, 1u);
}

TEST_F(Pipeline, UnknownAndBlockedDevicesAreDropped)
{
    EXPECT_EQ(send(0x26009999, 1), LICP_RX_UNKNOWN_DEVICE);
    licp_device_table_find(&devs, 0x26000001)->flags = LICP_DEV_FLAG_BLOCKED;
    EXPECT_EQ(send(0x26000001, 1), LICP_RX_BLOCKED);
    EXPECT_EQ(stats().pending, 0u);
}

TEST_F(Pipeline, WrongKeyFailsIntegrityCheck)
{
    const uint8_t other[16] = {0xFF};
    auto phy = make_uplink(0x26000001, 1, other);
    licp_rx_frame_t rx = make_rx(phy, T_HOME, CH3_HZ);
    EXPECT_EQ(licp_pipeline_process(&p, &rx), LICP_RX_BAD_MIC);
    EXPECT_FALSE(licp_device_table_find(&devs, 0x26000001)->fcnt_valid);
}

TEST_F(Pipeline, CounterJumpBeyondGapIsDropped)
{
    EXPECT_EQ(send(0x26000001, 10), LICP_RX_ACCEPTED);
    EXPECT_EQ(send(0x26000001, 10 + 16384), LICP_RX_ACCEPTED); /* exactly the gap: allowed */
    EXPECT_EQ(send(0x26000001, 10 + 16384 + 16385), LICP_RX_BAD_COUNTER);
}

TEST_F(Pipeline, OldFrameReplayIsDropped)
{
    EXPECT_EQ(send(0x26000001, 100), LICP_RX_ACCEPTED);
    /* Counter 50 looks like 0x10032 after rebuild: integrity fails with that counter. */
    EXPECT_EQ(send(0x26000001, 50), LICP_RX_BAD_MIC);
}

TEST_F(Pipeline, CounterRollsOverSixteenBits)
{
    licp_device_t *d = licp_device_table_find(&devs, 0x26000001);
    d->fcnt_up_last = 0xFFFE;
    d->fcnt_valid = true;
    EXPECT_EQ(send(0x26000001, 0x10001), LICP_RX_ACCEPTED);
    EXPECT_EQ(d->fcnt_up_last, 0x10001u);
}

TEST_F(Pipeline, EmergencyChannelGivesClassZeroAndQueuesAck)
{
    EXPECT_EQ(send(0x26000001, 1, T_HOME, CH0_HZ), LICP_RX_ACCEPTED);
    EXPECT_EQ(stats().pending_by_class[0], 1u);
    EXPECT_EQ(p.sos_count, 1u);
    EXPECT_EQ(send(0x26000001, 2, T_HOME, CH0_HZ), LICP_RX_ACCEPTED);
    EXPECT_EQ(p.sos_count, 1u); /* same device queued once */
    uint16_t ids[4];
    EXPECT_EQ(licp_pipeline_take_sos_acks(&p, ids, 4), 1u);
    EXPECT_EQ(ids[0], 5u);
    EXPECT_EQ(p.sos_count, 0u);
    licp_pipeline_return_sos_acks(&p, ids, 1);
    EXPECT_EQ(p.sos_count, 1u);
}

TEST_F(Pipeline, HeaderOnlyEventCountsCollision)
{
    licp_rx_frame_t rx = make_rx({}, T_HOME, CH3_HZ, LICP_RX_FLAG_HEADER_ONLY);
    EXPECT_EQ(licp_pipeline_process(&p, &rx), LICP_RX_COLLISION);
    EXPECT_EQ(load.cur.header_only, 1u);
}

TEST_F(Pipeline, UnknownFrequencyIsBadFormat)
{
    EXPECT_EQ(send(0x26000001, 1, T_HOME, 860000000u), LICP_RX_BAD_FORMAT);
}

namespace {
int g_hook_calls = 0;
licp_rx_outcome_t g_last = LICP_RX_OUTCOME_COUNT;
void hook(licp_rx_outcome_t o, const licp_frame_t *, const licp_device_t *, const licp_rx_frame_t *, void *user)
{
    ++g_hook_calls;
    g_last = o;
    *static_cast<int *>(user) += 1;
}
} // namespace

TEST_F(Pipeline, HooksSeeEveryOutcome)
{
    int user = 0;
    ASSERT_EQ(licp_pipeline_add_hook(&p, hook, &user), LICP_OK);
    g_hook_calls = 0;
    send(0x26000001, 1);
    EXPECT_EQ(g_last, LICP_RX_ACCEPTED);
    send(0x26000001, 1);
    EXPECT_EQ(g_last, LICP_RX_DUPLICATE);
    EXPECT_EQ(g_hook_calls, 2);
    EXPECT_EQ(user, 2);
    EXPECT_EQ(p.outcomes[LICP_RX_ACCEPTED], 1u);
    EXPECT_EQ(p.outcomes[LICP_RX_DUPLICATE], 1u);
}
