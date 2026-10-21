/* schwung_moonivibe.c -- the Schwung plugin wrapper for MooniVibe.
 *
 * MIRRORS junologue-chorus and nam: an audio FX DECLARES its own API struct
 * rather than including one from upstream. There is no audio_fx_api_v2.h in
 * either repo -- the struct is written into the plugin file, and only
 * "plugin_api_v1.h" is included (for host_api_v1_t).
 *
 * The entry point is move_audio_fx_init_v2. NOT move_plugin_init_v2, which is
 * the sound generator entry point -- exporting that one means the chain host
 * opens the library, finds nothing it recognises, and refuses the slot with no
 * error on screen. That was the whole load failure, confirmed by comparing
 * symbol tables against working audio FX on the device.
 *
 * NO WORKER THREAD. The engine is built synchronously in create_instance, so
 * there is no ready flag and no possibility of rendering before the engine
 * exists. DubStarMooniCon needed a thread; its pthread_setaffinity_np call is
 * what broke when _GNU_SOURCE sat below the includes. Nothing here needs that.
 *
 * process_block handles the buffer IN PLACE -- it arrives holding the chain's
 * audio for this insert, so there is no separate input pointer.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "plugin_api_v1.h"

#include "moonivibe.h"
#include "moonivibe_meta.h"

/* ---------------------------------------------------------------------------
 * Audio FX API v2. Declared here, as junologue-chorus and nam both do.
 * ------------------------------------------------------------------------ */

#define AUDIO_FX_API_VERSION_2 2

typedef struct audio_fx_api_v2 {
    uint32_t api_version;
    void  *(*create_instance)(const char *module_dir, const char *config_json);
    void   (*destroy_instance)(void *instance);
    void   (*process_block)(void *instance, int16_t *audio_inout, int frames);
    void   (*set_param)(void *instance, const char *key, const char *val);
    int    (*get_param)(void *instance, const char *key, char *buf, int buf_len);
} audio_fx_api_v2_t;

typedef audio_fx_api_v2_t *(*audio_fx_init_v2_fn)(const host_api_v1_t *host);

/* Diagnostic: remember the last set_param we saw, verbatim, so get_param can
 * report it. Plain statics -- set_param and get_param both run on the SPI
 * callback, so there is no cross-thread access to guard. */
static const host_api_v1_t *g_host = NULL;

/* Parameter parsing. The host sends enums as their OPTION NAME and floats as
 * decimal text -- usefulity parses exactly this way, with a numeric fallback.
 * The first version of this file used atof() for everything, so a string like
 * "Chorus" or "Slam 10k" silently became 0 and every enum control was frozen
 * at its first option. */

typedef struct { const char *name; int index; } mv_opt_t;

static int mv_match(const char *val, const mv_opt_t *opts, int count, int fallback_scale)
{
    for (int i = 0; i < count; i++)
        if (strcmp(val, opts[i].name) == 0) return opts[i].index;
    /* Numeric fallback. Enum indices may arrive scaled 0..1, so try both. */
    double d = atof(val);
    if (d > 0.0 && d <= 1.0 && fallback_scale > 1) {
        int idx = (int)(d * (double)(fallback_scale - 1) + 0.5);
        if (idx < 0) idx = 0;
        if (idx >= fallback_scale) idx = fallback_scale - 1;
        return idx;
    }
    int idx = (int)(d + 0.5);
    if (idx < 0) idx = 0;
    if (idx >= fallback_scale) idx = fallback_scale - 1;
    return idx;
}

static const mv_opt_t MV_OPT_MODE[]  = { {"Vibrato",0}, {"Chorus",1} };
static const mv_opt_t MV_OPT_CAP[]   = { {"Dark 47nF",0}, {"Stock 22nF",1}, {"Bright 10nF",2}, {"Thin 4.7nF",3} };
static const mv_opt_t MV_OPT_LAMP[]  = { {"Matched",0}, {"Stock",1}, {"Wide",2}, {"Drift",3} };
static const mv_opt_t MV_OPT_DRIVE[] = { {"Clean 100k",0}, {"Stock 47k",1}, {"Hot 22k",2}, {"Slam 10k",3} };
static const mv_opt_t MV_OPT_DIODE[] = { {"None",0}, {"Silicon",1}, {"Germanium",2}, {"LED",3} };
static const mv_opt_t MV_OPT_STAGES[]= { {"4 stages",0}, {"6 stages",1} };
static const mv_opt_t MV_OPT_SHAPE[] = { {"Sine",0}, {"Triangle",1}, {"Soft Square",2} };

