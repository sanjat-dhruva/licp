/**
 * @file test_store.cpp
 * @brief Store-and-forward: ordering, release, power-loss recovery, eviction, compaction.
 */
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include <sys/stat.h>

#include "test_util.hpp"

extern "C" {
#include "licp/storage/licp_store.h"
}

using licp_test::TempDir;

namespace {
licp_store_meta_t meta(uint32_t addr, uint8_t prio)
{
    licp_store_meta_t m;
    std::memset(&m, 0, sizeof(m));
    m.gnss_ms = 1000u + addr;
    m.freq_hz = 855300000u;
    m.channel = 1;
    m.dev_addr = addr;
    m.fcnt32 = addr * 10u;
    m.priority = prio;
    m.rssi_x10 = -1234;
    m.snr_x10 = -75;
    return m;
}

std::vector<uint8_t> payload(uint8_t seed, size_t n = 33)
{
    std::vector<uint8_t> p(n);
    for (size_t i = 0; i < n; ++i) {
        p[i] = static_cast<uint8_t>(seed + i);
    }
    return p;
}

off_t file_size(const std::string &p)
{
    struct stat sb;
    return (stat(p.c_str(), &sb) == 0) ? sb.st_size : -1;
}
} // namespace

class Store : public ::testing::Test {
protected:
    void open_store(uint64_t max = 1024 * 1024)
    {
        path = dir.file("frames.log");
        licp_store_config_t c = {path.c_str(), max, true};
        ASSERT_EQ(licp_store_open(&s, &c), LICP_OK);
    }
    void reopen(uint64_t max = 1024 * 1024)
    {
        licp_store_close(s);
        s = nullptr;
        open_store(max);
    }
    void TearDown() override { licp_store_close(s); }
    void add(uint32_t addr, uint8_t prio, uint32_t *seq = nullptr, licp_status_t expect = LICP_OK)
    {
        licp_store_meta_t m = meta(addr, prio);
        auto p = payload(static_cast<uint8_t>(addr));
        EXPECT_EQ(licp_store_append(s, &m, p.data(), p.size(), seq), expect);
    }
    TempDir dir;
    std::string path;
    licp_store_t *s = nullptr;
};

TEST_F(Store, ReadsBackWhatWasWritten)
{
    open_store();
    uint32_t seq = 0;
    add(7, 2, &seq);
    EXPECT_EQ(seq, 1u);
    licp_store_meta_t m;
    uint8_t phy[255];
    size_t len = 0;
    ASSERT_EQ(licp_store_read(s, seq, &m, phy, sizeof(phy), &len), LICP_OK);
    EXPECT_EQ(len, 33u);
    EXPECT_EQ(m.dev_addr, 7u);
    EXPECT_EQ(m.fcnt32, 70u);
    EXPECT_EQ(m.rssi_x10, -1234);
    EXPECT_EQ(m.snr_x10, -75);
    EXPECT_EQ(phy[0], 7u);
    EXPECT_EQ(licp_store_read(s, 99, &m, phy, sizeof(phy), &len), LICP_ERR_NOT_FOUND);
}

TEST_F(Store, SelectsEmergencyFirstThenOldest)
{
    open_store();
    add(1, 3);
    add(2, 2);
    add(3, 0);
    add(4, 2);
    add(5, 0);
    uint32_t seqs[10];
    size_t n = licp_store_select(s, seqs, 10);
    ASSERT_EQ(n, 5u);
    const uint32_t expect[5] = {3, 5, 2, 4, 1};
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(seqs[i], expect[i]) << "position " << i;
    }
    EXPECT_EQ(licp_store_select(s, seqs, 10), 0u); /* all in flight now */
    licp_store_requeue_in_flight(s);
    EXPECT_EQ(licp_store_select(s, seqs, 2), 2u);
}

TEST_F(Store, ReleasedFramesStayReleasedAfterRestart)
{
    open_store();
    for (uint32_t i = 1; i <= 4; ++i) {
        add(i, 2);
    }
    const uint32_t rel[2] = {1, 3};
    ASSERT_EQ(licp_store_release(s, rel, 2), LICP_OK);
    reopen();
    licp_store_stats_t st;
    licp_store_get_stats(s, &st);
    EXPECT_EQ(st.pending, 2u);
    uint32_t seqs[4];
    ASSERT_EQ(licp_store_select(s, seqs, 4), 2u);
    EXPECT_EQ(seqs[0], 2u);
    EXPECT_EQ(seqs[1], 4u);
    uint32_t seq = 0;
    add(9, 2, &seq);
    EXPECT_EQ(seq, 5u); /* numbering continues after restart */
}

