# Testing guide {#testing}

This guide covers how to run the tests, the expected result of every test, the results recorded at delivery, and the known deviations from the design.

## 1. How to test

### 1.1 Unit and integration tests on a host computer

You need:

- Linux or macOS,
- CMake 3.16 or newer,
- GCC 9+ or Clang 10+,
- GoogleTest: the system package, `/usr/src/googletest`, or an automatic download.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
cd build && ctest --output-on-failure
```

**Expected:** `100% tests passed, 0 tests failed out of 112`.

To run one group of tests, or to see each test's name:

```sh
./build/tests/licp_tests --gtest_filter='Store.*'
./build/tests/licp_tests --gtest_brief=0
```

### 1.2 Memory and undefined-behaviour checks

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DLICP_SANITIZE=ON
cmake --build build-asan -j
./build-asan/tests/licp_tests
```

**Expected:**

- 112 tests pass;
- no `runtime error:` lines;
- no `AddressSanitizer` or `LeakSanitizer` report.

### 1.3 Tests as 64-bit Arm code (same instruction set as the ZSOM-F01)

On Debian or Ubuntu:

```sh
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu qemu-user libgtest-dev
cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-linux-gnu.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64;-L;/usr/aarch64-linux-gnu"
cmake --build build-arm -j
qemu-aarch64 -L /usr/aarch64-linux-gnu build-arm/tests/licp_tests
```

**Expected:** 112 tests pass.

### 1.4 Independent test vectors

The cryptographic and frame vectors in `tests/unit/test_frame.cpp`, and the airtimes in `tests/unit/test_region_airtime.cpp`, come from a separate Python implementation (`tools/gen_vectors.py`). It uses the `cryptography` package, not the C code.

To regenerate them:

```sh
pip install cryptography
python3 tools/gen_vectors.py
```

**Expected:**

- `public vector OK`;
- the project uplink bytes and airtimes identical to the values in the tests.

### 1.5 Service smoke test with simulated radios (bench)

```sh
cat > bench.conf <<'EOF'
store_path = /tmp/licp/frames.log
device_table_path = /tmp/licp/devices.bin
rx_backend = sim
tx_backend = sim
obc_tcp_port = 5600
EOF
mkdir -p /tmp/licp && ./build/licpd -c bench.conf --sim -v
```

Connect a test client to TCP port 5600 and send a STATUS_REQUEST message: bytes `A5 5A 06 <seq 2 bytes> 00 00 <CRC-16>`.

**Expected:**

- a STATUS reply (type `0x07`) with 19 telemetry pairs;
- `licpd` exits with code 0 on SIGTERM;
- the log shows "licpd running", "on-board computer connected", "stopping".

### 1.6 Tests still to run on the target hardware

These need the ZSOM-F01, the radio cards and the receiver. They are not covered by the automated suite.

| # | Test | Method | Pass criterion |
|---|---|---|---|
| H1 | Service starts on the ZSOM-F01 | Install `licpd` and the configuration, start with systemd | Runs 24 h; no restarts; memory stable |
| H2 | Beacon timing | Spectrum analyser or SDR on 856.5 MHz, navigation pulse-per-second as reference | Beacon starts within ±5 ms of `superframe start + 50 ms` |
| H3 | Downlink content | Reference device or SDR decoder | Beacon decodes and its integrity tag verifies with the group key |
| H4 | Chirp reception (demonstrations) | Lab device on channels 0–7 | Frames appear in FRAMES messages with correct metadata |
| H5 | Frequency-hopping reception | Receiver driver feeding `/dev/licp_fhss0` | Frames accepted; header-only events counted as collisions |
| H6 | Power-loss safety | Cut power during sustained reception, 50 times | On restart, no stored frame is lost except the one being written |
| H7 | Flash endurance | 100 000 frames written and released, with compaction | No input/output errors; file size bounded by `store_max_bytes` |
| H8 | Watchdog | Stop the service with SIGSTOP | Board resets within the watchdog timeout |
| H9 | On-board computer link | The satellite team's simulator | Plan upload, frame hand-over and release work end to end |