/* Map a key + string to the double the engine expects. */
static int mv_parse(const char *key, const char *val, double *out)
{
    if (!strcmp(key, "mode"))       { *out = mv_match(val, MV_OPT_MODE,  2, 2); return 0; }
    if (!strcmp(key, "cap"))        { *out = mv_match(val, MV_OPT_CAP,   4, 4); return 0; }
    if (!strcmp(key, "lamp"))       { *out = mv_match(val, MV_OPT_LAMP,  4, 4); return 0; }
    if (!strcmp(key, "drive"))      { *out = mv_match(val, MV_OPT_DRIVE, 4, 4); return 0; }
    if (!strcmp(key, "diode"))      { *out = mv_match(val, MV_OPT_DIODE, 4, 4); return 0; }
    if (!strcmp(key, "stages")) {
        /* Three encodings arrive in practice:
         *   "4 stages" / "6 stages"  -- the option NAME
         *   "0" / "1"                -- the option INDEX
         *   "4" / "6"                -- the stage COUNT
         * mv_match cannot tell the last two apart, so handle this key here. */
        if (strcmp(val, "4 stages") == 0) { *out = 4; return 0; }
        if (strcmp(val, "6 stages") == 0) { *out = 6; return 0; }
        {
            double d = atof(val);
            if (d > 4.5)      { *out = 6; return 0; }   /* count 5-6 */
            if (d > 1.5)      { *out = 4; return 0; }   /* count 2-4 */
            *out = (d > 0.5) ? 6 : 4;                   /* index 1 -> 6 */
            return 0;
        }
    }
    if (!strcmp(key, "lfo_shape"))  { *out = mv_match(val, MV_OPT_SHAPE, 3, 3); return 0; }
    /* Continuous parameters -- decimal text. */
    if (!strcmp(key, "speed") || !strcmp(key, "intensity") || !strcmp(key, "spread")
     || !strcmp(key, "mismatch") || !strcmp(key, "mix") || !strcmp(key, "output")
     || !strcmp(key, "lamp_lfo") || !strcmp(key, "lamp_bias")) {
        *out = atof(val);
        return 0;
    }
    return -1;
}

static int  mv_write_state(const moonivibe_t *e, char *buf, int buf_len);
static int  mv_json_num(const char *json, const char *key, double *out);
static void mv_restore_state(moonivibe_t *e, const char *json);
static int  mv_match(const char *val, const mv_opt_t *opts, int count, int fallback_scale);
static int  mv_parse(const char *key, const char *val, double *out);

/* Reverse lookup: index -> option name. get_param answers enums with the
 * NAME, the way usefulity does -- returning a bare index lets the host map it
 * against a default range, which is how "Silicon" came out as the last entry. */
static const char *mv_opt_name(const char *key, int idx)
{
    const mv_opt_t *tab = NULL;
    int n = 0;
    if (!strcmp(key, "mode"))       { tab = MV_OPT_MODE;   n = 2; }
    else if (!strcmp(key, "cap"))   { tab = MV_OPT_CAP;    n = 4; }
    else if (!strcmp(key, "lamp"))  { tab = MV_OPT_LAMP;   n = 4; }
    else if (!strcmp(key, "drive")) { tab = MV_OPT_DRIVE;  n = 4; }
    else if (!strcmp(key, "diode")) { tab = MV_OPT_DIODE;  n = 4; }
    else if (!strcmp(key, "stages")){ tab = MV_OPT_STAGES; n = 2; }
    else if (!strcmp(key, "lfo_shape")) { tab = MV_OPT_SHAPE; n = 3; }
    if (!tab) return "";
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    return tab[idx].name;
}

static void mv_log(const char *msg);

/* ---------------------------------------------------------------------------
 * Instance
 * ------------------------------------------------------------------------ */

typedef struct {
    moonivibe_t engine;
} mv_instance_t;

/* Stepped controls: set_param arrives as a string, so these come in as digits
 * ("0".."3") and get_param must answer them as integers so the display shows
 * the option name rather than a number. */
static int mv_is_int_key(const char *key)
{
    return !strcmp(key, "mode")
        || !strcmp(key, "stages")
        || !strcmp(key, "lfo_shape")
        || !strcmp(key, "cap")
        || !strcmp(key, "lamp")
        || !strcmp(key, "drive")
        || !strcmp(key, "diode");
}

/* ---------------------------------------------------------------------------
 * API callbacks
 * ------------------------------------------------------------------------ */

static void mv_log(const char *msg)
{
    if (g_host && g_host->log) {
        char line[256];
        snprintf(line, sizeof(line), "[MOONIVIBE] %s", msg);
        g_host->log(line);
    }
}

