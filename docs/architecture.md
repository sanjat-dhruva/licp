# Architecture {#architecture}

## 1. Where the service sits

```
  Devices ──(frequency-hopping uplink)──▶ [ programmable-logic receiver ] ──/dev/licp_fhss0──┐
                                                                                             ▼
                                     ┌───────────────────── licpd (Linux, ZSOM-F01) ───────────────────────┐
                                     │  radio back-ends ─▶ pipeline ─▶ store ◀─▶ OBC link ◀─TCP─▶ on-board computer ─▶ ground
                                     │                         │                  ▲                             │
                                     │                         ▼                  │ plan upload                 │
                                     │                  load control         device table, orbit, keys          │
                                     │                         │                                                │
                                     │  superframe timer ─▶ beacon + downlink planner ─▶ SX1302 card (chirp downlink)
                                     └─────────────────────────────────────────────────────────────────────┘
```

The service is a single process with a single thread. `main.c` runs a loop every 10 milliseconds:

1. Accept or serve the on-board computer connection.
2. Call `licp_service_poll()`.
3. Kick the watchdog.

There are no locks. Timing that needs microsecond accuracy (transmit start times) is handled by the concentrator hardware, which is given an absolute time.

## 2. Modules

| Module | Folder | Responsibility | Depends on |
|---|---|---|---|
| common | `common/` | Status codes, logger, little-endian packing, CRC-32 and CRC-16 | — |
| crypto | `crypto/` | AES-128 (FIPS-197), AES-CMAC (RFC 4493) | common |
| lorawan | `lorawan/` | Frame parse, integrity code, counter rebuild, payload encryption, chirp time on air | crypto |
| region | `region/` | 855–858 MHz radio plan, emergency channels, downlink parameters | lorawan |
| proto | `proto/` | 26-byte orbit record, acknowledgement bitmap, beacon and bitmap frames | crypto |
| core | `core/` | Device table, superframe timing, load control, frame pipeline, downlink queue and planner, upload plan | all above |
| storage | `storage/` | Checksummed, power-safe store-and-forward log | common |
| hal | `hal/` | Radio back-ends (simulator, frequency-hopping receiver, SX1302), clock, watchdog | core |
| obc | `obc/` | Framing and streaming decoder for the on-board computer link | common |
| app | `app/` | Configuration, service orchestration | everything |

Headers live in `include/licp/<module>/`, sources in `src/<module>/`. Every public symbol starts with `licp_`, so modules can be linked together without name clashes.

## 3. Frame path (pipeline)

`licp_pipeline_process()` handles each frame in this order:

1. A "header only" event is counted as a collision; processing stops.
2. The frame is parsed. Anything that is not an uplink data frame on a known channel is dropped (`bad-format`).
3. The device address is looked up. The frame is dropped if the device is unknown or blocked.
4. The 32-bit counter is rebuilt, then the integrity code is verified with the device's network key (`bad-mic` on failure).
5. A counter equal to the last accepted one is a `duplicate`. A jump larger than `max_fcnt_gap` is `bad-counter`.
6. The frame is stored. The emergency channels give priority class 0; otherwise the device's class (1–3) is used.
7. On success:
    - the counter is committed,
    - the device's bit is set in the acknowledgement bitmap,
    - an emergency acknowledgement is queued (class 0 only),
    - the load counters are updated (home slot or shared).
8. Registered hooks are called with the outcome.

## 4. Beacon window

`licp_service_poll()` plans each window `plan_ahead_ms` (default 500 ms) before it opens:

1. Close the load-control period, which updates the access probabilities.
2. Queue the acknowledgement bitmap if new bits were set.
3. Build the beacon:
    - time and access probabilities,
    - emergency acknowledgements,
    - call-ins,
    - broadcast notices,
    - the next orbit record,
    - extension sections.

    Sections that do not fit are carried over to the next beacon.
4. Plan transmissions inside the 3.5-second window, in this order:
    1. the beacon,
    2. one-to-one messages for devices that answered a call-in,
    3. the broadcasts just announced,
    4. the bitmap.
5. Hand each transmission, with its absolute start time, to the transmit back-end.

## 5. Formats (summary)

| Format | Defined in | Notes |
|---|---|---|
| Beacon | `proto/licp_beacon.h` | Proprietary frame `0xE0`, kind 1, version 1, sections as type-length-value, 4-byte CMAC |
| Acknowledgement bitmap | `proto/licp_beacon.h` | Proprietary frame, kind 2, one bit per short identifier |
| Orbit record | `proto/licp_orbit.h` | 26 bytes as in the network note |
| Upload plan | `core/licp_plan.h` | `LPLN`, version 1, type-length-value entries, CRC-32 |
| On-board computer messages | `obc/licp_obc.h` | Sync `A5 5A`, type, sequence, length, payload, CRC-16 |
| Store log record | `storage/licp_store.h` | Magic `0x4C53`, type, length, sequence, data, CRC-32 |
| Device table file | `core/licp_device_table.c` | `LDEV`, version 1, 33-byte records, CRC-32 |
| Receiver record | `hal/licp_radio_fhss.h` | Fixed 277-byte records from `/dev/licp_fhss0` |

Every format carries a version or magic number. Every reader skips unknown type-length-value entries, so new fields can be added without breaking deployed devices.

## 6. Files on the payload

| File | Default path | Written |
|---|---|---|
| Frame log | `/var/lib/licp/frames.log` | Every accepted frame (fsync by default) |
| Device table | `/var/lib/licp/devices.bin` | After each plan, at each new pass, at shutdown (atomic rename) |
| Configuration | `/etc/licp/licp.conf` | Read at start |

## 7. Security notes

- Device network keys arrive only in the upload plan. They are wiped from memory when removed.
- Beacons and bitmaps are signed with the group key. Devices must reject unsigned or wrongly signed beacons.
- The payload never decrypts application data. It stores frames as received.
- Keys at rest in `devices.bin` are not encrypted (see the deviations in testing.md).