TEST_F(Store, RecoversFromTornWrite)
{
    open_store();
    add(1, 2);
    add(2, 2);
    off_t good = file_size(path);
    /* Simulate power loss in the middle of a third record. */
    FILE *f = std::fopen(path.c_str(), "ab");
    ASSERT_NE(f, nullptr);
    const uint8_t torn[9] = {0x53, 0x4C, 0x01, 0x00, 0x30, 0x00, 0x03, 0x00, 0x00};
    std::fwrite(torn, 1, sizeof(torn), f);
    std::fclose(f);
    reopen();
    licp_store_stats_t st;
    licp_store_get_stats(s, &st);
    EXPECT_EQ(st.pending, 2u);
    EXPECT_EQ(st.truncated_bytes, 9u);
    EXPECT_EQ(file_size(path), good);
    uint32_t seq = 0;
    add(3, 2, &seq);
    EXPECT_EQ(seq, 3u);
}

TEST_F(Store, CorruptedRecordStopsRecoveryAtLastGoodRecord)
{
    open_store();
    add(1, 2);
    off_t after_first = file_size(path);
    add(2, 2);
    add(3, 2);
    FILE *f = std::fopen(path.c_str(), "r+b");
    ASSERT_NE(f, nullptr);
    std::fseek(f, after_first + 20, SEEK_SET);
    std::fputc(0xFF, f);
    std::fclose(f);
    reopen();
    licp_store_stats_t st;
    licp_store_get_stats(s, &st);
    EXPECT_EQ(st.pending, 1u);
}

TEST_F(Store, EvictsLowerPriorityWhenFull)
{
    open_store(1024); /* each record is 10+28+33+4 = 75 bytes: 13 fit */
    for (uint32_t i = 1; i <= 13; ++i) {
        add(i, 3);
    }
    add(100, 3, nullptr, LICP_ERR_FULL); /* same priority: refused */
    add(200, 0);                          /* emergency: evicts the oldest bulk frame */
    licp_store_stats_t st;
    licp_store_get_stats(s, &st);
    EXPECT_EQ(st.evicted, 1u);
    EXPECT_EQ(st.rejected_full, 1u);
    EXPECT_EQ(st.pending_by_class[0], 1u);
    EXPECT_LE(st.file_bytes, 1024u);
    licp_store_meta_t m;
    uint8_t phy[255];
    size_t len;
    EXPECT_EQ(licp_store_read(s, 1, &m, phy, sizeof(phy), &len), LICP_ERR_NOT_FOUND);
    EXPECT_EQ(licp_store_read(s, 2, &m, phy, sizeof(phy), &len), LICP_OK);
}

TEST_F(Store, FullOfEmergencyFramesRefusesMore)
{
    open_store(1024);
    for (uint32_t i = 1; i <= 13; ++i) {
        add(i, 0);
    }
    add(50, 0, nullptr, LICP_ERR_FULL);
}

TEST_F(Store, CompactionReclaimsReleasedSpace)
{
    open_store();
    for (uint32_t i = 1; i <= 10; ++i) {
        add(i, 2);
    }
    std::vector<uint32_t> rel = {1, 2, 3, 4, 5, 6, 7, 8};
    ASSERT_EQ(licp_store_release(s, rel.data(), rel.size()), LICP_OK);
    off_t before = file_size(path);
    ASSERT_EQ(licp_store_compact(s), LICP_OK);
    off_t after = file_size(path);
    EXPECT_EQ(after, 2 * 75);
    EXPECT_LT(after, before);
    licp_store_meta_t m;
    uint8_t phy[255];
    size_t len;
    ASSERT_EQ(licp_store_read(s, 10, &m, phy, sizeof(phy), &len), LICP_OK);
    EXPECT_EQ(m.dev_addr, 10u);
    reopen();
    licp_store_stats_t st;
    licp_store_get_stats(s, &st);
    EXPECT_EQ(st.pending, 2u);
}

TEST_F(Store, RejectsBadArguments)
{
    open_store();
    licp_store_meta_t m = meta(1, 4);
    uint8_t p[1] = {0};
    EXPECT_EQ(licp_store_append(s, &m, p, 1, nullptr), LICP_ERR_PARAM);
    m.priority = 1;
    EXPECT_EQ(licp_store_append(s, &m, p, 0, nullptr), LICP_ERR_PARAM);
    licp_store_config_t c = {nullptr, 4096, false};
    licp_store_t *x = nullptr;
    EXPECT_EQ(licp_store_open(&x, &c), LICP_ERR_PARAM);
}
