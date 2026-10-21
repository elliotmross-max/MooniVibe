/* moonivibe_meta.h -- MooniVibe: chain_params and ui_hierarchy.
 *
 * GENERATED FROM ONE PARAMETER TABLE so the two declarations cannot drift.
 * An earlier revision had this list duplicated inside the string -- 28 entries
 * where there are 15 -- because the JSON was patched in place, and a host
 * indexing a doubled array found "stages" declared twice. That is why the
 * Stage label froze while the audio kept responding.
 *
 * The knob row holds EIGHT: the Move's grid. The six you play with live there
 * and the circuit selectors are reachable through the parameter menu.
 */

#ifndef MOONIVIBE_META_H
#define MOONIVIBE_META_H

static const char *MOONIVIBE_CHAIN_PARAMS =
"[" "\n"
  "{\"key\":\"speed\",\"name\":\"Speed\",\"type\":\"float\",\"min\":0.02,\"max\":12,\"default\":0.6,\"step\":0.01,\"unit\":\"Hz\"},"
  "{\"key\":\"intensity\",\"name\":\"Intensity\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.65,\"step\":0.01},"
  "{\"key\":\"mode\",\"name\":\"Mode\",\"type\":\"enum\",\"options\":[\"Vibrato\",\"Chorus\"],\"default\":\"Chorus\"},"
  "{\"key\":\"mix\",\"name\":\"Mix\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.75,\"step\":0.01},"
  "{\"key\":\"output\",\"name\":\"Output\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.7,\"step\":0.01},"
  "{\"key\":\"lamp_lfo\",\"name\":\"Lamp LFO\",\"type\":\"float\",\"min\":0,\"max\":8,\"default\":0.17,\"step\":0.01},"
  "{\"key\":\"lamp_bias\",\"name\":\"Lamp Bias\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.5,\"step\":0.01},"
  "{\"key\":\"cap\",\"name\":\"Capacitor\",\"type\":\"enum\",\"options\":[\"Stock 220n\",\"Dark 100n\",\"Mid 47n\",\"Bright 22n\"],\"default\":\"Stock 220n\"},"
  "{\"key\":\"lamp\",\"name\":\"Lamp\",\"type\":\"enum\",\"options\":[\"Matched\",\"Stock\",\"Wide\",\"Drift\"],\"default\":\"Stock\"},"
  "{\"key\":\"drive\",\"name\":\"Drive Res\",\"type\":\"enum\",\"options\":[\"Clean 100k\",\"Stock 47k\",\"Hot 22k\",\"Slam 10k\"],\"default\":\"Stock 47k\"},"
  "{\"key\":\"diode\",\"name\":\"Diodes\",\"type\":\"enum\",\"options\":[\"None\",\"Silicon\",\"Germanium\",\"LED\"],\"default\":\"None\"},"
  "{\"key\":\"stages\",\"name\":\"Stages\",\"type\":\"enum\",\"options\":[\"4 stages\",\"6 stages\"],\"default\":\"4 stages\"},"
  "{\"key\":\"lfo_shape\",\"name\":\"LFO Shape\",\"type\":\"enum\",\"options\":[\"Sine\",\"Triangle\",\"Soft Square\"],\"default\":\"Sine\"},"
  "{\"key\":\"spread\",\"name\":\"Stereo Spread\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.25,\"step\":0.01},"
  "{\"key\":\"mismatch\",\"name\":\"Cell Mismatch\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":1,\"step\":0.01}"
"\n" "]";

static const char *MOONIVIBE_UI_HIERARCHY =
"{"
  "\"modes\":null,"
  "\"levels\":{"
    "\"root\":{"
      "\"name\":\"MooniVibe\","
      "\"knobs\":[\"speed\",\"intensity\",\"mix\",\"output\",\"lamp_lfo\",\"lamp_bias\",\"cap\",\"lamp\"],"
      "\"params\":[" "\n"
        "{\"key\":\"speed\",\"label\":\"Speed\",\"type\":\"float\",\"min\":0.02,\"max\":12,\"default\":0.6,\"step\":0.01,\"unit\":\"Hz\"},"
        "{\"key\":\"intensity\",\"label\":\"Intensity\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.65,\"step\":0.01},"
        "{\"key\":\"mode\",\"label\":\"Mode\",\"type\":\"enum\",\"options\":[\"Vibrato\",\"Chorus\"],\"default\":\"Chorus\"},"
        "{\"key\":\"mix\",\"label\":\"Mix\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.75,\"step\":0.01},"
        "{\"key\":\"output\",\"label\":\"Output\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.7,\"step\":0.01},"
        "{\"key\":\"lamp_lfo\",\"label\":\"Lamp LFO\",\"type\":\"float\",\"min\":0,\"max\":8,\"default\":0.17,\"step\":0.01},"
        "{\"key\":\"lamp_bias\",\"label\":\"Lamp Bias\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.5,\"step\":0.01},"
        "{\"key\":\"cap\",\"label\":\"Capacitor\",\"type\":\"enum\",\"options\":[\"Stock 220n\",\"Dark 100n\",\"Mid 47n\",\"Bright 22n\"],\"default\":\"Stock 220n\"},"
        "{\"key\":\"lamp\",\"label\":\"Lamp\",\"type\":\"enum\",\"options\":[\"Matched\",\"Stock\",\"Wide\",\"Drift\"],\"default\":\"Stock\"},"
        "{\"key\":\"drive\",\"label\":\"Drive Res\",\"type\":\"enum\",\"options\":[\"Clean 100k\",\"Stock 47k\",\"Hot 22k\",\"Slam 10k\"],\"default\":\"Stock 47k\"},"
        "{\"key\":\"diode\",\"label\":\"Diodes\",\"type\":\"enum\",\"options\":[\"None\",\"Silicon\",\"Germanium\",\"LED\"],\"default\":\"None\"},"
        "{\"key\":\"stages\",\"label\":\"Stages\",\"type\":\"enum\",\"options\":[\"4 stages\",\"6 stages\"],\"default\":\"4 stages\"},"
        "{\"key\":\"lfo_shape\",\"label\":\"LFO Shape\",\"type\":\"enum\",\"options\":[\"Sine\",\"Triangle\",\"Soft Square\"],\"default\":\"Sine\"},"
        "{\"key\":\"spread\",\"label\":\"Stereo Spread\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.25,\"step\":0.01},"
        "{\"key\":\"mismatch\",\"label\":\"Cell Mismatch\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":1,\"step\":0.01}"
"\n" "      ]"
    "}"
  "}"
"}";

#endif /* MOONIVIBE_META_H */
