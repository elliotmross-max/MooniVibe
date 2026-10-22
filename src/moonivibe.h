/* moonivibe.h -- MooniVibe: stereo phase shifter with swappable circuit parts.
 *
 * THE IDEA
 *   The original Shin-Ei UniVibe is four cascaded all-pass stages whose pole
 *   frequency is swept by a lamp shining on photoresistors. Its voice is not
 *   one fixed sound: it is whatever parts Shin-Ei happened to fit that week.
 *   Wide-tolerance capacitors move the notches, different bulbs change how the
 *   sweep lags, different resistors change how hard the signal hits the
 *   network, different diodes change how it breaks up.
 *
 *   So this engine does NOT store "notch frequency" or "sweep depth". It
 *   stores COMPONENT VALUES and derives coefficients from them the way the
 *   circuit would. Turning the Capacitor knob is a part substitution, not a
 *   preset change: neighbouring settings relate continuously and physically.
 *
 * WHY THE STAGES DO NOT MOVE TOGETHER  (this is the whole point)
 *   A real UniVibe does not have one clean oscillator sweeping four filters.
 *   It has a lamp with thermal inertia, and FOUR PHOTOCELLS THAT ARE NOT
 *   MATCHED TO EACH OTHER. Each cell has slightly different dark resistance
 *   and response time, so the four stages drift RELATIVE TO EACH OTHER as the
 *   lamp pulses. That mismatch, accumulated through the cascade, is the wonky
 *   double-pulse character people recognise -- and it is exactly what most
 *   emulations miss. A shared sweep reads as tremolo or a cheap rotary
 *   speaker; four mismatched parts doing the same thing slightly differently
 *   is what makes it a UniVibe.
 *
 *   So every stage carries its OWN lag state and its OWN rise/fall constants.
 *   Nothing here is quantised to a grid, nothing repeats exactly, and the two
 *   channels are offset from each other.
 *
 *   The Lamp selector is therefore a MATCHING GRADE, not a speed:
 *
 *     Matched  all four cells identical -- clean, modern, the "cheap clone"
 *     Stock    small spread, a real unit's tolerance
 *     Wide     deliberately mismatched, pronounced wonk
 *     Drift    large spread, slow divergent wandering
 *
 *   "Matched" is there on purpose: it is the sound people complain about, and
 *   having it makes the other three legible by comparison.
 *
 * ORIGINAL vs. INHERITED
 *   Inherited from the topology: four all-pass sections, summed dry/wet
 *   notches, lamp lag, vibrato/chorus as a wet/dry difference.
 *   Original: independent per-channel state with an adjustable LFO phase
 *   offset (the hardware is mono), per-stage photocell mismatch, and a lamp
 *   whose rise and fall are shaped curves rather than a linear lag.
 */

#ifndef MOONIVIBE_H
#define MOONIVIBE_H

#include <stddef.h>

#define MV_STAGES_MAX   6
#define MV_CHANNELS     2

/* ---------------------------------------------------------------------------
 * Component tables. Each index is a physical part.
 * ------------------------------------------------------------------------ */

/* All-pass capacitors. f_pole = 1 / (2*pi*R*C), so halving C doubles f_pole. */
#define MV_CAP_COUNT 4
extern const double MV_CAP_FARADS[MV_CAP_COUNT];   /* 47n, 22n, 10n, 4.7n */
extern const char  *MV_CAP_LABELS[MV_CAP_COUNT];   /* "Dark 47nF", ... */

/* Lamp / photocell MATCHING GRADE.
 *
 * `spread` is how far the four cells differ from each other. The per-cell
 * deviation is fixed at init (from `seed`) so a patch sounds the same every
 * time it is loaded, while still being irregular.
 *
 * `shape` bends the lamp's response curve away from linear: higher values give
 * a more compressed drive and a more rounded, pulse-like attack.
 *
 * `rise`/`fall` are the base lag rates. Each cell multiplies these by its own
 * factor, so the four stages chase the same target at different paces. */
#define MV_LAMP_COUNT 4
typedef struct {
    double rise;      /* base climb rate toward the LFO target      */
    double fall;      /* base decay rate. fall < rise == hangs      */
    double spread;    /* 0 = all cells identical, 0.45 = wild       */
    double shape;     /* response-curve compression, 0 = linear      */
    const char *label;
} mv_lamp_t;
extern const mv_lamp_t MV_LAMPS[MV_LAMP_COUNT];

/* Input resistor feeding the phase network. Smaller == hotter. */
#define MV_DRIVE_COUNT 4
extern const double MV_DRIVE_OHMS[MV_DRIVE_COUNT]; /* 100k, 47k, 22k, 10k */
extern const char  *MV_DRIVE_LABELS[MV_DRIVE_COUNT];

