/**
 * @file test_downlink_plan.cpp
 * @brief Downlink queue and window planner; upload plan validation and application.
 */
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "test_util.hpp"

extern "C" {
#include "licp/core/licp_downlink.h"
#include "licp/core/licp_plan.h"
#include "licp/region/licp_region.h"
}

/* ------------------------------ Downlinks ------------------------------- */

class Downlink : public ::testing::Test {
protected:
    void SetUp() override
    {
        licp_region_default(&region);
        licp_dl_config_t c;
        licp_dl_default_config(&c);
        licp_dlq_init(&q, &c);
        std::memset(beacon, 0xB0, sizeof(beacon));
    }
    size_t plan(uint64_t start = 100000)
    {
        return licp_dl_plan_window(&q, &region, start, 3500, beacon, sizeof(beacon), tx, LICP_DL_MAX_TX);
    }
    licp_region_t region;
    licp_dl_queue_t q;
    uint8_t beacon[40];
    licp_tx_request_t tx[LICP_DL_MAX_TX];
};

TEST_F(Downlink, BeaconAloneStartsAfterLead)
{
    ASSERT_EQ(plan(), 1u);
    EXPECT_EQ(tx[0].time_ms, 100050u);
    EXPECT_EQ(tx[0].freq_hz, 856500000u);
    EXPECT_EQ(tx[0].mod.sf, 10u);
    EXPECT_GT(tx[0].airtime_us, 0u);
}

TEST_F(Downlink, UnicastFollowsCallInSequence)
{
    const uint8_t frame[20] = {0x60};
    ASSERT_EQ(licp_dlq_add(&q, LICP_DL_UNICAST, 0xABCD, 42, 0, frame, sizeof(frame)), LICP_OK);
    EXPECT_EQ(plan(), 1u); /* not yet called in: nothing extra */

    uint16_t ids[8];
    ASSERT_EQ(licp_dlq_take_call_ins(&q, ids, 8), 1u);
    EXPECT_EQ(ids[0], 42u);
    EXPECT_EQ(plan(), 1u); /* called in, device not heard yet */

    licp_dlq_on_uplink(&q, 0xABCD);
    ASSERT_EQ(plan(), 2u);
    EXPECT_EQ(tx[1].len, 20u);
    EXPECT_GE(tx[1].time_ms, tx[0].time_ms + tx[0].airtime_us / 1000u + 30u);
    EXPECT_EQ(q.count, 0u);
}

TEST_F(Downlink, UnansweredUnicastIsDroppedAfterMaxCallIns)
{
    const uint8_t frame[10] = {0};
    ASSERT_EQ(licp_dlq_add(&q, LICP_DL_UNICAST, 1, 7, 0, frame, sizeof(frame)), LICP_OK);
    uint16_t ids[8];
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(licp_dlq_take_call_ins(&q, ids, 8), 1u);
    }
    EXPECT_EQ(licp_dlq_take_call_ins(&q, ids, 8), 0u);
    EXPECT_EQ(q.dropped, 1u);
    EXPECT_EQ(q.count, 0u);
}

TEST_F(Downlink, BroadcastSentOnlyAfterAnnouncement)
{
    const uint8_t frame[30] = {0};
    ASSERT_EQ(licp_dlq_add(&q, LICP_DL_BROADCAST, 0, 0, 3, frame, sizeof(frame)), LICP_OK);
    EXPECT_EQ(plan(), 1u);
    uint8_t groups[4];
    ASSERT_EQ(licp_dlq_take_broadcasts(&q, groups, 4), 1u);
    EXPECT_EQ(groups[0], 3u);
    EXPECT_EQ(plan(), 2u);
}

TEST_F(Downlink, OnlyOneAckMapIsKept)
{
    const uint8_t a[14] = {1}, b[15] = {2};
    ASSERT_EQ(licp_dlq_add(&q, LICP_DL_ACKMAP, 0, 0, 0, a, sizeof(a)), LICP_OK);
    ASSERT_EQ(licp_dlq_add(&q, LICP_DL_ACKMAP, 0, 0, 0, b, sizeof(b)), LICP_OK);
    EXPECT_EQ(q.count, 1u);
    ASSERT_EQ(plan(), 2u);
    EXPECT_EQ(tx[1].len, 15u);
}

