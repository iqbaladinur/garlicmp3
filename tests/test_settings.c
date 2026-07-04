#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "settings.h"

static void test_init_defaults(void)
{
    Settings s;

    settings_init(&s);
    assert(s.cursor == SETTINGS_ITEM_PRESET);
    assert(s.preset == EQ_PRESET_FLAT);
    assert(s.bass_t == 10 && s.mid_t == 10 && s.treble_t == 10);
    assert(s.rva == 0);
}

static void test_cursor_wraps(void)
{
    Settings s;

    settings_init(&s);
    settings_cursor_move(&s, -1);
    assert(s.cursor == SETTINGS_ITEM_COUNT - 1);
    settings_cursor_move(&s, 1);
    assert(s.cursor == SETTINGS_ITEM_PRESET);
}

static void test_preset_cycle_loads_values(void)
{
    Settings s;

    settings_init(&s);
    s.cursor = SETTINGS_ITEM_PRESET;
    assert(settings_adjust(&s, 1) == 1);
    assert(s.preset == EQ_PRESET_BASS_BOOST);
    assert(s.bass_t == 16 && s.mid_t == 10 && s.treble_t == 10);
    /* cycling backwards from Flat wraps to Custom */
    settings_init(&s);
    s.cursor = SETTINGS_ITEM_PRESET;
    assert(settings_adjust(&s, -1) == 1);
    assert(s.preset == EQ_PRESET_CUSTOM);
}

static void test_custom_preset_keeps_values(void)
{
    Settings s;

    settings_init(&s);
    s.bass_t = 17;
    settings_set_preset(&s, EQ_PRESET_CUSTOM);
    assert(s.bass_t == 17);
    settings_set_preset(&s, EQ_PRESET_ROCK);
    assert(s.bass_t == 14 && s.mid_t == 9 && s.treble_t == 13);
}

static void test_manual_band_switches_to_custom(void)
{
    Settings s;

    settings_init(&s);
    s.cursor = SETTINGS_ITEM_BASS;
    assert(settings_adjust(&s, 1) == 1);
    assert(s.bass_t == 11);
    assert(s.preset == EQ_PRESET_CUSTOM);
}

static void test_band_clamps(void)
{
    Settings s;

    settings_init(&s);
    s.cursor = SETTINGS_ITEM_TREBLE;
    s.treble_t = EQ_BAND_MAX;
    assert(settings_adjust(&s, 1) == 0);
    assert(s.treble_t == EQ_BAND_MAX);
    s.treble_t = EQ_BAND_MIN;
    assert(settings_adjust(&s, -1) == 0);
    assert(s.treble_t == EQ_BAND_MIN);
    assert(settings_clamp_band(99) == EQ_BAND_MAX);
    assert(settings_clamp_band(-3) == EQ_BAND_MIN);
    assert(settings_clamp_band(12) == 12);
}

static void test_rva_toggle(void)
{
    Settings s;

    settings_init(&s);
    s.cursor = SETTINGS_ITEM_RVA;
    assert(settings_adjust(&s, 1) == 1);
    assert(s.rva == 1);
    assert(settings_adjust(&s, -1) == 1);
    assert(s.rva == 0);
}

static void test_names(void)
{
    assert(strcmp(settings_preset_name(EQ_PRESET_FLAT), "Flat") == 0);
    assert(strcmp(settings_preset_name(EQ_PRESET_CUSTOM), "Custom") == 0);
    assert(strcmp(settings_item_name(SETTINGS_ITEM_RVA), "Normalize (RVA)") == 0);
}

int main(void)
{
    test_init_defaults();
    test_cursor_wraps();
    test_preset_cycle_loads_values();
    test_custom_preset_keeps_values();
    test_manual_band_switches_to_custom();
    test_band_clamps();
    test_rva_toggle();
    test_names();
    printf("test_settings OK\n");
    return 0;
}
