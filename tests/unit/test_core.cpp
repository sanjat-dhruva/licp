/**
 * @file test_core.cpp
 * @brief Device table, superframe timing and access-probability control.
 */
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>

#include "test_util.hpp"

extern "C" {
#include "licp/core/licp_device_table.h"
#include "licp/core/licp_load.h"
#include "licp/core/licp_superframe.h"
}

using licp_test::make_device;
using licp_test::TempDir;

/* ----------------------------- Device table ----------------------------- */

class DeviceTable : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_EQ(licp_device_table_init(&t, 4), LICP_OK); }
    void TearDown() override { licp_device_table_free(&t); }
    licp_device_table_t t;
};

TEST_F(DeviceTable, KeepsRecordsSortedAndFindsThem)
{
    licp_device_t a = make_device(30, 3), b = make_device(10, 1), c = make_device(20, 2);
    ASSERT_EQ(licp_device_table_upsert(&t, &a), LICP_OK);
    ASSERT_EQ(licp_device_table_upsert(&t, &b), LICP_OK);
    ASSERT_EQ(licp_device_table_upsert(&t, &c), LICP_OK);
    EXPECT_EQ(t.items[0].dev_addr, 10u);
    EXPECT_EQ(t.items[2].dev_addr, 30u);
    ASSERT_NE(licp_device_table_find(&t, 20), nullptr);
    EXPECT_EQ(licp_device_table_find(&t, 25), nullptr);
    ASSERT_NE(licp_device_table_find_short(&t, 3), nullptr);
    EXPECT_EQ(licp_device_table_find_short(&t, 3)->dev_addr, 30u);
}

TEST_F(DeviceTable, RemoveAndCapacity)
{
    for (uint32_t i = 1; i <= 4; ++i) {
        licp_device_t d = make_device(i, (uint16_t)i);
        ASSERT_EQ(licp_device_table_upsert(&t, &d), LICP_OK);
    }
    licp_device_t extra = make_device(99, 99);
    EXPECT_EQ(licp_device_table_upsert(&t, &extra), LICP_ERR_FULL);
    EXPECT_EQ(licp_device_table_remove(&t, 2), LICP_OK);
    EXPECT_EQ(licp_device_table_remove(&t, 2), LICP_ERR_NOT_FOUND);
    EXPECT_EQ(licp_device_table_count(&t), 3u);
    EXPECT_EQ(licp_device_table_upsert(&t, &extra), LICP_OK);
}

TEST_F(DeviceTable, UploadCannotRollCounterBack)
{
    licp_device_t d = make_device(5, 5);
    d.fcnt_up_last = 100;
    d.fcnt_valid = true;
    ASSERT_EQ(licp_device_table_upsert(&t, &d), LICP_OK);
    d.fcnt_up_last = 40; /* older value from the ground */
    d.priority = 3;
    ASSERT_EQ(licp_device_table_upsert(&t, &d), LICP_OK);
    licp_device_t *cur = licp_device_table_find(&t, 5);
    EXPECT_EQ(cur->fcnt_up_last, 100u);
    EXPECT_EQ(cur->priority, 3u);
    d.fcnt_up_last = 150; /* newer value from the ground path */
    ASSERT_EQ(licp_device_table_upsert(&t, &d), LICP_OK);
    EXPECT_EQ(licp_device_table_find(&t, 5)->fcnt_up_last, 150u);
}

TEST_F(DeviceTable, SaveAndLoadRoundTrip)
{
    TempDir dir;
    licp_device_t a = make_device(0x11111111, 1), b = make_device(0x22222222, 2);
    b.fcnt_up_last = 77;
    b.fcnt_valid = true;
    b.flags = LICP_DEV_FLAG_BLOCKED;
    ASSERT_EQ(licp_device_table_upsert(&t, &a), LICP_OK);
    ASSERT_EQ(licp_device_table_upsert(&t, &b), LICP_OK);
    std::string path = dir.file("devices.bin");
    ASSERT_EQ(licp_device_table_save(&t, path.c_str()), LICP_OK);

    licp_device_table_t u;
    ASSERT_EQ(licp_device_table_init(&u, 4), LICP_OK);
    ASSERT_EQ(licp_device_table_load(&u, path.c_str()), LICP_OK);
    ASSERT_EQ(licp_device_table_count(&u), 2u);
    licp_device_t *lb = licp_device_table_find(&u, 0x22222222);
    ASSERT_NE(lb, nullptr);
    EXPECT_EQ(lb->fcnt_up_last, 77u);
    EXPECT_EQ(lb->flags, LICP_DEV_FLAG_BLOCKED);
    EXPECT_EQ(0, std::memcmp(lb->nwk_s_key, licp_test::nwk_key(), 16));
    licp_device_table_free(&u);
}

TEST_F(DeviceTable, CorruptedFileIsRejectedAndTableUnchanged)
{
    TempDir dir;
    licp_device_t a = make_device(1, 1);
    ASSERT_EQ(licp_device_table_upsert(&t, &a), LICP_OK);
    std::string path = dir.file("devices.bin");
    ASSERT_EQ(licp_device_table_save(&t, path.c_str()), LICP_OK);
    FILE *f = std::fopen(path.c_str(), "r+b");
    ASSERT_NE(f, nullptr);
    std::fseek(f, 15, SEEK_SET);
    std::fputc(0x5A, f);
    std::fclose(f);
    licp_device_t b = make_device(2, 2);
    ASSERT_EQ(licp_device_table_upsert(&t, &b), LICP_OK);
    EXPECT_EQ(licp_device_table_load(&t, path.c_str()), LICP_ERR_CRC);
    EXPECT_EQ(licp_device_table_count(&t), 2u);
    EXPECT_EQ(licp_device_table_load(&t, dir.file("missing").c_str()), LICP_ERR_IO);
}

