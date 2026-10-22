/* moonivibe.c -- MooniVibe DSP engine.
 *
 * A stereo phase shifter in the UniVibe tradition: four cascaded first-order
 * all-pass sections whose poles are swept by a lamp shining on photoresistors,
 * with the notches produced by summing the phase-shifted path with dry.
 *
 * ===========================================================================
 * THE ALL-PASS COEFFICIENT. GET THIS RIGHT OR NOTHING WORKS.
 *
 * For   H(z) = (-g + z^-1) / (1 - g*z^-1)
 * the phase shift is -90 degrees at the frequency given by
 *
 *     g = (1 - sin(w)) / cos(w),      w = 2*pi*f/fs
 *
 * Earlier revisions used g = tan(pi*f/fs), which is the bilinear mapping for
 * a first-order LOWPASS. That gives about 9 degrees at f instead of 90, so a
 * four-stage cascade rotated ~19 degrees in total where a phase shifter needs
 * several hundred. With that little rotation NO NOTCHES FORM, the effect has
 * nothing to do, and every control that acts through the cascade -- mix,
 * stages, LFO shape, vibrato, lamp, intensity, spread, mismatch, capacitor --
 * is inaudible. Only the drive knob (which scales the signal outside the
 * cascade) and the diode stage (after the sum) could be heard.
 *
 * Verified: g = 0.8668 at 1 kHz gives exactly -90.0 degrees.
 * ===========================================================================
 *
 * WHY THE STAGES DO NOT MOVE TOGETHER
 *   A real UniVibe has one lamp and four photocells that are NOT matched to
 *   each other. Each cell has its own dark resistance and its own response
 *   time, so the four stages drift RELATIVE to one another as the lamp
 *   pulses. That mismatch, accumulated through the cascade, is the wonky
 *   double-pulse character that separates a UniVibe from a clean phaser.
 *
 *   So each stage carries its own lag state, its own rise/fall constants and
 *   its own resting pole offset, all fixed at init from a seed so a patch is
 *   reproducible.
 *
 * THE STAGE POLES ARE STAGGERED
 *   The schematic's four phase capacitors are 470p, 4n7, 15n and 220n -- a
 *   ~470x spread in deliberately uneven steps. The DAFx2019 grey-box paper
 *   calls this out: "staggered or spread notches unlike other phasers which
 *   tend to use matched pairs". The stagger is preserved here as a set of
 *   pole frequencies spread unevenly across the band.
 */

#define _GNU_SOURCE 1

#include "moonivibe.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---------------------------------------------------------------------------
 * Component tables
 * ------------------------------------------------------------------------ */

/* Capacitor selector. Each entry scales the whole set of stage poles, so
 * turning the knob moves all four notches together -- the three variants are
 * plausible part substitutions around the schematic's stock value. */
const double MV_CAP_FARADS[MV_CAP_COUNT] = {
    220.0e-9,   /* Stock  -- the schematic's largest phase cap */
    100.0e-9,   /* Dark   -- larger caps, notches lower        */
    47.0e-9,    /* Mid    -- smaller caps, notches higher      */
    22.0e-9     /* Bright -- smallest set, notches well up     */
};

const char *MV_CAP_LABELS[MV_CAP_COUNT] = {
    "Stock 220n", "Dark 100n", "Mid 47n", "Bright 22n"
};

/* Lamp matching grades. `spread` is how far the four cells differ from each
 * other; `shape` bends the response curve away from linear.
 *
 * Matched is deliberately the boring one -- it is what a modern clone does,
 * and having it available makes the other three legible by comparison. */
const mv_lamp_t MV_LAMPS[MV_LAMP_COUNT] = {
    { 0.22, 0.11, 0.00, 0.0, "Matched" },
    { 0.22, 0.11, 0.18, 0.8, "Stock  " },
    { 0.24, 0.09, 0.40, 1.6, "Wide   " },
    { 0.16, 0.06, 0.62, 2.2, "Drift  " }
};

/* Input resistor feeding the phase network. Smaller == hotter. */
const double MV_DRIVE_OHMS[MV_DRIVE_COUNT] = {
    100.0e3, 47.0e3, 22.0e3, 10.0e3
};

const char *MV_DRIVE_LABELS[MV_DRIVE_COUNT] = {
    "Clean 100k", "Stock 47k ", "Hot 22k   ", "Slam 10k  "
};

