# Thread scheduling and logging — staged migration

## Current implementation
- `scheduler.h`: opt-in OS policy selection; scheduling refusal does not abort a managed worker.
- `subproc_spawn_fds_profile`: applies a chosen policy in the forked child prior to exec.
- `log-queue.h`: bounded queue of already-formatted fixed-size messages. Producers use a try-lock and drop on contention/full capacity; drain invokes existing `Log()` callbacks on its owning thread. `ra_log_queue_dropped()` reports lost records.

## Constraints / next stages
- Do not call existing `Log()` from hard RT callbacks. It allocates, locks indirectly through callbacks and performs I/O.
- The queue does not yet handle timestamps at ingestion, callback reentrancy, drop summaries, lifetime coordination, or independent logger thread supervision. `ra_log_queue_free()` requires producers to have stopped.
- `ra_log_queue_push()` uses bounded `snprintf` and a try-lock, and is **not** a hard real-time guarantee. Introduce a dedicated fixed-format RT event ring for strict RT paths.
- `privileges.h`: experimental child-only `ra_privileges_restrict_exec()` clears permitted/effective/inheritable/ambient capabilities and applies Linux `no_new_privs` after scheduling is selected. It is **not enabled automatically**; a spawn-policy/handshake integration and test are required first.
- Separate thread policies from capability authority. `SCHED_OTHER` does not drop inherited `CAP_SYS_NICE`. Dropping capabilities, bounding/ambient sets and `no_new_privs` must happen before untrusted child code. Audit file capabilities, rlimits and inheritance before enabling capability-based RT.
- For each audio graph identify capture/device callback, resampler, codec, recorder and network threads. Promote only the audited bounded-latency callback thread, not arbitrary GStreamer worker processes.
- Move logging drain to an owning thread without changing audit callback ordering; measure queue saturation and dispatch latency before making it the default path.
- Provide startup error handshakes for subprocess exec/profile problems and clear diagnostics.

## Experimental worker logging integration

Set `log.async-workers=true` in the application configuration to enable
bounded logging from non-owner threads. `logger_init()` records its caller
as owner and installs a 20 ms GLib default-context drain (up to 128 records
per tick). The owner still calls the existing `Log()` directly; workers
enqueue fixed-size formatted messages and never invoke callbacks or write
files. Drops are counted and periodically reported by the owning thread.

**Not production default yet:** all worker producer threads must be joined
before `logger_end()`. The application's GLib default context must be
driven by the same thread that called `logger_init()`. This first stage
formats bounded messages on workers and is not safe for hard real-time
callbacks: use a fixed binary event ring there. Overflow truncates messages;
overload discards entries rather than blocking producers. Worker timestamps
are assigned at drain time, not at submission time.