/* ------------------------------ Superframe ------------------------------ */

TEST(Superframe, DefaultLengthIs45500ms)
{
    licp_sf_config_t c;
    licp_sf_default(&c);
    EXPECT_EQ(licp_sf_length_ms(&c), 45500u);
}

TEST(Superframe, LocatesBeaconWindowAndSlots)
{
    licp_sf_config_t c;
    licp_sf_default(&c);
    c.epoch_ms = 1000000;
    licp_sf_position_t p;

    ASSERT_EQ(licp_sf_locate(&c, 1000000, &p), LICP_OK);
    EXPECT_EQ(p.superframe, 0u);
    EXPECT_TRUE(p.in_beacon);
    EXPECT_EQ(p.next_beacon_ms, 1045500u);

    ASSERT_EQ(licp_sf_locate(&c, 1000000 + 3500, &p), LICP_OK);
    EXPECT_FALSE(p.in_beacon);
    EXPECT_EQ(p.slot, 0u);
    EXPECT_FALSE(p.second_half);

    ASSERT_EQ(licp_sf_locate(&c, 1000000 + 3500 + 3500 * 11 + 1750, &p), LICP_OK);
    EXPECT_EQ(p.slot, 11u);
    EXPECT_TRUE(p.second_half);
    EXPECT_EQ(p.offset_ms, 1750u);

    ASSERT_EQ(licp_sf_locate(&c, 1000000 + 45500 * 7 + 10, &p), LICP_OK);
    EXPECT_EQ(p.superframe, 7u);
    EXPECT_TRUE(p.in_beacon);

    EXPECT_EQ(licp_sf_locate(&c, 999999, &p), LICP_ERR_STATE);
    EXPECT_EQ(licp_sf_slot_start_ms(&c, 2, 3), 1000000u + 91000u + 3500u + 10500u);
}

/* ----------------------------- Load control ----------------------------- */

class Load : public ::testing::Test {
protected:
    void SetUp() override
    {
        licp_load_default_config(&cfg);
        ASSERT_EQ(licp_load_init(&l, &cfg), LICP_OK);
    }
    void superframe(uint32_t decoded, uint32_t collisions)
    {
        for (uint32_t i = 0; i < decoded; ++i) {
            licp_load_on_decoded(&l, 2, 1);
        }
        for (uint32_t i = 0; i < collisions; ++i) {
            licp_load_on_collision(&l);
        }
        licp_load_end_superframe(&l);
    }
    licp_load_config_t cfg;
    licp_load_t l;
};

TEST_F(Load, HighCollisionsThrottleBulkFirstNeverEmergency)
{
    double p1 = l.p[1], p2 = l.p[2];
    superframe(10, 10); /* 50% collisions */
    EXPECT_DOUBLE_EQ(l.p[3], 0.15);
    EXPECT_DOUBLE_EQ(l.p[2], p2);
    EXPECT_DOUBLE_EQ(l.p[1], p1);
    EXPECT_DOUBLE_EQ(l.p[0], 1.0);
    for (int i = 0; i < 10; ++i) {
        superframe(10, 10);
    }
    EXPECT_DOUBLE_EQ(l.p[3], cfg.p_min[3]);
    EXPECT_LT(l.p[2], p2); /* once bulk hits its floor, normal is throttled */
    EXPECT_DOUBLE_EQ(l.p[0], 1.0);
}

TEST_F(Load, LowCollisionsRelaxEveryClass)
{
    superframe(20, 0);
    EXPECT_DOUBLE_EQ(l.p[1], 0.9);
    EXPECT_DOUBLE_EQ(l.p[2], 0.6);
    EXPECT_NEAR(l.p[3], 0.4, 1e-12);
    for (int i = 0; i < 20; ++i) {
        superframe(20, 0);
    }
    EXPECT_DOUBLE_EQ(l.p[1], 1.0);
}

TEST_F(Load, TooFewSamplesChangeNothing)
{
    superframe(1, 2);
    EXPECT_DOUBLE_EQ(l.p[3], 0.3);
    EXPECT_NEAR(l.last_ratio, 2.0 / 3.0, 1e-9);
}

TEST_F(Load, HomeSlotFramesDoNotCountAsLoad)
{
    for (int i = 0; i < 50; ++i) {
        licp_load_on_decoded(&l, 2, 0);
    }
    licp_load_on_collision(&l);
    licp_load_end_superframe(&l);
    EXPECT_EQ(l.last.decoded_home, 50u);
    EXPECT_DOUBLE_EQ(l.p[3], 0.3); /* one event < min_samples */
}

TEST(LoadConfig, RejectsInconsistentBounds)
{
    licp_load_config_t c;
    licp_load_default_config(&c);
    c.p_init[2] = 0.01; /* below p_min[2] */
    licp_load_t l;
    EXPECT_EQ(licp_load_init(&l, &c), LICP_ERR_PARAM);
}