static void *v2_create_instance(const char *module_dir, const char *config_json)
{
    (void)module_dir;
    (void)config_json;

    mv_instance_t *inst = (mv_instance_t *)calloc(1, sizeof(*inst));
    if (!inst) return NULL;

    /* MOVE_SAMPLE_RATE is 44100. Built here, on the loader thread, rather than
     * on a worker: the cost is a handful of tan() calls. */
    if (moonivibe_init(&inst->engine, (double)MOVE_SAMPLE_RATE) != 0) {
        free(inst);
        return NULL;
    }

    return inst;
}

static void v2_destroy_instance(void *instance)
{
    free(instance);
}

static void v2_process_block(void *instance, int16_t *audio_inout, int frames)
{
    mv_instance_t *inst = (mv_instance_t *)instance;
    if (!audio_inout || frames <= 0) return;

    /* No engine: pass the audio through untouched rather than muting the
     * chain. An effect that fails to construct should not silence the slot
     * it was inserted into. */
    if (!inst) return;

    moonivibe_render(&inst->engine, (short *)audio_inout, frames);
}

static void v2_set_param(void *instance, const char *key, const char *val)
{
    mv_instance_t *inst = (mv_instance_t *)instance;
    if (!inst || !key || !val) return;

    /* Bulk state restore arrives as a JSON object, not a single value. */
    if (!strcmp(key, "state")) {
        mv_restore_state(&inst->engine, val);
        return;
    }

    double v = 0.0;
    if (mv_parse(key, val, &v) != 0) {
        return;
    }
    (void)moonivibe_set_param(&inst->engine, key, v);

}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len)
{
    mv_instance_t *inst = (mv_instance_t *)instance;
    if (!key || !buf || buf_len <= 0) return -1;
    int n;

    /* Static keys answered without touching the engine. */
    if (!strcmp(key, "name")) {
        n = snprintf(buf, (size_t)buf_len, "%s", "MooniVibe");
        return (n < 0 || n >= buf_len) ? -1 : n;
    }
    if (!strcmp(key, "ui_hierarchy")) {
        n = snprintf(buf, (size_t)buf_len, "%s", MOONIVIBE_UI_HIERARCHY);
        return (n < 0 || n >= buf_len) ? -1 : n;
    }
    if (!strcmp(key, "chain_params")) {
        n = snprintf(buf, (size_t)buf_len, "%s", MOONIVIBE_CHAIN_PARAMS);
        return (n < 0 || n >= buf_len) ? -1 : n;
    }
    if (!strcmp(key, "state")) {
        if (!inst) return -1;
        return mv_write_state(&inst->engine, buf, buf_len);
    }

    if (!inst) return -1;

    double got = 0.0;
    if (moonivibe_get_param(&inst->engine, key, &got) != 0) return -1;

    if (mv_is_int_key(key)) {
        /* Most enums store an INDEX, but stages stores a stage COUNT (4/6),
         * so it has to be mapped back before the name lookup -- otherwise
         * both settings clamp to the last entry and the display freezes. */
        int idx = (int)(got < 0 ? got - 0.5 : got + 0.5);
        if (!strcmp(key, "stages")) idx = (idx > 4) ? 1 : 0;
        n = snprintf(buf, (size_t)buf_len, "%s", mv_opt_name(key, idx));
    }
    else
        n = snprintf(buf, (size_t)buf_len, "%.4f", got);

    return (n < 0 || n >= buf_len) ? -1 : n;
}

/* ---------------------------------------------------------------------------
 * Slot state. The host reads this when a chain is saved, and writes it back
 * on restore. NAM's comment is explicit that without it the host "silently
 * bails on fx:state empty and drops the slot save" -- which is also a
 * candidate for why the slot never comes up at all.
 * ------------------------------------------------------------------------ */

static int mv_write_state(const moonivibe_t *e, char *buf, int buf_len)
{
    return snprintf(buf, (size_t)buf_len,
        "{\"speed\":%.4f,\"intensity\":%.4f,\"mode\":%d,\"cap\":%d,"
        "\"lamp\":%d,\"drive\":%d,\"diode\":%d,\"stages\":%d,"
        "\"lfo_shape\":%d,\"spread\":%.4f,\"mismatch\":%.4f,"
        "\"mix\":%.4f,\"output\":%.4f}",
        e->speed, e->intensity, e->mode, e->cap_sel,
        e->lamp_sel, e->drive_sel, e->diode_sel, e->stages,
        e->lfo_shape, e->spread, e->mismatch,
        e->mix, e->output);
}

/* Pull one numeric field out of a flat {"key":value,...} object. NAM does the
 * same with strstr + atof; there is no JSON library in this module. */
