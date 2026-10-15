/**
 * @file main.c
 * @brief licpd: the LICP on-board service for Linux on the ZSOM-F01.
 *
 * Usage:
 * @verbatim
 *  licpd -c /etc/licp/licp.conf     normal operation
 *  licpd -c licp.conf --sim         simulated radios (bench and ground tests)
 *  licpd --help
 * @endverbatim
 *
 * The main loop polls the service every 10 milliseconds, serves one
 * on-board computer connection over TCP, kicks the watchdog and exits
 * cleanly on SIGINT or SIGTERM.
 *
 * @ingroup app
 */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "licp/app/licp_config.h"
#include "licp/app/licp_service.h"
#include "licp/common/licp_log.h"
#include "licp/hal/licp_clock.h"
#include "licp/hal/licp_radio_fhss.h"
#include "licp/hal/licp_radio_sim.h"
#include "licp/hal/licp_watchdog.h"
#include "licp/obc/licp_obc.h"

#ifdef LICP_WITH_LIBLORAGW
#include "licp/hal/licp_radio_sx1302.h"
#endif

/** Module tag used in log messages. */
static const char *TAG = "main";
/** Set by the signal handler to end the main loop. */
static volatile sig_atomic_t g_stop = 0;

/** @brief SIGINT/SIGTERM handler: request a clean stop. @param sig Signal number (unused). */
static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/** @brief Print command-line help. */
static void usage(void)
{
    (void)printf("usage: licpd [-c config] [--sim] [-v]\n"
                 "  -c FILE   configuration file (default /etc/licp/licp.conf)\n"
                 "  --sim     use simulated radios instead of hardware\n"
                 "  -v        debug logging\n");
}

/** Non-blocking TCP listener on all interfaces. */
static int open_listener(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(fd, 1) != 0) {
        (void)close(fd);
        return -1;
    }
    (void)fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}

/**
 * @brief Write all bytes, retrying on EINTR and short writes.
 * @return 0 on success, -1 on error.
 */
static int write_all(int fd, const uint8_t *p, size_t n)
{
    while (n > 0u) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += (size_t)w;
        n -= (size_t)w;
    }
    return 0;
}

/**
 * @brief Program entry: load configuration, start the service, run the main loop.
 * @param argc Argument count.
 * @param argv Arguments (see usage()).
 * @return 0 on clean exit, 1 on start-up failure, 2 on bad arguments.
 */
