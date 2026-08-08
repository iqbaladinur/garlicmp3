#ifndef SETTINGS_H
#define SETTINGS_H

enum {
    /* Playback */
    SETTINGS_ITEM_REPEAT = 0,
    SETTINGS_ITEM_FAVORITES_ONLY,
    SETTINGS_ITEM_VOLUME_STEP,
    /* Equalizer */
    SETTINGS_ITEM_PRESET,
    SETTINGS_ITEM_BASS,
    SETTINGS_ITEM_MID,
    SETTINGS_ITEM_TREBLE,
    SETTINGS_ITEM_RVA,
    /* System */
    SETTINGS_ITEM_DEBUG,
    SETTINGS_ITEM_COUNT
};

enum {
    EQ_PRESET_FLAT = 0,
    EQ_PRESET_BASS_BOOST,
    EQ_PRESET_BASS_TREBLE,
    EQ_PRESET_VOCAL,
    EQ_PRESET_ROCK,
    EQ_PRESET_CUSTOM,
    EQ_PRESET_COUNT
};

/* Band values are tenths of the mpg123 SEQ multiplier: 10 = 1.0 = flat. */
#define EQ_BAND_MIN 5
#define EQ_BAND_MAX 20

#define VOLUME_STEP_MIN 1
#define VOLUME_STEP_MAX 20

typedef struct Settings {
    int cursor;   /* SETTINGS_ITEM_* */
    /* Playback */
    int repeat_mode;     /* 0=off 1=all 2=one */
    int favorites_only;  /* 0/1 */
    int volume_step;     /* 1-20 */
    /* Equalizer */
    int preset;   /* EQ_PRESET_* */
    int bass_t;
    int mid_t;
    int treble_t;
    int rva;      /* 0/1 */
    /* System */
    int debug;    /* 0/1 */
} Settings;

void settings_init(Settings *s);
void settings_cursor_move(Settings *s, int direction);
int settings_adjust(Settings *s, int direction);
void settings_set_preset(Settings *s, int preset);
int settings_clamp_band(int value);
int settings_clamp_volume_step(int value);
const char *settings_preset_name(int preset);
const char *settings_repeat_name(int mode);
const char *settings_item_name(int item);
const char *settings_section_name(int item);
int settings_section_start(int item);

#endif