static int mv_json_num(const char *json, const char *key, double *out)
{
    char needle[64];
    const char *p;
    if (!json || !key || !out) return -1;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p) return -1;
    p = strchr(p + strlen(needle), ':');
    if (!p) return -1;
    *out = atof(p + 1);
    return 0;
}

static void mv_restore_state(moonivibe_t *e, const char *json)
{
    static const char *KEYS[] = {
        "speed","intensity","spread","mismatch","mix","output",
        "mode","stages","lfo_shape","cap","lamp","drive","diode"
    };
    for (unsigned i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        double v;
        if (mv_json_num(json, KEYS[i], &v) == 0)
            (void)moonivibe_set_param(e, KEYS[i], v);
    }
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------ */

static audio_fx_api_v2_t g_fx_api_v2;

audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host)
{
    g_host = host;

    memset(&g_fx_api_v2, 0, sizeof(g_fx_api_v2));
    g_fx_api_v2.api_version      = AUDIO_FX_API_VERSION_2;
    g_fx_api_v2.create_instance  = v2_create_instance;
    g_fx_api_v2.destroy_instance = v2_destroy_instance;
    g_fx_api_v2.process_block    = v2_process_block;
    g_fx_api_v2.set_param        = v2_set_param;
    g_fx_api_v2.get_param        = v2_get_param;

    return &g_fx_api_v2;
}

/* ---------------------------------------------------------------------------
 * v1 entry point. The working audio FX on the device export BOTH this and v2:
 *
 *   clap.so:  T move_audio_fx_init_v1
 *             T move_audio_fx_init_v2
 *
 * Airwindows declares api_version 2 and still ships the v1 symbol, so the
 * chain host evidently looks for it. Mine exported only v2 -- the slot is
 * listed from the manifest, then the host goes to instantiate it, finds no
 * v1 entry point, and declines silently.
 *
 * plugin_api_v1_t carries SEVEN callbacks and NO instance pointer:
 *
 *   on_load, on_unload, on_midi, set_param, get_param, get_error, render_block
 *
 * Note the render function is render_block here, not process_block. The v1
 * API is single-instance by construction, so one module-global instance
 * backs all of it.
 * ------------------------------------------------------------------------ */

/* ---------------------------------------------------------------------------
 * v1 entry point. The working audio FX on the device export BOTH this and v2:
 *
 *   clap.so:  T move_audio_fx_init_v1
 *             T move_audio_fx_init_v2
 *
 * Airwindows declares api_version 2 and still ships the v1 symbol, so the
 * chain host evidently looks for it. Mine exported only v2 -- which is the
 * difference that matters: the slot is listed from the manifest, then the
 * host goes to instantiate it, finds no v1 entry point, and declines silently.
 *
 * plugin_api_v1_t carries SEVEN callbacks and NO instance pointer, in this
 * order: api_version, on_load, on_unload, on_midi, set_param, get_param,
 * get_error, render_block. Note render_block here, not process_block.
 *
 * The v1 API is single-instance by construction, so one module-global
 * instance backs all of it.
 * ------------------------------------------------------------------------ */

static void *g_v1_instance = NULL;

static int v1_on_load(const char *module_dir, const char *json_defaults)
{
    g_v1_instance = v2_create_instance(module_dir, json_defaults);
    return g_v1_instance ? 0 : -1;
}

static void v1_on_unload(void)
{
    if (g_v1_instance) {
        v2_destroy_instance(g_v1_instance);
        g_v1_instance = NULL;
    }
}

static void v1_on_midi(const uint8_t *msg, int len, int source)
{
    (void)msg; (void)len; (void)source;
}

static void v1_set_param(const char *key, const char *val)
{
    v2_set_param(g_v1_instance, key, val);
}

static int v1_get_param(const char *key, char *buf, int buf_len)
{
    return v2_get_param(g_v1_instance, key, buf, buf_len);
}

static int v1_get_error(char *buf, int buf_len)
{
    if (!buf || buf_len <= 0) return 0;
    buf[0] = '\0';
    return 0;
}

static void v1_render_block(int16_t *out_interleaved_lr, int frames)
{
    v2_process_block(g_v1_instance, out_interleaved_lr, frames);
}

plugin_api_v1_t *move_audio_fx_init_v1(const host_api_v1_t *host)
{
    static plugin_api_v1_t api;
    (void)host;

    memset(&api, 0, sizeof(api));
    api.api_version  = 1;
    api.on_load      = v1_on_load;
    api.on_unload    = v1_on_unload;
    api.on_midi      = v1_on_midi;
    api.set_param    = v1_set_param;
    api.get_param    = v1_get_param;
    api.get_error    = v1_get_error;
    api.render_block = v1_render_block;
    return &api;
}
