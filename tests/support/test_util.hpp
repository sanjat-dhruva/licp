/**
 * @file test_util.hpp
 * @brief Shared helpers for the Google Test suite.
 */
#ifndef LICP_TEST_UTIL_HPP
#define LICP_TEST_UTIL_HPP

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

extern "C" {
#include "licp/core/licp_device_table.h"
#include "licp/core/licp_pipeline.h"
#include "licp/lorawan/licp_frame.h"
}

namespace licp_test {

/** Temporary directory removed (with its files) at scope exit. */
class TempDir {
public:
    TempDir()
    {
        char tmpl[] = "/tmp/licp_test_XXXXXX";
        char *p = mkdtemp(tmpl);
        path_ = p ? p : "/tmp";
    }
    ~TempDir()
    {
        std::string cmd = "rm -rf '" + path_ + "'";
        if (std::system(cmd.c_str()) != 0) {
            /* best effort */
        }
    }
    std::string file(const std::string &name) const { return path_ + "/" + name; }
    const std::string &path() const { return path_; }

private:
    std::string path_;
};

/** Network key used by test devices. */
inline const uint8_t *nwk_key()
{
    static const uint8_t k[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    return k;
}

/** Application key used by test devices. */
inline const uint8_t *app_key()
{
    static const uint8_t k[16] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                  0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    return k;
}

/** A device record using ::nwk_key. */
inline licp_device_t make_device(uint32_t addr, uint16_t short_id, uint8_t prio = 2, uint8_t ch = 3,
                                 uint8_t slot = 4)
{
    licp_device_t d;
    std::memset(&d, 0, sizeof(d));
    d.dev_addr = addr;
    std::memcpy(d.nwk_s_key, nwk_key(), 16);
    d.home_channel = ch;
    d.home_slot = slot;
    d.priority = prio;
    d.short_id = short_id;
    return d;
}

/** Build an unconfirmed uplink with a 12-byte payload on port 1. */
inline std::vector<uint8_t> make_uplink(uint32_t addr, uint32_t fcnt32, const uint8_t *key = nullptr)
{
    const uint8_t plain[12] = {'r', 'e', 'a', 'd', 'i', 'n', 'g', '-', '0', '0', '0', '1'};
    uint8_t buf[64];
    size_t len = 0;
    licp_status_t st = licp_frame_build_data(LICP_MTYPE_UNCONF_DATA_UP, addr, 0x00, fcnt32, nullptr, 0, 1, true,
                                             plain, sizeof(plain), key ? key : nwk_key(), app_key(), buf,
                                             sizeof(buf), &len);
    if (st != LICP_OK) {
        return {};
    }
    return std::vector<uint8_t>(buf, buf + len);
}

/** Wrap a frame in a reception record. */
inline licp_rx_frame_t make_rx(const std::vector<uint8_t> &phy, uint64_t t_ms, uint32_t freq_hz, uint8_t flags = 0)
{
    licp_rx_frame_t rx;
    std::memset(&rx, 0, sizeof(rx));
    if (!phy.empty()) {
        std::memcpy(rx.phy, phy.data(), phy.size());
    }
    rx.len = phy.size();
    rx.gnss_ms = t_ms;
    rx.freq_hz = freq_hz;
    rx.datarate = 0;
    rx.rssi_x10 = -1250;
    rx.snr_x10 = -105;
    rx.flags = flags;
    return rx;
}

} // namespace licp_test

#endif
