#include "stats.h"
#include "platform.h"
#include "mathx.h"

#define SAMPLE_PERIOD 0.5f   /* seconds between counter reads; cheap on CPU */
#define CPU_TAU 2.0f         /* smoothing time constants */
#define NET_TAU 1.5f

void stats_init(Stats *st, float cpu_override, double net_override) {
    st->have_prev = 0;
    st->since_sample = SAMPLE_PERIOD; /* force an immediate first sample */
    st->cpu_raw = st->cpu = 0.f;
    st->bps_raw = st->bps = 0.0;
    st->cpu_override = cpu_override;
    st->net_override = net_override;
    st->busy0 = st->total0 = st->bytes0 = 0;
}

static void sample(Stats *st, float elapsed) {
    uint64_t busy, total, bytes;
    if (!plat_stats_read(&busy, &total, &bytes)) return;
    if (st->have_prev) {
        uint64_t dt_total = total - st->total0;
        uint64_t dt_busy  = busy - st->busy0;
        if (dt_total > 0) st->cpu_raw = clampf((float)dt_busy / (float)dt_total, 0.f, 1.f);
        uint64_t dbytes = bytes >= st->bytes0 ? bytes - st->bytes0 : 0;
        if (elapsed > 0.01f) st->bps_raw = (double)dbytes * 8.0 / (double)elapsed;
    }
    st->busy0 = busy; st->total0 = total; st->bytes0 = bytes;
    st->have_prev = 1;
}

void stats_update(Stats *st, float dt) {
    st->since_sample += dt;
    if (st->since_sample >= SAMPLE_PERIOD) {
        sample(st, st->since_sample);
        st->since_sample = 0.f;
    }
    float cpu_target = st->cpu_override >= 0.f ? st->cpu_override : st->cpu_raw;
    double bps_target = st->net_override >= 0.0 ? st->net_override : st->bps_raw;
    st->cpu = approachf(st->cpu, cpu_target, CPU_TAU, dt);
    /* Smooth network in log space so a burst decays gracefully instead of
     * snapping the wall from calm to violent and back. */
    double lt = log(bps_target + 1.0), lc = log(st->bps + 1.0);
    lc = approachf((float)lc, (float)lt, NET_TAU, dt);
    st->bps = exp(lc) - 1.0;
}