/* Output diodes. asym != 0 gives a different curve per polarity, which is
 * what puts even-order harmonics in the germanium setting. */
const mv_diode_t MV_DIODES[MV_DIODE_COUNT] = {
    { 4.0, 4.0, 0.00, "None      " },   /* transparent down to full scale */
    { 0.7, 1.6, 0.00, "Silicon   " },
    { 0.5, 3.2, 0.35, "Germanium " },   /* soft, asymmetric -> even harmonics */
    { 0.9, 0.5, 0.00, "LED       " }
};

/* ---------------------------------------------------------------------------
 * Stage poles.
 *
 * These are the frequencies at which each stage rotates 90 degrees. They are
 * staggered unevenly, mirroring the schematic's 470p/4n7/15n/220n capacitor
 * spread -- a tidy progression would give the tidy pair of notches a normal
 * phaser has, which is not the sound this is meant to make.
 *
 * STOCK_POLES is the resting set; the lamp sweeps each one across LAMP_SWEEP
 * times its resting frequency.
 * ------------------------------------------------------------------------ */

static const double STOCK_POLES[MV_STAGES_MAX] = {
    220.0, 600.0, 1200.0, 3000.0,    /* uneven on purpose */
    6800.0, 14000.0                  /* 6-stage option continues the spread */
};

#define MV_LAMP_SWEEP 4.0    /* pole travels over this ratio as the lamp moves */

/* ---------------------------------------------------------------------------
 * The all-pass coefficient. THIS IS THE FUNCTION THAT MATTERS.
 *
 * g = (1 - sin(w)) / cos(w)  gives -90 degrees of phase shift at frequency f.
 * Verified numerically across the band: 100 Hz, 1 kHz, 8 kHz all land on
 * exactly -90.0 degrees.
 * ------------------------------------------------------------------------ */

static double pole_to_g(double f_pole, double fs)
{
    double w = 2.0 * M_PI * f_pole / fs;
    double c;

    /* Keep well inside the unit circle and away from Nyquist. */
    if (w > 2.9) w = 2.9;
    if (w < 1.0e-6) w = 1.0e-6;

    c = cos(w);
    if (c < 0.05) c = 0.05;          /* guard the division */

    return (1.0 - sin(w)) / c;
}

/* Pole frequency for one stage, given the lamp position 0..1. */
static double stage_pole(int stage, double lamp_pos)
{
    int i = stage < 0 ? 0 : (stage >= MV_STAGES_MAX ? MV_STAGES_MAX - 1 : stage);
    /* lamp_pos 0 = dark (low pole), 1 = bright (high pole) */
    double ratio = 1.0 + (MV_LAMP_SWEEP - 1.0) * lamp_pos;
    return STOCK_POLES[i] * ratio;
}

double moonivibe_pole_hz(const moonivibe_t *uv, int stage)
{
    if (!uv || stage < 0 || stage >= uv->stages) return 0.0;
    return STOCK_POLES[stage < MV_STAGES_MAX ? stage : MV_STAGES_MAX - 1];
}

/* ---------------------------------------------------------------------------
 * Photocell mismatch -- deterministic, so a patch sounds the same every load
 * but irregular enough to break the grid.
 * ------------------------------------------------------------------------ */

/* Integer hash. No libc rand(), so the sequence is identical on every platform
 * and cannot be perturbed by anything else seeding the C RNG. */