/* Output clipping diodes. None / Silicon / Germanium / LED.
 * The germanium knee is asymmetric, which is why it sounds warmer. */
#define MV_DIODE_COUNT 4
typedef struct {
    double threshold;  /* where conduction starts, normalised */
    double knee;       /* softness of the transition          */
    double asym;       /* 0 = symmetric, >0 = different per polarity */
    const char *label;
} mv_diode_t;
extern const mv_diode_t MV_DIODES[MV_DIODE_COUNT];

/* Mode: the original's two-position switch, which is all the circuit offers.
 *
 *   Vibrato  wet only, no dry summed   -- phase-shifted path alone, no comb
 *   Chorus   wet + dry, partial blend  -- the notched comb, Mix sets depth
 *
 * Both read the same delay-free all-pass network; the switch picks which
 * signals reach the output. There is no third position: a deeper sweep is
 * what the Intensity knob is for, not a separate mode. */
#define MV_MODE_VIBRATO 0
#define MV_MODE_CHORUS  1
#define MV_MODE_COUNT   2

/* LFO shapes. */
#define MV_SHAPE_SINE     0
#define MV_SHAPE_TRIANGLE 1
#define MV_SHAPE_SQUARE   2

/* ---------------------------------------------------------------------------
 * Engine
 * ------------------------------------------------------------------------ */

typedef struct {
    double x1, y1;      /* one first-order all-pass section's state */
} mv_ap_stage_t;

typedef struct {
    mv_ap_stage_t ap[MV_CHANNELS][MV_STAGES_MAX];

    double lfo_phase[MV_CHANNELS];   /* radians, per channel */
    double lamp_phase;               /* the SECOND, slower lamp LFO */

    /* PER-STAGE lamp state. These are the whole point: four cells that do
     * not move together. g[] is each stage's own coefficient. */
    double g[MV_CHANNELS][MV_STAGES_MAX];        /* live coefficient */
    double g_rest[MV_CHANNELS][MV_STAGES_MAX];   /* resting coefficient */
    double pole_mul[MV_STAGES_MAX];  /* fixed per-stage RESTING POLE
                                      * multiplier -- the photocells are not
                                      * matched, so each stage's pole sits at
                                      * a different frequency. This is the
                                      * wonk, and it is what makes the notches
                                      * land unevenly through the cascade. */
    double rise_mul[MV_STAGES_MAX];  /* fixed per-stage rise multiplier */
    double fall_mul[MV_STAGES_MAX];  /* fixed per-stage fall multiplier */

    /* parameters (component selectors are ints; the rest are normalised) */
    int    cap_sel;
    int    lamp_sel;
    int    drive_sel;
    int    diode_sel;
    int    mode;
    int    stages;        /* 4 or 6 */
    int    lfo_shape;
    double speed;         /* main LFO rate */
    double lamp_lfo;      /* SECOND, slower lamp modulation rate (0 = off) */
    double lamp_bias;     /* lamp resting brightness, 0..1 (0.5 = centred) */
    double intensity;     /* 0 .. 1, sweep depth into g */
    double spread;        /* 0 .. 1, LFO phase offset between channels */
    double mismatch;      /* 0 .. 1, scales the lamp table's spread */
    double mix;           /* 0 .. 1, wet amount within the mode's range */
    double output;        /* 0 .. 1, output level */

    unsigned seed;        /* fixes the per-cell jitter */
    double sample_rate;
    int    dirty;
} moonivibe_t;

/* Build / reset. sample_rate must be > 8000. Returns 0 on success. */
int  moonivibe_init(moonivibe_t *uv, double sample_rate);

/* Re-roll the fixed photocell mismatch. Same seed == same mismatch. */
void moonivibe_set_seed(moonivibe_t *uv, unsigned seed);

/* Push a component change; recomputes coefficients from the parts. */
void moonivibe_set_cap  (moonivibe_t *uv, int sel);
void moonivibe_set_lamp (moonivibe_t *uv, int sel);
void moonivibe_set_drive(moonivibe_t *uv, int sel);
void moonivibe_set_diode(moonivibe_t *uv, int sel);

/* Parameter interface, shared with the wrapper.
 * Returns 0 on success. Unrecognised keys leave *out untouched and return -1. */
int  moonivibe_set_param(moonivibe_t *uv, const char *key, double value);
int  moonivibe_get_param(const moonivibe_t *uv, const char *key, double *out);

/* Process `frames` interleaved stereo int16 samples IN PLACE. */
void moonivibe_render(moonivibe_t *uv, short *lr, int frames);

/* Introspection used by the contract test. */
double moonivibe_pole_hz(const moonivibe_t *uv, int stage);      /* 0 if unused */
double moonivibe_stage_rise_mul(const moonivibe_t *uv, int stage);

#endif /* MOONIVIBE_H */