## 2. Expected result of every automated test

| Test | Expected result |
|---|---|
| **Common** | |
| Crc.Crc32CheckValue | CRC-32 of "123456789" = 0xCBF43926 |
| Crc.Crc32Incremental | Same value when computed in two parts |
| Crc.Crc16CcittFalseCheckValue | CRC-16/CCITT-FALSE of "123456789" = 0x29B1 |
| Bytes.LittleEndianRoundTrip | 16/24/32/64-bit values survive write and read; low byte first |
| Bytes.WriterStopsAtCapacity | Writer flags overflow and ignores later writes |
| Bytes.ReaderStopsAtEnd | Reader flags under-run and returns 0 |
| Status.NamesAreStable | Names returned for known and unknown codes |
| Log.SinkAndLevelFiltering | Debug suppressed at info level; info reaches the custom sink |
| **Crypto** | |
| Aes.Fips197AppendixC1 | Ciphertext 69c4e0d8…c55a (FIPS-197 C.1) |
| Aes.InPlaceEncryption | Same result when input and output share a buffer |
| Vectors/CmacRfc4493.OneShot/0–3 | Tags of RFC 4493 examples 1–4 (0, 16, 40, 64 bytes) |
| Vectors/CmacRfc4493.IncrementalOddChunks/0–3 | Same tags when the message is fed in uneven pieces |
| **Frames** | |
| Frame.ParsesPublicVector | Address 0x49BE7DF1, counter 2, port 1, 4-byte payload |
| Frame.VerifiesAndDecryptsPublicVector | Integrity code valid; payload decrypts to "test" |
| Frame.ProjectVectorNeedsFull32BitCounter | Fails with the 16-bit counter; passes with the rebuilt 0x00010005 |
| Frame.BuilderMatchesIndependentVector | C builder output equals the Python-generated frame byte for byte |
| Frame.TamperedFrameFailsIntegrityCheck | One flipped bit gives an integrity error |
| Frame.RejectsShortAndNonDataFrames | Short frame: length error. Join request or major ≠ 0: format error |
| Frame.RejectsFOptsLongerThanFrame | Length error |
| Frame.RejectsPortZeroWithFOpts | Format error |
| Frame.ParsesFOptsAndEmptyPort | MAC commands parsed; no port; integrity code valid |
| Frame.PayloadCryptIsItsOwnInverse | Encrypting twice restores the plain text |
| FrameCounter.RebuildCases | Normal step, repeat, roll-over and older-value cases give the documented counters |
| **Region and airtime** | |
| Region.DefaultsMatchNetworkNote | 15 channels 855.1–857.9 MHz; emergency channels 0 and 8 (856.7 MHz); downlink 856.5 MHz |
| Region.MapsShiftedFrequencyToChannel | ±12.4 kHz frequency shift still maps to the right channel; 855.8 MHz maps to none |
| Region.ValidationCatchesBadPlans | Bad emergency index, zero channels or spreading factor 6 rejected |
| Vectors/Airtime.MatchesIndependentFormula/0–4 | Time on air within 1 µs of the Python formula (for example SF12, 51 bytes = 2 465 792 µs) |
| AirtimeLimits.InvalidParametersGiveZero | 0 for invalid spreading factor, bandwidth or a NULL pointer |
| **Formats** | |
| Orbit.PacksToTwentySixBytesAndBack | 26 bytes; every field identical after unpack |
| Orbit.RejectsOutOfRangeFields | Inclination > 180°, eccentricity ≥ 1, angle ≥ 360° rejected |
| Orbit.DragTermKeepsThreeSignificantDigits | 3.4567e-5 encodes as 346 × 10^(−4−3) = 3.46e-5 |
| Bitmap.HundredDevicesFitInThirteenBytes | 100 bits extract to 13 bytes; first byte 0x49 |
| Beacon.RoundTripWithAllSections | 63-byte beacon; every field recovered |
| Beacon.MinimalBeaconIsTwentyTwoBytes | Header 3 + fixed 15 + tag 4 = 22 bytes |
| Beacon.WrongKeyOrTamperingIsRejected | Integrity error for a wrong key or a flipped bit |
| Beacon.TruncatesAcknowledgementsToFitAndReportsIt | At 40 bytes: 8 of 16 acknowledgements sent, orbit dropped, reported |
| Beacon.UnknownExtensionSectionsAreSkippedByDecoder | Extension type 0x90 skipped; other sections decoded |
| Beacon.ProbabilityConversion | 1.0 → 255, 0.5 → 128, 0 → 0 |
| AckMap.RoundTrip | Bits 0, 57, 99 recovered; 27-byte frame; tamper detected |
| **Core** | |
| DeviceTable.KeepsRecordsSortedAndFindsThem | Sorted by address; lookups by address and by short identifier |
| DeviceTable.RemoveAndCapacity | Full table refuses; removal frees a place |
| DeviceTable.UploadCannotRollCounterBack | An older uploaded counter is ignored; a newer one is taken |
| DeviceTable.SaveAndLoadRoundTrip | Keys, counters and flags restored from file |
| DeviceTable.CorruptedFileIsRejectedAndTableUnchanged | Checksum error; table in memory untouched; missing file gives an input/output error |
| Superframe.DefaultLengthIs45500ms | 3500 + 12 × 3500 = 45 500 ms |
| Superframe.LocatesBeaconWindowAndSlots | Correct superframe, window, slot, half and next-beacon time |
| Load.HighCollisionsThrottleBulkFirstNeverEmergency | Class 3 halves to its floor before class 2 moves; class 0 stays 1.0 |
| Load.LowCollisionsRelaxEveryClass | Classes 1–3 rise by 0.1 up to 1.0 |
| Load.TooFewSamplesChangeNothing | Fewer than 5 events: no change |
| Load.HomeSlotFramesDoNotCountAsLoad | Home-slot frames are counted separately from shared-slot load |
| LoadConfig.RejectsInconsistentBounds | Initial value below minimum rejected |
| Pipeline.AcceptsValidFrameAndUpdatesState | Stored as class 2; counter committed; bitmap bit set |
| Pipeline.FrameOutsideHomeSlotCountsAsSharedLoad | Counted as shared-slot traffic |
| Pipeline.DuplicateIsDropped | Second copy is a duplicate; one frame stored |
| Pipeline.UnknownAndBlockedDevicesAreDropped | Unknown and blocked outcomes; nothing stored |
| Pipeline.WrongKeyFailsIntegrityCheck | Integrity failure; counter not committed |
| Pipeline.CounterJumpBeyondGapIsDropped | A jump of exactly 16 384 is accepted; 16 385 is rejected |
| Pipeline.OldFrameReplayIsDropped | Older counter rejected (integrity failure after counter rebuild) |
| Pipeline.CounterRollsOverSixteenBits | 0xFFFE then 0x10001 accepted |
| Pipeline.EmergencyChannelGivesClassZeroAndQueuesAck | Class 0; one acknowledgement per device queued, taken and returned |
| Pipeline.HeaderOnlyEventCountsCollision | Collision outcome; counter incremented |
| Pipeline.UnknownFrequencyIsBadFormat | 860 MHz frame rejected |
| Pipeline.HooksSeeEveryOutcome | Hook called for accepted and duplicate frames with its user pointer |
| Downlink.BeaconAloneStartsAfterLead | Beacon at window start + 50 ms on 856.5 MHz, SF10 |
| Downlink.UnicastFollowsCallInSequence | Sent only after call-in and the device's uplink, after the beacon plus gap |
| Downlink.UnansweredUnicastIsDroppedAfterMaxCallIns | Dropped after 3 unanswered call-ins; counted |
| Downlink.BroadcastSentOnlyAfterAnnouncement | Sent only in the window whose beacon announced it |
| Downlink.OnlyOneAckMapIsKept | A newer bitmap replaces the older one |
| Downlink.WhatDoesNotFitWaitsForNextWindow | Everything planned ends within 3.5 s; leftovers stay queued and are announced again |
| Plan.AppliesEveryEntryType | Every handler called once; sequence 77 returned |
| Plan.CorruptedPlanChangesNothing | Checksum error; no handler called |
| Plan.RejectsWrongEntryLengthEvenWithGoodChecksum | 15-byte key entry rejected as a format error |
| Plan.RejectsBadMagicVersionAndTruncation | Format, length and checksum errors respectively |
| Plan.BuilderReportsOverflow | Builder returns 0 when the buffer is too small |
| **Storage** | |
| Store.ReadsBackWhatWasWritten | Metadata and payload identical; unknown sequence not found |
| Store.SelectsEmergencyFirstThenOldest | Order 3, 5, 2, 4, 1; in-flight frames not selected twice; requeue works |
| Store.ReleasedFramesStayReleasedAfterRestart | 2 of 4 pending after reopen; numbering continues at 5 |
| Store.RecoversFromTornWrite | 9 torn bytes discarded; file truncated to the last good record |
| Store.CorruptedRecordStopsRecoveryAtLastGoodRecord | Only records before the corruption kept |
| Store.EvictsLowerPriorityWhenFull | Same-priority frame refused; emergency frame evicts the oldest bulk frame |
| Store.FullOfEmergencyFramesRefusesMore | Store full error |
| Store.CompactionReclaimsReleasedSpace | File shrinks to the 2 live records (150 bytes); data still readable after reopen |
| Store.RejectsBadArguments | Parameter errors for priority 4, empty payload or a NULL path |
| **Input/output** | |
| Obc.EncodeDecodeRoundTrip | Type, sequence and payload recovered |
| Obc.ByteByByteWithGarbageAndTwoMessages | Both messages found despite leading and intervening noise |
| Obc.BadChecksumIsDroppedAndNextMessageStillDecodes | One checksum error counted; next message decoded |
| Obc.EncodeRejectsOversizedPayload | 0 bytes for payloads over 16 384 |
| Fhss.RecordRoundTrip | All fields recovered; bad magic rejected |
| Fhss.BackEndReadsRecordsFromDevice | 3 frames read; partial record kept for the next read; transmit unsupported |
| Config.DefaultsAreValid | Defaults pass validation |
| Config.SetsTypedValues | Numbers, keys, lists, booleans and signed values parsed |
| Config.RejectsBadValuesAndUnknownKeys | Out-of-range and malformed values rejected; unknown key reported |
| Config.LoadsFileAndReportsBadLine | Format error at line 5; earlier lines applied |
| Config.ExampleFileLoadsCleanly | `config/licp.conf.example` loads and validates |
| **Service (end to end)** | |
| Service.PlanUploadLoadsDevicesAndKey | 2 devices, group key, 1 orbit record, plan 501 |
| Service.CorruptPlanIsRejectedWithStatus | PLAN_RESULT carries the checksum error; no devices added |
| Service.FullPassFromReceptionToRelease | Beacon at superframe start + 50 ms with emergency acknowledgement and orbit; bitmap has bits 1 and 2; emergency frame handed over first; nothing pending after release |
| Service.ContactEndReturnsUnconfirmedFramesToPending | In-flight frames become pending again |
| Service.UnicastIsCalledInThenSentAfterDeviceAnswers | Call-in in one beacon; message sent in the next window after the device's uplink |
| Service.StateSurvivesRestart | Device counters and pending frames restored; replay after restart is a duplicate |
| Service.NewPassStartsAfterQuietGap | Pass 1 starts; bitmap cleared |
| Service.BeaconExtensionSectionIsBroadcast | Extension section 0x90 appears in the beacon |

