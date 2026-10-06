/*
 * Small iOS support surface shared by the no-JIT Madeira target.
 *
 * These counters are intentionally kept out of the Wine sources: they are
 * optional diagnostics used by server_ios.c and must not change synchronization
 * semantics. The cache/QoS hooks likewise have safe no-op behavior when a
 * platform-specific implementation is not present.
 */

#include <stdint.h>
#include <stddef.h>

volatile long long ios_alert_wakes;
volatile long long ios_alert_waits;
volatile long long ios_alert_lat_n;
volatile long long ios_alert_lat_ticks;
volatile long long ios_alert_lat_hist[6];
volatile long long ios_alert_spin_tries;
volatile long long ios_alert_spin_hits;

volatile long long ios_qpc_syscalls;

volatile long long ios_xp_set_event;
volatile long long ios_xp_reset_event;
volatile long long ios_xp_pulse_event;
volatile long long ios_xp_wait_single;
volatile long long ios_xp_wait_multi;
volatile long long ios_xp_wait_zero;
volatile long long ios_xp_wait_zero_timeout;
volatile long long ios_xp_yield;
volatile long long ios_xp_yield_slept;
volatile long long ios_xp_delay;
volatile long long ios_xp_delay_hist[6];

void ios_eco_apply_self(void) {}
void ios_inproc_cache_release(void *peb) { (void)peb; }
void madeira_fast_flush_pid(void) {}