int main(int argc, char **argv)
{
    const char *cfg_path = "/etc/licp/licp.conf";
    int sim = 0;
    int verbose = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cfg_path = argv[++i];
        } else if (strcmp(argv[i], "--sim") == 0) {
            sim = 1;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else {
            usage();
            return (strcmp(argv[i], "--help") == 0) ? 0 : 2;
        }
    }

    static licp_config_t cfg;
    licp_config_default(&cfg);
    int bad_line = 0;
    licp_status_t st = licp_config_load(&cfg, cfg_path, &bad_line);
    if (st == LICP_ERR_IO) {
        LICP_LOGW(TAG, "no configuration at %s, using defaults", cfg_path);
    } else if (st != LICP_OK) {
        LICP_LOGE(TAG, "%s line %d: %s", cfg_path, bad_line, licp_status_str(st));
        return 1;
    }
    if (verbose) {
        cfg.log_level = LICP_LOG_DEBUG;
    }

    licp_clock_system_t sysclk;
    licp_clock_t clock;
    licp_clock_system_init(&sysclk, cfg.leap_seconds, &clock);

    static licp_service_t svc;
    st = licp_service_init(&svc, &cfg, &clock);
    if (st != LICP_OK) {
        LICP_LOGE(TAG, "service init failed: %s", licp_status_str(st));
        return 1;
    }

    /* Radios. */
    static licp_radio_sim_t simradio;
    static licp_radio_fhss_t fhss;
    licp_radio_t radio;
    if (sim || strcmp(cfg.rx_backend, "sim") == 0) {
        licp_radio_sim_init(&simradio, &radio);
        (void)licp_service_add_rx(&svc, &radio);
        licp_service_set_tx(&svc, &radio);
    } else {
        if (strcmp(cfg.rx_backend, "fhss") == 0) {
            if (licp_radio_fhss_init(&fhss, cfg.fhss_device, &radio) == LICP_OK) {
                (void)licp_service_add_rx(&svc, &radio);
            }
        }
#ifdef LICP_WITH_LIBLORAGW
        static licp_radio_sx1302_t sx;
        if (strcmp(cfg.tx_backend, "sx1302") == 0 || strcmp(cfg.rx_backend, "sx1302") == 0) {
            licp_radio_t sxr;
            if (licp_radio_sx1302_init(&sx, cfg.sx1302_spi, &cfg.region, &clock, &sxr) == LICP_OK) {
                if (strcmp(cfg.rx_backend, "sx1302") == 0) {
                    (void)licp_service_add_rx(&svc, &sxr);
                }
                licp_service_set_tx(&svc, &sxr);
            }
        }
#else
        if (strcmp(cfg.tx_backend, "sx1302") == 0) {
            LICP_LOGW(TAG, "built without libloragw: no transmitter (rebuild with -DLICP_WITH_LIBLORAGW=ON)");
        }
#endif
    }
    st = licp_service_start(&svc);
    if (st != LICP_OK) {
        LICP_LOGE(TAG, "radio start failed: %s", licp_status_str(st));
        licp_service_shutdown(&svc);
        return 1;
    }

    licp_watchdog_t wd;
    if (licp_watchdog_open(&wd, cfg.watchdog_path) != LICP_OK) {
        LICP_LOGW(TAG, "watchdog %s not available", cfg.watchdog_path);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);
    (void)signal(SIGPIPE, SIG_IGN);

    int lfd = (cfg.obc_tcp_port != 0u) ? open_listener(cfg.obc_tcp_port) : -1;
    if (cfg.obc_tcp_port != 0u && lfd < 0) {
        LICP_LOGW(TAG, "cannot listen on port %u", (unsigned)cfg.obc_tcp_port);
    }
    int cfd = -1;
    static licp_obc_decoder_t dec;
    licp_obc_decoder_init(&dec);
    static uint8_t reply[LICP_OBC_HDR_LEN + LICP_OBC_MAX_PAYLOAD + LICP_OBC_CRC_LEN];

    LICP_LOGI(TAG, "licpd running (%s radios)", sim ? "simulated" : "hardware");
    while (!g_stop) {
        struct pollfd pfd[2];
        nfds_t nfds = 0;
        if (lfd >= 0 && cfd < 0) {
            pfd[nfds].fd = lfd;
            pfd[nfds].events = POLLIN;
            nfds++;
        }
        if (cfd >= 0) {
            pfd[nfds].fd = cfd;
            pfd[nfds].events = POLLIN;
            nfds++;
        }
        (void)poll(pfd, nfds, 10);

        if (lfd >= 0 && cfd < 0) {
            cfd = accept(lfd, NULL, NULL);
            if (cfd >= 0) {
                licp_obc_decoder_init(&dec);
                LICP_LOGI(TAG, "on-board computer connected");
            }
        }
        if (cfd >= 0) {
            uint8_t in[2048];
            ssize_t r = recv(cfd, in, sizeof(in), MSG_DONTWAIT);
            if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                (void)close(cfd);
                cfd = -1;
                licp_store_requeue_in_flight(svc.store);
                LICP_LOGI(TAG, "on-board computer disconnected");
            } else if (r > 0) {
                size_t off = 0;
                while (off < (size_t)r) {
                    size_t used = 0;
                    licp_obc_msg_t msg;
                    bool got = licp_obc_decoder_feed(&dec, in + off, (size_t)r - off, &used, &msg);
                    off += used;
                    if (got) {
                        size_t n = licp_service_handle_obc(&svc, &msg, reply, sizeof(reply));
                        if (n > 0u && write_all(cfd, reply, n) != 0) {
                            LICP_LOGW(TAG, "reply write failed");
                        }
                    } else if (used == 0u) {
                        break;
                    }
                }
            }
        }

        licp_service_poll(&svc);
        licp_watchdog_kick(&wd);
    }

    LICP_LOGI(TAG, "stopping");
    if (cfd >= 0) {
        (void)close(cfd);
    }
    if (lfd >= 0) {
        (void)close(lfd);
    }
    licp_service_shutdown(&svc);
    licp_watchdog_close(&wd);
    return 0;
}