TEST_F(Downlink, WhatDoesNotFitWaitsForNextWindow)
{
    /* SF10 frames of 115 bytes take about 1.09 s: beacon + 2 fit in 3.5 s, the rest wait. */
    uint8_t big[115] = {0};
    uint8_t groups[4];
    for (int i = 0; i < 4; ++i) {
        ASSERT_EQ(licp_dlq_add(&q, LICP_DL_BROADCAST, 0, 0, (uint8_t)i, big, sizeof(big)), LICP_OK);
    }
    ASSERT_EQ(licp_dlq_take_broadcasts(&q, groups, 4), 4u);
    size_t n = plan();
    EXPECT_GE(n, 2u);
    EXPECT_LT(n, 5u);
    const licp_tx_request_t &last = tx[n - 1];
    EXPECT_LE(last.time_ms + (last.airtime_us + 999u) / 1000u, 100000u + 3500u);
    EXPECT_EQ(q.count, 5u - n);
    /* Leftovers lose their announcement and are announced again. */
    EXPECT_EQ(licp_dlq_take_broadcasts(&q, groups, 4), q.count);
}

/* ------------------------------ Upload plan ----------------------------- */

namespace {
struct Seen {
    int upserts = 0, removes = 0, orbits = 0, broadcasts = 0, unicasts = 0, keys = 0, params = 0, unknown = 0;
    uint32_t last_addr = 0;
    uint8_t last_param = 0;
};
licp_status_t s_upsert(void *u, const licp_device_t *d)
{
    auto *s = static_cast<Seen *>(u);
    s->upserts++;
    s->last_addr = d->dev_addr;
    return LICP_OK;
}
licp_status_t s_remove(void *u, uint32_t) { static_cast<Seen *>(u)->removes++; return LICP_OK; }
licp_status_t s_orbit(void *u, const licp_orbit_t *) { static_cast<Seen *>(u)->orbits++; return LICP_OK; }
licp_status_t s_bc(void *u, uint8_t, const uint8_t *, size_t) { static_cast<Seen *>(u)->broadcasts++; return LICP_OK; }
licp_status_t s_uc(void *u, uint32_t, uint16_t, const uint8_t *, size_t) { static_cast<Seen *>(u)->unicasts++; return LICP_OK; }
licp_status_t s_key(void *u, const uint8_t *) { static_cast<Seen *>(u)->keys++; return LICP_OK; }
licp_status_t s_param(void *u, uint8_t id, uint32_t)
{
    static_cast<Seen *>(u)->params++;
    static_cast<Seen *>(u)->last_param = id;
    return LICP_OK;
}
licp_status_t s_unknown(void *u, uint8_t, const uint8_t *, size_t) { static_cast<Seen *>(u)->unknown++; return LICP_OK; }

std::vector<uint8_t> full_plan()
{
    std::vector<uint8_t> buf(2048);
    licp_plan_builder_t b;
    licp_plan_begin(&b, buf.data(), buf.size(), 77);
    licp_device_t d = licp_test::make_device(0x260000AA, 9);
    licp_plan_add_device(&b, &d);
    uint8_t rm[4] = {1, 0, 0, 0};
    licp_plan_add(&b, LICP_PLAN_DEVICE_REMOVE, rm, 4);
    uint8_t orbit[26] = {0};
    orbit[5] = 1; /* mean motion must be non-zero */
    licp_plan_add(&b, LICP_PLAN_ORBIT, orbit, 26);
    uint8_t bc[6] = {2, 0xE0, 1, 2, 3, 4};
    licp_plan_add(&b, LICP_PLAN_BROADCAST, bc, sizeof(bc));
    uint8_t uc[12] = {0xAA, 0, 0, 0x26, 9, 0, 0x60, 1, 2, 3, 4, 5};
    licp_plan_add(&b, LICP_PLAN_UNICAST, uc, sizeof(uc));
    uint8_t key[16] = {7};
    licp_plan_add(&b, LICP_PLAN_GROUP_KEY, key, 16);
    uint8_t param[5] = {LICP_PARAM_MAX_FCNT_GAP, 0, 1, 0, 0};
    licp_plan_add(&b, LICP_PLAN_PARAM, param, 5);
    uint8_t ext[3] = {9, 9, 9};
    licp_plan_add(&b, 0x81, ext, 3);
    size_t n = licp_plan_end(&b);
    buf.resize(n);
    return buf;
}
} // namespace