static unsigned mv_hash(unsigned x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* Stage 0 is the reference cell (multiplier 1.0); the others deviate around
 * it, which is how a real unit behaves -- you match three cells to one, and
 * the reference is whatever it is. */
static void rebuild_mismatch(moonivibe_t *uv)
{
    const double spread = MV_LAMPS[uv->lamp_sel].spread * uv->mismatch;
    const int n = uv->stages < 2 ? 2 : uv->stages;

    uv->rise_mul[0] = 1.0;
    uv->fall_mul[0] = 1.0;
    uv->pole_mul[0] = 1.0;

    for (int s = 1; s < MV_STAGES_MAX; s++) {
        if (s >= n) {
            uv->rise_mul[s] = 1.0;
            uv->fall_mul[s] = 1.0;
            uv->pole_mul[s] = 1.0;
            continue;
        }

        unsigned h1 = mv_hash(uv->seed + (unsigned)s * 2654435761u);
        unsigned h2 = mv_hash(uv->seed + (unsigned)s * 40503u + 12345u);
        unsigned h3 = mv_hash(uv->seed + (unsigned)s * 2246822519u + 777u);

        /* Map to -1..1, then scale by spread. The three properties deviate
         * INDEPENDENTLY: a cell slow to darken is not necessarily slow to
         * light, nor off-centre in its resting resistance. */
        double d1 = ((double)(h1 & 0xffffu) / 32767.5) - 1.0;
        double d2 = ((double)(h2 & 0xffffu) / 32767.5) - 1.0;
        double d3 = ((double)(h3 & 0xffffu) / 32767.5) - 1.0;

        uv->rise_mul[s] = 1.0 + d1 * spread;
        uv->fall_mul[s] = 1.0 + d2 * spread * 1.35;    /* fall deviates more */
        uv->pole_mul[s] = 1.0 + d3 * spread * 0.55;    /* resting resistance */

        if (uv->rise_mul[s] < 0.05) uv->rise_mul[s] = 0.05;
        if (uv->fall_mul[s] < 0.05) uv->fall_mul[s] = 0.05;
        if (uv->pole_mul[s] < 0.50) uv->pole_mul[s] = 0.50;
        if (uv->pole_mul[s] > 1.60) uv->pole_mul[s] = 1.60;
    }
}

double moonivibe_stage_rise_mul(const moonivibe_t *uv, int stage)
{
    if (!uv || stage < 0 || stage >= MV_STAGES_MAX) return 1.0;
    return uv->rise_mul[stage];
}

void moonivibe_set_seed(moonivibe_t *uv, unsigned seed)
{
    if (!uv) return;
    uv->seed = seed;
    rebuild_mismatch(uv);
    uv->dirty = 1;
}

/* ---------------------------------------------------------------------------
 * Init and component setters
 * ------------------------------------------------------------------------ */

int moonivibe_init(moonivibe_t *uv, double sample_rate)
{
    if (!uv || sample_rate < 8000.0) return -1;

    memset(uv, 0, sizeof(*uv));
    uv->sample_rate = sample_rate;

    /* Defaults: the classic voicing, with a real unit's mismatch. */
    uv->cap_sel   = 0;      /* Stock      */
    uv->lamp_sel  = 1;      /* Stock      */
    uv->drive_sel = 1;      /* Stock 47k  */
    uv->diode_sel = 0;      /* None       */
    uv->mode      = MV_MODE_CHORUS;
    uv->stages    = 4;
    uv->lfo_shape = MV_SHAPE_SINE;
    uv->speed     = 0.6;
    uv->lamp_lfo  = 0.17;   /* slow, and deliberately NOT a multiple of speed */
    uv->lamp_bias = 0.5;    /* centred resting brightness */
    uv->intensity = 0.65;
    uv->spread    = 0.25;
    uv->mismatch  = 1.0;
    uv->mix       = 0.75;   /* matches the manifest default */
    uv->output    = 0.7;
    uv->seed      = 0x5eed1234u;

    uv->lfo_phase[0] = 0.0;
    uv->lfo_phase[1] = 0.0;
    uv->lamp_phase  = 0.0;

    /* Must run first: pole_mul feeds the coefficient seeding below. */
    rebuild_mismatch(uv);

    /* SEED EVERY STAGE COEFFICIENT. memset() left them at zero, and the render
     * loop only walks g TOWARD a target -- so a fresh instance ran with g = 0,
     * which turns the all-pass into a one-sample delay and makes wet*x + dry*in
     * collapse to in for every value of mix.
     *
     * Start each stage at its own resting pole, which is where the lamp would
     * have it sitting with the lamp at its midpoint. */
    for (int ch = 0; ch < MV_CHANNELS; ch++) {
        for (int s = 0; s < MV_STAGES_MAX; s++) {
            double pole = stage_pole(s, 0.5) * uv->pole_mul[s];
            uv->g[ch][s]      = pole_to_g(pole, uv->sample_rate);
            uv->g_rest[ch][s] = uv->g[ch][s];
        }
    }

    uv->dirty = 1;
    return 0;
}

static int clamp_index(int v, int n)
{
    if (v < 0) return 0;
    if (v >= n) return n - 1;
    return v;
}

void moonivibe_set_cap(moonivibe_t *uv, int sel)
{
    if (!uv) return;
    uv->cap_sel = clamp_index(sel, MV_CAP_COUNT);
    uv->dirty = 1;
}

void moonivibe_set_lamp(moonivibe_t *uv, int sel)
{
    if (!uv) return;
    uv->lamp_sel = clamp_index(sel, MV_LAMP_COUNT);
    rebuild_mismatch(uv);        /* the grade changes the cell mismatch */
    uv->dirty = 1;
}

void moonivibe_set_drive(moonivibe_t *uv, int sel)
{
    if (!uv) return;
    uv->drive_sel = clamp_index(sel, MV_DRIVE_COUNT);
    uv->dirty = 1;
}

void moonivibe_set_diode(moonivibe_t *uv, int sel)
{
    if (!uv) return;
    uv->diode_sel = clamp_index(sel, MV_DIODE_COUNT);
    uv->dirty = 1;
}

/* ---------------------------------------------------------------------------
 * Parameters
 * ------------------------------------------------------------------------ */

int moonivibe_set_param(moonivibe_t *uv, const char *key, double value)
{
    if (!uv || !key) return -1;

    if (!strcmp(key, "speed"))          uv->speed = value < 0.02 ? 0.02 : (value > 12.0 ? 12.0 : value);
    else if (!strcmp(key, "intensity")) uv->intensity = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
    else if (!strcmp(key, "lamp_lfo"))  uv->lamp_lfo = value < 0.0 ? 0.0 : (value > 8.0 ? 8.0 : value);
    else if (!strcmp(key, "lamp_bias")) uv->lamp_bias = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
    else if (!strcmp(key, "spread"))    uv->spread = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
    else if (!strcmp(key, "mix"))       uv->mix = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
    else if (!strcmp(key, "output"))    uv->output = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
    else if (!strcmp(key, "mode"))      uv->mode = value > 0.5 ? MV_MODE_CHORUS : MV_MODE_VIBRATO;
    else if (!strcmp(key, "stages")) {
        /* The wrapper normalises every encoding to a stage COUNT. */
        uv->stages = (value > 4.5) ? 6 : 4;
    }
    else if (!strcmp(key, "lfo_shape")) uv->lfo_shape = clamp_index((int)(value + 0.5), 3);
    else if (!strcmp(key, "mismatch")) {
        uv->mismatch = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
        rebuild_mismatch(uv);
    }
    else if (!strcmp(key, "cap"))       moonivibe_set_cap(uv, (int)(value + 0.5));
    else if (!strcmp(key, "lamp"))      moonivibe_set_lamp(uv, (int)(value + 0.5));
    else if (!strcmp(key, "drive"))     moonivibe_set_drive(uv, (int)(value + 0.5));
    else if (!strcmp(key, "diode"))     moonivibe_set_diode(uv, (int)(value + 0.5));
    else return -1;

    if (!strcmp(key, "stages")) rebuild_mismatch(uv);

    uv->dirty = 1;
    return 0;
}

int moonivibe_get_param(const moonivibe_t *uv, const char *key, double *out)
{
    if (!uv || !key || !out) return -1;

    if (!strcmp(key, "speed"))          *out = uv->speed;
    else if (!strcmp(key, "intensity")) *out = uv->intensity;
    else if (!strcmp(key, "lamp_lfo"))  *out = uv->lamp_lfo;
    else if (!strcmp(key, "lamp_bias")) *out = uv->lamp_bias;
    else if (!strcmp(key, "spread"))    *out = uv->spread;
    else if (!strcmp(key, "mix"))       *out = uv->mix;
    else if (!strcmp(key, "output"))    *out = uv->output;
    else if (!strcmp(key, "mismatch"))  *out = uv->mismatch;
    else if (!strcmp(key, "mode"))      *out = (double)uv->mode;
    else if (!strcmp(key, "stages"))    *out = (double)uv->stages;
    else if (!strcmp(key, "lfo_shape")) *out = (double)uv->lfo_shape;
    else if (!strcmp(key, "cap"))       *out = (double)uv->cap_sel;
    else if (!strcmp(key, "lamp"))      *out = (double)uv->lamp_sel;
    else if (!strcmp(key, "drive"))     *out = (double)uv->drive_sel;
    else if (!strcmp(key, "diode"))     *out = (double)uv->diode_sel;
    else return -1;

    return 0;
}

/* ---------------------------------------------------------------------------
 * The render loop
 * ------------------------------------------------------------------------ */

/* Shapes the raw LFO phase into -1..1. */
static double lfo_value(int shape, double phase)
{
    switch (shape) {
    case MV_SHAPE_TRIANGLE: {
        double t = phase / (2.0 * M_PI);
        t = t - floor(t);
        return 4.0 * fabs(t - 0.5) - 1.0;
    }
    case MV_SHAPE_SQUARE:
        return tanh(3.0 * sin(phase));
    default:
        return sin(phase);
    }
}

/* One clipping stage, with the knee and asymmetry from the diode table. */
static double diode_shape(double x, const mv_diode_t *d)
{
    if (d->threshold >= 3.9) return x;          /* "None" passes through */

    const double k = d->knee;
    double lim = d->threshold * (1.0 + d->asym * (x > 0.0 ? 1.0 : -1.0));

    return lim * tanh(k * x / lim) / k;
}

void moonivibe_render(moonivibe_t *uv, short *lr, int frames)
{
    if (!uv || !lr || frames <= 0) return;

    const double fs = uv->sample_rate;
    const mv_lamp_t *lamp = &MV_LAMPS[uv->lamp_sel];
    const mv_diode_t *diode = &MV_DIODES[uv->diode_sel];
    const int nstages = uv->stages;

    /* The capacitor selector shifts the whole pole set. Larger caps = lower
     * poles, so scale by the ratio against the stock value. */
    const double capScale = MV_CAP_FARADS[0] / MV_CAP_FARADS[uv->cap_sel];

    /* Drive: the resistor sets how hard the network is pushed. Normalised
     * against the stock value so Stock == 1.0. */
    const double drive = MV_DRIVE_OHMS[1] / MV_DRIVE_OHMS[uv->drive_sel];

    /* Mix. The output is wet*x + dry*in, and at a null (x = -in) that is
     * (dry-wet)*in -- so the comb is DEEPEST when wet and dry are equal.
     * The knob therefore runs 0 (dry only) to 1 (equal parts).
     *
     * Vibrato ignores Mix entirely: it is the phase-shifted path alone, which
     * is the hardware's vibrato position and produces no comb at all. */
    /* Mix spans dry-only (0) to EQUAL PARTS (1). Equal parts is where the
     * comb is deepest, so that belongs at the knob's maximum -- a straight
     * 0..1 wet amount would put the deepest point at 0.5 and leave the top
     * half removing dry and shallowing the comb again. */
    /* TRUE dry/wet. Fully left = the input passes through unchanged, which
     * means dry = 1 and wet = 0 with unity sum -- so the output level does
     * not move as the knob travels. The deepest comb therefore sits at the
     * middle of the knob, where equal parts puts it.  */
    /* TRUE dry/wet, applied in BOTH modes.
     *
     * Fully left = the input passes through untouched (dry 1, wet 0), so the
     * knob works the same in Chorus and Vibrato. The Mode switch changes the
     * CHARACTER of the sweep, not the blend. */
    /* Mix spans dry-only (0) up to EQUAL PARTS (1).
     *
     * The comb is deepest when wet and dry are equal, so that belongs at the
     * knob's MAXIMUM -- a straight 0..1 crossfade put it at the centre, which
     * made the effect peak halfway and shallow again past it. */
    const double wet = (uv->mode == MV_MODE_VIBRATO) ? 1.0 : (0.5 * uv->mix);
    const double dry = (uv->mode == MV_MODE_VIBRATO) ? 0.0 : (1.0 - wet);

    const double lfo_inc  = 2.0 * M_PI * uv->speed / fs;
    /* The second lamp rate. Deliberately not a multiple of the first, so the
     * two beat against each other instead of locking -- that wandering is the
     * character a single LFO cannot produce. */
    const double lamp_inc = 2.0 * M_PI * uv->lamp_lfo / fs;

    for (int i = 0; i < frames; i++) {
        /* Update every stage's coefficient for this sample. */
        for (int ch = 0; ch < MV_CHANNELS; ch++) {
            const double lfo = lfo_value(uv->lfo_shape, uv->lfo_phase[ch]);

            for (int s = 0; s < nstages; s++) {
                /* The lamp position for this cell: 0 dark, 1 bright. Intensity
                 * sets how far the sweep travels. */
                double depth   = uv->intensity;
                /* Bias sets the resting brightness; the main LFO sweeps
                 * around it; the second, slower LFO adds a gentle wander on
                 * top so the sweep never repeats exactly. */
                /* The second lamp LFO MODULATES THE SWEEP DEPTH rather than
                 * adding an offset. An offset was absorbed by the 0..1 clamp
                 * below -- the main LFO at full depth already reaches both
                 * rails, so anything added on top was thrown away and the
                 * control was inaudible. Scaling the depth wanders how far the
                 * sweep travels, which survives the clamp at any bias.
                 *
                 * The two rates are deliberately not multiples, so they beat
                 * against each other instead of locking: that slow drift is
                 * the character a single LFO cannot produce. */
                /* The wander scales the SWEEP DEPTH, centred below 1.0 so it
                 * always has headroom in both directions. A +/- swing around
                 * 1.0 was only audible on its shrinking half: once the main
                 * LFO drives lampPos to both rails, adding more depth changes
                 * nothing, so half the cycle was silent.
                 *
                 * Range 0.35 .. 0.95 -- always leave room to travel. */
                double wander = (uv->lamp_lfo > 0.0)
                              ? (0.65 + 0.30 * sin(uv->lamp_phase)) : 1.0;
                double lampPos = uv->lamp_bias + 0.5 * lfo * depth * wander;
                if (lampPos < 0.0) lampPos = 0.0;
                if (lampPos > 1.0) lampPos = 1.0;

                /* Each stage's pole: its own resting frequency (with its own
                 * photocell offset) swept by the lamp. */
                double pole   = stage_pole(s, lampPos) * uv->pole_mul[s] * capScale;
                double target = pole_to_g(pole, fs);

                /* The lamp's curve bends the response rather than shrinking
                 * it: applied to the deviation so unity is kept at the
                 * extremes, which is what a thermal lamp does. */
                double g0 = uv->g[ch][s];
                double rest = uv->g_rest[ch][s];
                target = rest + (target - rest) *
                         (1.0 - 0.25 * lamp->shape * (1.0 - fabs(lfo)));

                /* Each cell chases the target with its OWN rise/fall rates, so
                 * the stages drift apart as the lamp pulses. */
                double rate = (target > g0 ? lamp->rise * uv->rise_mul[s]
                                           : lamp->fall * uv->fall_mul[s]);
                if (rate > 1.0) rate = 1.0;

                uv->g[ch][s] = g0 + (target - g0) * rate;
            }
        }

        /* Run this frame's two channels through their cascades. */
        for (int ch = 0; ch < MV_CHANNELS; ch++) {
            const double in = (double)lr[i * 2 + ch] / 32768.0;

            const double driven = in * drive;   /* drive feeds BOTH paths */
            double x = driven;
            for (int s = 0; s < nstages; s++) {
                double g = uv->g[ch][s];
                if (g > 0.999) g = 0.999;
                if (g < -0.999) g = -0.999;

                mv_ap_stage_t *st = &uv->ap[ch][s];
                double y = -g * x + st->x1 + g * st->y1;
                st->x1 = x;
                st->y1 = y;
                x = y;
            }

            /* THE MIX IS LAST. The wet path is finished and coloured first,
             * then summed with dry -- the pedal's output mixer is the final
             * node, with nothing after it. */
            x = diode_shape(x, diode);

            x *= wet;                        /* scale AFTER the diode, so it conducts */
            double out = x + dry * driven;
            out *= uv->output * 0.9;      /* headroom below the clip */

            if (out > 1.0) out = 1.0;
            if (out < -1.0) out = -1.0;

            lr[i * 2 + ch] = (short)(out * 32767.0);
        }

        /* Advance the LFO, holding channel 1 at a fixed offset so the two
         * sides keep their relationship. */
        uv->lfo_phase[0] += lfo_inc;
        if (uv->lfo_phase[0] > 2.0 * M_PI) uv->lfo_phase[0] -= 2.0 * M_PI;

        uv->lfo_phase[1] = uv->lfo_phase[0] + uv->spread * M_PI;
        if (uv->lfo_phase[1] > 2.0 * M_PI) uv->lfo_phase[1] -= 2.0 * M_PI;

        if (uv->lamp_lfo > 0.0) {
            uv->lamp_phase += lamp_inc;
            if (uv->lamp_phase > 2.0 * M_PI) uv->lamp_phase -= 2.0 * M_PI;
        }
    }

    uv->dirty = 0;
}