## 3. Results recorded at delivery (14 October 2026)

| Platform | Build | Result |
|---|---|---|
| x86-64 Ubuntu 24.04, GCC 13.3, GoogleTest 1.14 | Debug, `-Werror` | 112 / 112 passed |
| x86-64, AddressSanitizer + UndefinedBehaviorSanitizer | Debug | 112 / 112 passed, no sanitizer reports |
| aarch64 (cross GCC 13, run under qemu-aarch64) | Release, `-mcpu=cortex-a53` | 112 / 112 passed |
| aarch64 Linux, native (Apple-silicon Linux VM), GCC 11.4, GoogleTest 1.14 | Release | 112 / 112 passed |
| x86-64 with `-DLICP_WITH_LIBLORAGW=ON` against sx1302_hal 2.1.0 | Release | Compiles and links (no hardware run) |
| `licpd --sim` smoke test over TCP | AddressSanitizer build | STATUS reply received; clean exit on SIGTERM; no leaks |

## 4. Deviations and open points

These are known differences from the design note, or items not yet verified.

| # | Area | Deviation | Impact | Action |
|---|---|---|---|---|
| D1 | SX1302 back-end | Compiled and linked against sx1302_hal 2.1.0, but not run on the radio cards. Chirp reception limited to channels 0–7. Time mapping uses the concentrator counter read together with the system clock (about 1 ms accuracy), not a pulse-per-second-latched reference | Beacon timing accuracy is unverified | Hardware tests H2–H4; move to the PPS-latched mapping (`lgw_gps_sync`) if better than 1 ms is needed |
| D2 | Frequency-hopping receiver | The 277-byte record format of `/dev/licp_fhss0` is a proposal. No driver exists yet; tested with file-backed records only | Interface may change | Agree the format with the programmable-logic team; change `licp_fhss_decode_record()` only |
| D3 | On-board computer link | TCP transport and message set are a working proposal; one connection at a time | Interface may change | Align with the interface control document |
| D4 | Keys at rest | `devices.bin` stores network keys unencrypted (permissions 0600) | Exposure if the flash is read | Encrypt with a payload key or use secure storage |
| D5 | First frame of a device | With no known counter, the upper 16 bits are taken as 0 | Devices past 65 535 frames need their counter in the plan | Central system always uploads the last known counter |
| D6 | Old-frame replays | Reported as integrity failures, not as replays (the rebuilt counter jumps a roll-over) | Telemetry labelling only | None, or add a separate replay counter |
| D7 | Acknowledgement bitmap | Sent in every beacon window where new bits were set, not only in the last window of a pass (the payload does not know where each footprint's pass ends). One frame covers 808 short identifiers at a 115-byte downlink limit | More downlink time than one bitmap per pass; networks over 808 devices per pass need chunk rotation | Add chunk rotation, or plan the bitmap window from orbit prediction |
| D8 | Pass detection | A new pass starts after 10 minutes without accepted frames, not from orbit prediction | Bitmap reset timing is approximate | Use pass predictions from the plan if needed |
| D9 | Drag term | 12-bit mantissa (3 significant digits) instead of the 5 digits of a two-line element set | Slightly lower multi-day prediction accuracy | Confirm with flight dynamics |
| D10 | Cryptography | Portable software AES, not constant-time; the Zynq hardware engines are not used | Acceptable for this threat model | Optionally use the Linux kernel crypto interface |
| D11 | Plan application | A plan is fully validated before it is applied. But if a handler fails midway (for example a full device table), earlier entries remain applied | Partial update in rare cases | The PLAN_RESULT error tells the ground to resend after fixing |
| D12 | Load-control constants | Thresholds (30% / 10%), factor 0.5 and step 0.1 are initial values | Throughput may not be optimal | Tune with receiver measurements (challenge 13 of the plan) |
| D13 | Timing source | The system clock is assumed to be disciplined to the navigation pulse-per-second (for example chrony). Leap seconds come from configuration | Wrong superframe alignment if the clock is not disciplined | Configure chrony with the PPS reference on the ZSOM-F01 |
| D14 | Duty cycle | No duty-cycle limit or listen-before-talk on downlinks | Correct for a satellite in a private band; confirm against the allocation | Confirm with the band allocation |
| D15 | Out of scope | Not included: central-system planning, device firmware, CCSDS framing, image update and memory scrubbing (operating-system level) | — | Covered by other work packages |
| D16 | Concurrency | Single-threaded. The FRAMES reply uses a static buffer | Not re-entrant; fine for the single main loop | Keep the service single-threaded |