TEST(Plan, AppliesEveryEntryType)
{
    auto plan = full_plan();
    ASSERT_FALSE(plan.empty());
    Seen s;
    licp_plan_handlers_t h = {s_upsert, s_remove, s_orbit, s_bc, s_uc, s_key, s_param, s_unknown, &s};
    uint32_t seq = 0;
    ASSERT_EQ(licp_plan_apply(plan.data(), plan.size(), &h, &seq), LICP_OK);
    EXPECT_EQ(seq, 77u);
    EXPECT_EQ(s.upserts, 1);
    EXPECT_EQ(s.last_addr, 0x260000AAu);
    EXPECT_EQ(s.removes, 1);
    EXPECT_EQ(s.orbits, 1);
    EXPECT_EQ(s.broadcasts, 1);
    EXPECT_EQ(s.unicasts, 1);
    EXPECT_EQ(s.keys, 1);
    EXPECT_EQ(s.params, 1);
    EXPECT_EQ(s.last_param, LICP_PARAM_MAX_FCNT_GAP);
    EXPECT_EQ(s.unknown, 1);
}

TEST(Plan, CorruptedPlanChangesNothing)
{
    auto plan = full_plan();
    plan[20] ^= 0x01;
    Seen s;
    licp_plan_handlers_t h = {s_upsert, s_remove, s_orbit, s_bc, s_uc, s_key, s_param, s_unknown, &s};
    EXPECT_EQ(licp_plan_apply(plan.data(), plan.size(), &h, nullptr), LICP_ERR_CRC);
    EXPECT_EQ(s.upserts + s.removes + s.orbits + s.broadcasts + s.unicasts + s.keys + s.params + s.unknown, 0);
}

TEST(Plan, RejectsWrongEntryLengthEvenWithGoodChecksum)
{
    std::vector<uint8_t> buf(256);
    licp_plan_builder_t b;
    licp_plan_begin(&b, buf.data(), buf.size(), 1);
    uint8_t key[15] = {0};
    licp_plan_add(&b, LICP_PLAN_GROUP_KEY, key, 15);
    size_t n = licp_plan_end(&b);
    ASSERT_GT(n, 0u);
    EXPECT_EQ(licp_plan_validate(buf.data(), n, nullptr), LICP_ERR_FORMAT);
}

TEST(Plan, RejectsBadMagicVersionAndTruncation)
{
    auto plan = full_plan();
    std::vector<uint8_t> bad = plan;
    bad[0] = 'X';
    EXPECT_EQ(licp_plan_validate(bad.data(), bad.size(), nullptr), LICP_ERR_FORMAT);
    EXPECT_EQ(licp_plan_validate(plan.data(), 10, nullptr), LICP_ERR_LENGTH);
    EXPECT_EQ(licp_plan_validate(plan.data(), plan.size() - 1, nullptr), LICP_ERR_CRC);
}

TEST(Plan, BuilderReportsOverflow)
{
    uint8_t small[20];
    licp_plan_builder_t b;
    licp_plan_begin(&b, small, sizeof(small), 1);
    uint8_t key[16] = {0};
    licp_plan_add(&b, LICP_PLAN_GROUP_KEY, key, 16);
    EXPECT_EQ(licp_plan_end(&b), 0u);
}
