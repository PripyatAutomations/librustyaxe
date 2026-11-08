# Task scheduler (experimental)

`scheduler.h` exposes per-task POSIX scheduling policy and nice adjustment.
Use `ra_sched_query`, `ra_sched_apply`, or `ra_sched_drop_rt`. No policy
is changed automatically. This intentionally belongs to librustyaxe so both
rrserver and rrclient (and other programs) may use it.

Linux treats thread IDs as individual scheduler targets for sched_setscheduler;
pass a native TID when adjusting a worker thread. pid=0 applies to caller.
Nice values and scheduling policy changes can require CAP_SYS_NICE or matching
RLIMIT_RTPRIO/RLIMIT_NICE. A drop from inherited RT to SCHED_OTHER is useful
before running lower-priority subprocess work. Do not grant CAP_SYS_NICE
globally to every RustyRig worker. Child priority changes should be applied
before untrusted plugin entrypoints when possible.

RT threads must not block on network, disk, GTK, database locks, or unbounded
queues. Use bounded buffers and explicit yields/backpressure; monitor overruns.
This API does not yet set affinity, SCHED_DEADLINE, RLIMITs, CPU cgroups,
priority inheritance, or spawn-time scheduling. A future supervisor should
manage those and report effective priorities. `ra_sched_apply` is not
transactional if applying nice fails after changing the policy.
