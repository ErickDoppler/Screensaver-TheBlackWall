/* Samples CPU load and network throughput and smooths them for the wall. */
#ifndef BW_STATS_H
#define BW_STATS_H
#include <stdint.h>

typedef struct Stats {
    uint64_t busy0, total0, bytes0;
    int      have_prev;
    float    since_sample;   /* seconds since last counter read */
    float    cpu_raw, cpu;   /* 0..1, raw sample and smoothed value */
    double   bps_raw, bps;   /* bits per second, raw and smoothed */
    float    cpu_override;   /* <0 = use real counters */
    double   net_override;   /* <0 = use real counters */
} Stats;

void stats_init(Stats *st, float cpu_override, double net_override);
void stats_update(Stats *st, float dt);
#endif
