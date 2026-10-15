/**
 * @file test_common.cpp
 * @brief Tests for checksums, byte helpers, status names and logging.
 */
#include <gtest/gtest.h>

#include <string>

extern "C" {
#include "licp/common/licp_bytes.h"
#include "licp/common/licp_crc.h"
#include "licp/common/licp_log.h"
#include "licp/common/licp_status.h"
}

TEST(Crc, Crc32CheckValue)
{
    EXPECT_EQ(licp_crc32(0, "123456789", 9), 0xCBF43926u);
}

TEST(Crc, Crc32Incremental)
{
    uint32_t c = licp_crc32(0, "12345", 5);
    c = licp_crc32(c, "6789", 4);
    EXPECT_EQ(c, 0xCBF43926u);
}

TEST(Crc, Crc16CcittFalseCheckValue)
{
    EXPECT_EQ(licp_crc16_ccitt("123456789", 9), 0x29B1u);
}

TEST(Bytes, LittleEndianRoundTrip)
{
    uint8_t b[8];
    licp_put_u16(b, 0xBEEF);
    EXPECT_EQ(b[0], 0xEF);
    EXPECT_EQ(licp_get_u16(b), 0xBEEFu);
    licp_put_u24(b, 0x123456);
    EXPECT_EQ(licp_get_u24(b), 0x123456u);
    licp_put_u32(b, 0xDEADBEEFu);
    EXPECT_EQ(licp_get_u32(b), 0xDEADBEEFu);
    licp_put_u64(b, 0x0102030405060708ull);
    EXPECT_EQ(b[0], 0x08);
    EXPECT_EQ(licp_get_u64(b), 0x0102030405060708ull);
}

TEST(Bytes, WriterStopsAtCapacity)
{
    uint8_t b[5];
    licp_writer_t w;
    licp_writer_init(&w, b, sizeof(b));
    licp_writer_u32(&w, 1);
    EXPECT_TRUE(w.ok);
    licp_writer_u16(&w, 2); /* needs 2, only 1 left */
    EXPECT_FALSE(w.ok);
    licp_writer_u8(&w, 3); /* ignored after overflow */
    EXPECT_EQ(w.len, 4u);
}

TEST(Bytes, ReaderStopsAtEnd)
{
    const uint8_t b[3] = {1, 2, 3};
    licp_reader_t r;
    licp_reader_init(&r, b, sizeof(b));
    EXPECT_EQ(licp_reader_u16(&r), 0x0201u);
    EXPECT_EQ(licp_reader_left(&r), 1u);
    EXPECT_EQ(licp_reader_u16(&r), 0u);
    EXPECT_FALSE(r.ok);
}

TEST(Status, NamesAreStable)
{
    EXPECT_STREQ(licp_status_str(LICP_OK), "LICP_OK");
    EXPECT_STREQ(licp_status_str(LICP_ERR_MIC), "LICP_ERR_MIC");
    EXPECT_STREQ(licp_status_str((licp_status_t)-14), "LICP_ERR_UNKNOWN");
}

namespace {
std::string g_last;
void capture(licp_log_level_t, const char *module, const char *msg, void *)
{
    g_last = std::string(module) + ":" + msg;
}
} // namespace

TEST(Log, SinkAndLevelFiltering)
{
    licp_log_set_sink(capture, nullptr);
    licp_log_set_level(LICP_LOG_INFO);
    g_last.clear();
    LICP_LOGD("t", "hidden %d", 1);
    EXPECT_TRUE(g_last.empty());
    LICP_LOGI("t", "shown %d", 2);
    EXPECT_EQ(g_last, "t:shown 2");
    licp_log_set_sink(nullptr, nullptr);
}
