#include "settings.h"

typedef struct EqPreset {
    const char *name;
    int bass_t;
    int mid_t;
    int treble_t;
} EqPreset;

static const EqPreset presets[EQ_PRESET_COUNT] = {
    { "Flat",        10, 10, 10 },
    { "Bass Boost",  16, 10, 10 },
    { "Bass+Treble", 15,  9, 14 },
    { "Vocal",        8, 14, 11 },
    { "Rock",        14,  9, 13 },
    { "Custom",      10, 10, 10 } /* band values unused: Custom keeps current */
};

void settings_init(Settings *s)
{
    s->cursor = SETTINGS_ITEM_REPEAT;
    s->repeat_mode = 1;        /* REPEAT_ALL */
    s->favorites_only = 0;
    s->volume_step = 2;
    s->preset = EQ_PRESET_FLAT;
    s->bass_t = 10;
    s->mid_t = 10;
    s->treble_t = 10;
    s->rva = 0;
    s->debug = 0;
}

void settings_cursor_move(Settings *s, int direction)
{
    s->cursor += direction;
    if (s->cursor < 0) {
        s->cursor = SETTINGS_ITEM_COUNT - 1;
    } else if (s->cursor >= SETTINGS_ITEM_COUNT) {
        s->cursor = 0;
    }
}

int settings_clamp_band(int value)
{
    if (value < EQ_BAND_MIN) {
        return EQ_BAND_MIN;
    }
    if (value > EQ_BAND_MAX) {
        return EQ_BAND_MAX;
    }
    return value;
}

int settings_clamp_volume_step(int value)
{
    if (value < VOLUME_STEP_MIN) {
        return VOLUME_STEP_MIN;
    }
    if (value > VOLUME_STEP_MAX) {
        return VOLUME_STEP_MAX;
    }
    return value;
}

void settings_set_preset(Settings *s, int preset)
{
    if (preset < 0 || preset >= EQ_PRESET_COUNT) {
        preset = EQ_PRESET_FLAT;
    }
    s->preset = preset;
    if (preset != EQ_PRESET_CUSTOM) {
        s->bass_t = presets[preset].bass_t;
        s->mid_t = presets[preset].mid_t;
        s->treble_t = presets[preset].treble_t;
    }
}

static int adjust_band(Settings *s, int *band, int direction)
{
    int next = settings_clamp_band(*band + direction);

    if (next == *band) {
        return 0;
    }
    *band = next;
    s->preset = EQ_PRESET_CUSTOM;
    return 1;
}

int settings_adjust(Settings *s, int direction)
{
    switch (s->cursor) {
    case SETTINGS_ITEM_REPEAT:
        s->repeat_mode = (s->repeat_mode + direction + 3) % 3;
        return 1;
    case SETTINGS_ITEM_FAVORITES_ONLY:
        s->favorites_only = !s->favorites_only;
        return 1;
    case SETTINGS_ITEM_VOLUME_STEP:
    {
        int next = settings_clamp_volume_step(s->volume_step + direction);
        if (next == s->volume_step) {
            return 0;
        }
        s->volume_step = next;
        return 1;
    }
    case SETTINGS_ITEM_PRESET: {
        int next = s->preset + direction;
        if (next < 0) {
            next = EQ_PRESET_COUNT - 1;
        } else if (next >= EQ_PRESET_COUNT) {
            next = 0;
        }
        settings_set_preset(s, next);
        return 1;
    }
    case SETTINGS_ITEM_BASS:
        return adjust_band(s, &s->bass_t, direction);
    case SETTINGS_ITEM_MID:
        return adjust_band(s, &s->mid_t, direction);
    case SETTINGS_ITEM_TREBLE:
        return adjust_band(s, &s->treble_t, direction);
    case SETTINGS_ITEM_RVA:
        s->rva = !s->rva;
        return 1;
    case SETTINGS_ITEM_DEBUG:
        s->debug = !s->debug;
        return 1;
    default:
        return 0;
    }
}

const char *settings_preset_name(int preset)
{
    if (preset < 0 || preset >= EQ_PRESET_COUNT) {
        return "Flat";
    }
    return presets[preset].name;
}

const char *settings_repeat_name(int mode)
{
    switch (mode) {
    case 0:  return "Off";
    case 1:  return "All";
    case 2:  return "One";
    default: return "All";
    }
}

const char *settings_item_name(int item)
{
    switch (item) {
    case SETTINGS_ITEM_REPEAT:
        return "Repeat";
    case SETTINGS_ITEM_FAVORITES_ONLY:
        return "Favorites only";
    case SETTINGS_ITEM_VOLUME_STEP:
        return "Volume step";
    case SETTINGS_ITEM_PRESET:
        return "Preset";
    case SETTINGS_ITEM_BASS:
        return "Bass";
    case SETTINGS_ITEM_MID:
        return "Mid";
    case SETTINGS_ITEM_TREBLE:
        return "Treble";
    case SETTINGS_ITEM_RVA:
        return "Normalize (RVA)";
    case SETTINGS_ITEM_DEBUG:
        return "Debug logging";
    default:
        return "";
    }
}

const char *settings_section_name(int item)
{
    if (item <= SETTINGS_ITEM_VOLUME_STEP) {
        return "Playback";
    }
    if (item <= SETTINGS_ITEM_RVA) {
        return "Equalizer";
    }
    return "System";
}

int settings_section_start(int item)
{
    return item == SETTINGS_ITEM_REPEAT
        || item == SETTINGS_ITEM_PRESET
        || item == SETTINGS_ITEM_DEBUG;
}
