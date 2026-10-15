# Adding features {#adding_features}

The code has six extension points. Each needs changes in one or two places only.

## 1. A new module

1. Create `include/licp/<module>/licp_<name>.h` and `src/<module>/licp_<name>.c`.
2. Document the header with `@file`, `@brief`, `@defgroup` (or `@addtogroup`), and `@param` / `@return` on every function.
3. Return ::licp_status_t from anything that can fail, and prefix every public symbol with `licp_`.
4. Re-run CMake. Sources are collected automatically (`file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`).
5. Add `tests/unit/test_<name>.cpp`. It is collected automatically too.
6. Add a row to the test table and, if needed, to the deviations in `docs/testing.md`.

## 2. React to every received frame (pipeline hook)

Use this, for example, for per-device statistics, a geofence alarm or extra telemetry.

```c
static void my_hook(licp_rx_outcome_t o, const licp_frame_t *f, const licp_device_t *d,
                    const licp_rx_frame_t *rx, void *user)
{
    if (o == LICP_RX_ACCEPTED) { /* ... */ }
}
...
licp_pipeline_add_hook(licp_service_pipeline(&svc), my_hook, &my_state);
```

Up to ::LICP_PIPELINE_MAX_HOOKS hooks can be registered. Hooks run in the main loop, so keep them short.

## 3. Add information to the beacon (extension section)

1. Pick a section type of `0x80` or above.
2. Fill a ::licp_beacon_extra_t and call ::licp_service_set_beacon_extra.

Devices that do not know the type skip it, because ::licp_beacon_decode ignores unknown sections. Extension sections are added only if they fit after the standard sections.

To make a section part of the standard set:

- add its type to ::licp_beacon_section_type_t;
- encode it in `licp_beacon_encode()` at the right priority;
- decode it in `licp_beacon_decode()`;
- bump ::LICP_PFRAME_VERSION only if old devices could misread it.

## 4. Add an entry to the upload plan

For a project-specific entry:

1. Use a type of `0x80` or above.
2. Register a handler with ::licp_service_set_plan_extension.

For a standard entry:

1. Add the type to ::licp_plan_type_t.
2. Add its length rule in `check_entry()` (`src/core/licp_plan.c`).
3. Add a callback to ::licp_plan_handlers_t and dispatch it in `licp_plan_apply()`.
4. Implement the handler in `src/app/licp_service.c`.

## 5. A new radio back-end

1. Implement the five operations of ::licp_radio_ops_t: `start`, `receive`, `transmit`, `stats`, `stop`.
2. Use `src/hal/licp_radio_fhss.c` (receive only) or `src/hal/licp_radio_sim.c` as a template.
3. Add an initialiser `licp_radio_<name>_init(...)` that fills a ::licp_radio_t.
4. Select it in `app/main.c` from a new value of `rx_backend` or `tx_backend`.

If it needs an external library, guard it with a CMake option, as `LICP_WITH_LIBLORAGW` does.

## 6. A new configuration key

1. Add the field to ::licp_config_t.
2. Set its default in ::licp_config_default.
3. Add one `strcmp` line in ::licp_config_set.
4. Document it in `config/licp.conf.example`.

The test `Config.ExampleFileLoadsCleanly` fails if the example file contains a key the parser does not know.

## 7. New telemetry

1. Add an identifier at the end of ::licp_tm_id_t. Never renumber existing identifiers.
2. Add one `tm_put()` line in `build_status()` (`src/app/licp_service.c`).

Ground tools must ignore identifiers they do not know.

## Coding rules used throughout

- C11, no compiler extensions except `__typeof__` in the configuration macros.
- Builds clean with `-Wall -Wextra -Wshadow -Wconversion -Werror`.
- No dynamic allocation in the per-frame path. Allocation happens only at start-up and for the store index.
- Every multi-byte field on the wire is little-endian, through `licp_put_*` / `licp_get_*`.
- Secrets are wiped after use (`licp_aes_wipe`, device removal, shutdown).
