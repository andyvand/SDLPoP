/* GBA sound bridge: replaces the audio entry points called by SDLPoP.
   The original implementations live in seg009.c and require AUDIO_S16SYS
   stereo @ 44.1 kHz mixing, plus OPL3/MIDI/OGG — all infeasible on a
   16.78 MHz ARM7. Here we ignore the SDL callback path completely and
   feed our DMA1 mixer directly. */
#include <gba.h>
#include <string.h>
#include <stdlib.h>
#include "common.h"
#include "gba_port.h"

extern int  gba_sfx_play(const int8_t* data, uint32_t length, int loop, uint8_t volume);
extern void gba_music_play(const int8_t* data, uint32_t length, int loop);
extern void gba_music_stop(void);
extern void gba_sfx_stop_all(void);
extern int  gba_sfx_any_playing(void);

/* Mirrors seg009.c state symbols so check_sound_playing() compiles.
   sound_names is owned by upstream data.c; do NOT redeclare it here. */
short speaker_playing  = 0;
short digi_playing     = 0;
short midi_playing     = 0;
short ogg_playing      = 0;
SDL_AudioSpec* digi_audiospec = NULL;
byte* digi_buffer = NULL;
byte* digi_remaining_pos = NULL;
int   digi_remaining_length = 0;
int   wave_version = -1;
int   digi_unavailable = 0;
const int digi_samplerate = 18157;
short square_wave_state = 4000;
float square_wave_samples_since_last_flip = 0.0f;
extern char** sound_names;

#ifndef __GBA__
extern int is_sound_on;
extern int enable_music;
#endif

void speaker_sound_stop(void) { gba_sfx_stop_all(); speaker_playing = 0; }
void stop_digi(void)          { gba_sfx_stop_all(); digi_playing = 0; }
void stop_ogg(void)           { gba_music_stop();   ogg_playing  = 0; }
void stop_midi(void)          { gba_music_stop();   midi_playing = 0; }

void stop_sounds(void) {
    gba_sfx_stop_all();
    gba_music_stop();
    speaker_playing = digi_playing = midi_playing = ogg_playing = 0;
}

void init_digi(void) {
    if (digi_audiospec != NULL) return;
    static SDL_AudioSpec spec;
    spec.freq     = digi_samplerate;
    spec.format   = AUDIO_S8;
    spec.channels = 1;
    spec.samples  = 304;
    spec.silence  = 0;
    spec.callback = NULL;
    spec.userdata = NULL;
    digi_audiospec = &spec;
    gba_audio_init(&spec);
}

void turn_music_on_off(byte on) { enable_music = on; if (!on) gba_music_stop(); }
void turn_sound_on_off(byte on) { is_sound_on = on; if (!on) gba_sfx_stop_all(); }
int  check_sound_playing(void)  { return gba_sfx_any_playing(); }

/* digi sound buffer: PoP 1.3+ wraps a digi_new_type whose samples are
   unsigned 8-bit.  tools/preproc_dat subtracts 128 from each sample at
   build time and stamps bit 15 of digi_new.unknown2 as a sentinel; the
   runtime simply hands the ROM pointer straight to the mixer.  Without
   pre-conversion we'd need a per-active-sound scratch (64 KB of EWRAM
   on the old design), so an unstamped buffer is dropped rather than
   played back inverted. */
void play_digi_sound(sound_buffer_type* buffer) {
    if (!buffer) return;
    init_digi();
    stop_digi();

    const int8_t* samples;
    uint32_t count;
    if ((buffer->type & 7) == sound_digi_converted) {
        samples = (const int8_t*)buffer->converted.samples;
        count   = (uint32_t)buffer->converted.length;
    } else if ((buffer->type & 7) == sound_digi) {
        uint16_t unk2 = SDL_SwapLE16(buffer->digi_new.unknown2);
        if (!(unk2 & 0x8000)) return;
        samples = (const int8_t*)buffer->digi_new.samples;
        count   = SDL_SwapLE16(buffer->digi_new.sample_count);
    } else {
        return;
    }
    digi_playing = 1;
    gba_sfx_play(samples, count, 0, 200);
}

void play_speaker_sound(sound_buffer_type* buffer) {
    (void)buffer; /* dropped on GBA — PC speaker square wave isn't worth the cost */
}

void play_midi_sound(sound_buffer_type* buffer) { (void)buffer; }
void play_ogg_sound (sound_buffer_type* buffer) { (void)buffer; }

void play_sound_from_buffer(sound_buffer_type* buffer) {
    if (!buffer) return;
    switch (buffer->type & 7) {
        case sound_digi:
        case sound_digi_converted:
            play_digi_sound(buffer);
            break;
        case sound_speaker:
        case sound_midi:
        case sound_ogg:
        default:
            /* silently dropped */
            break;
    }
}

void free_sound(sound_buffer_type* buffer) {
    if (!buffer) return;
    gba_free(buffer); /* safe on NULL / ROM / non-pool pointers */
}

void load_sound_names(void) {
    /* On GBA we don't read names.txt — music names list isn't used. */
}

/* Replacement for seg009.c's load_sound (which is inside #ifndef __GBA__).
   We do NOT copy samples into EWRAM: load_from_opendats_alloc() returns a
   pointer straight into cartridge ROM for DAT-resident resources (see the
   #ifdef __GBA__ path in seg009.c), and the digi samples were already
   converted to signed 8-bit (with the 0x8000 sentinel) at build time by
   tools/preproc_dat. So the returned ROM buffer is handed directly to the
   DMA mixer by play_digi_sound(). free_sound() no-ops on ROM pointers. */
extern void* load_from_opendats_alloc(int resource, const char* extension,
                                       data_location* out_result, int* out_size);
sound_buffer_type* load_sound(int index) {
    init_digi(); /* make sure the DMA mixer is running */
    return (sound_buffer_type*)load_from_opendats_alloc(index + 10000, "bin", NULL, NULL);
}

/* These were defined in seg009.c but are also referenced from elsewhere. */
void generate_square_wave(byte* s, float f, int n) { (void)s; (void)f; (void)n; }
void speaker_callback(void* u, Uint8* s, int n)    { (void)u; (void)s; (void)n; }
void digi_callback   (void* u, Uint8* s, int n)    { (void)u; (void)s; (void)n; }
void ogg_callback    (void* u, Uint8* s, int n)    { (void)u; (void)s; (void)n; }
void audio_callback  (void* u, Uint8* s, int n)    { (void)u; (void)s; (void)n; }
void midi_callback   (void* u, Uint8* s, int n)    { (void)u; (void)s; (void)n; }
void init_midi(void)                               { }
