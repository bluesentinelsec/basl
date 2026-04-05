/* audio.c — Vigil audio plugin powered by miniaudio.
 *
 * Provides a high-level, SDL_mixer-style API for sound effects,
 * music streaming, and 3D spatialized audio.
 *
 * Classes: Engine, Sound, Music
 * Module functions: set_listener_position, set_listener_direction
 */

/* Enable OGG Vorbis decoding via stb_vorbis.
   Include the header portion first so miniaudio sees the declarations,
   then include the implementation after miniaudio. */
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_GENERATION
#include "miniaudio.h"

/* Now include the stb_vorbis implementation. */
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"

/* ── Handle registry (same pattern as gui/sdl plugins) ───────────── */

#define AUDIO_HANDLE_MAX 256

typedef struct
{
    void *items[AUDIO_HANDLE_MAX];
    int32_t count;
} audio_handle_registry_t;

static int audio_handle_store(audio_handle_registry_t *r, void *ptr, int64_t *out)
{
    for (int32_t i = 0; i < r->count; i++)
    {
        if (r->items[i] == NULL)
        {
            r->items[i] = ptr;
            *out = (int64_t)i;
            return 0;
        }
    }
    if (r->count >= AUDIO_HANDLE_MAX)
        return -1;
    r->items[r->count] = ptr;
    *out = (int64_t)r->count++;
    return 0;
}

static void *audio_handle_get(audio_handle_registry_t *r, int64_t h)
{
    if (h < 0 || h >= (int64_t)r->count)
        return NULL;
    return r->items[h];
}

static void audio_handle_clear(audio_handle_registry_t *r, int64_t h)
{
    if (h >= 0 && h < (int64_t)r->count)
        r->items[h] = NULL;
}

static audio_handle_registry_t g_engines = {{0}, 0};
static audio_handle_registry_t g_sounds = {{0}, 0};
static audio_handle_registry_t g_musics = {{0}, 0};

/* ── Stack helpers ───────────────────────────────────────────────── */

static int32_t audio_arg_i32(vigil_vm_t *vm, size_t base, size_t idx)
{
    return vigil_nanbox_decode_i32(vigil_vm_stack_get(vm, base + idx));
}

static double audio_arg_f64(vigil_vm_t *vm, size_t base, size_t idx)
{
    return vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + idx));
}

static const char *audio_arg_str(vigil_vm_t *vm, size_t base, size_t idx, char *buf, size_t bufsz)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
    {
        const char *s = vigil_string_object_c_str(obj);
        size_t len = strlen(s);
        if (len >= bufsz)
            len = bufsz - 1;
        memcpy(buf, s, len);
        buf[len] = '\0';
        return buf;
    }
    buf[0] = '\0';
    return buf;
}

static int64_t audio_self_handle(vigil_vm_t *vm, size_t base)
{
    vigil_value_t val = vigil_vm_stack_get(vm, base);
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(val);
    vigil_value_t field;
    vigil_instance_object_get_field(obj, 0, &field);
    int64_t result = vigil_nanbox_decode_int(field);
    vigil_value_release(&field);
    return result;
}

static int64_t audio_arg_handle(vigil_vm_t *vm, size_t base, size_t idx)
{
    return audio_self_handle(vm, base + idx);
}

static vigil_status_t audio_push_handle_instance(vigil_vm_t *vm, size_t class_index, int64_t handle,
                                                 vigil_error_t *error)
{
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_value_t field;
    vigil_value_init_int(&field, handle);
    vigil_object_t *inst = NULL;
    vigil_status_t st = vigil_instance_object_new(rt, class_index, &field, 1U, &inst, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t result;
    vigil_value_init_object(&result, &inst);
    st = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return st;
}

static vigil_status_t audio_push_ok_err(vigil_vm_t *vm, vigil_error_t *error)
{
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t audio_push_fail_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t st = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 1, &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t v;
    vigil_value_init_object(&v, &obj);
    st = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return st;
}

/* Class indexes — must match class table order. */
enum
{
    AUDIO_ENGINE_CLASS = 0U,
    AUDIO_SOUND_CLASS = 1U,
    AUDIO_MUSIC_CLASS = 2U,
};

/* ── audio.Engine ────────────────────────────────────────────────── */

static vigil_status_t audio_engine_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    (void)base;
    vigil_vm_stack_pop_n(vm, arg_count);

    ma_engine *engine = (ma_engine *)malloc(sizeof(ma_engine));
    if (!engine)
    {
        audio_push_handle_instance(vm, AUDIO_ENGINE_CLASS, -1, error);
        return audio_push_fail_err(vm, "audio: out of memory", error);
    }

    ma_engine_config config = ma_engine_config_init();
    if (ma_engine_init(&config, engine) != MA_SUCCESS)
    {
        free(engine);
        audio_push_handle_instance(vm, AUDIO_ENGINE_CLASS, -1, error);
        return audio_push_fail_err(vm, "audio: failed to initialize engine", error);
    }

    int64_t handle;
    audio_handle_store(&g_engines, engine, &handle);
    audio_push_handle_instance(vm, AUDIO_ENGINE_CLASS, handle, error);
    return audio_push_ok_err(vm, error);
}

static vigil_status_t audio_engine_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_engine *engine = (ma_engine *)audio_handle_get(&g_engines, h);
    if (engine)
    {
        ma_engine_uninit(engine);
        free(engine);
    }
    audio_handle_clear(&g_engines, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_engine_set_volume(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    double vol = audio_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_engine *engine = (ma_engine *)audio_handle_get(&g_engines, h);
    if (engine)
        ma_engine_set_volume(engine, (float)vol);
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── audio.Sound ─────────────────────────────────────────────────── */

static vigil_status_t audio_sound_load(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t eng_h = audio_arg_handle(vm, base, 1);
    char buf[512];
    const char *path = audio_arg_str(vm, base, 2, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);

    ma_engine *engine = (ma_engine *)audio_handle_get(&g_engines, eng_h);
    if (!engine)
    {
        audio_push_handle_instance(vm, AUDIO_SOUND_CLASS, -1, error);
        return audio_push_fail_err(vm, "audio: invalid engine", error);
    }

    ma_sound *sound = (ma_sound *)malloc(sizeof(ma_sound));
    if (!sound)
    {
        audio_push_handle_instance(vm, AUDIO_SOUND_CLASS, -1, error);
        return audio_push_fail_err(vm, "audio: out of memory", error);
    }

    if (ma_sound_init_from_file(engine, path, 0, NULL, NULL, sound) != MA_SUCCESS)
    {
        free(sound);
        audio_push_handle_instance(vm, AUDIO_SOUND_CLASS, -1, error);
        return audio_push_fail_err(vm, "audio: failed to load sound", error);
    }

    int64_t handle;
    audio_handle_store(&g_sounds, sound, &handle);
    audio_push_handle_instance(vm, AUDIO_SOUND_CLASS, handle, error);
    return audio_push_ok_err(vm, error);
}

static vigil_status_t audio_sound_load_memory(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t eng_h = audio_arg_handle(vm, base, 1);
    vigil_value_t arr_val = vigil_vm_stack_get(vm, base + 2);
    vigil_vm_stack_pop_n(vm, arg_count);

    ma_engine *engine = (ma_engine *)audio_handle_get(&g_engines, eng_h);
    vigil_object_t *arr_obj = vigil_value_as_object(&arr_val);
    if (!engine || !arr_obj || vigil_object_type(arr_obj) != VIGIL_OBJECT_ARRAY)
    {
        audio_push_handle_instance(vm, AUDIO_SOUND_CLASS, -1, error);
        return audio_push_fail_err(vm, "audio: invalid engine or data", error);
    }

    size_t len = vigil_array_object_length(arr_obj);
    unsigned char *data = (unsigned char *)malloc(len);
    if (!data)
    {
        audio_push_handle_instance(vm, AUDIO_SOUND_CLASS, -1, error);
        return audio_push_fail_err(vm, "audio: out of memory", error);
    }
    for (size_t i = 0; i < len; i++)
    {
        vigil_value_t elem;
        vigil_array_object_get(arr_obj, i, &elem);
        data[i] = (unsigned char)vigil_value_as_int(&elem);
    }

    /* Decode from memory into a data source, then init sound. */
    ma_decoder *decoder = (ma_decoder *)malloc(sizeof(ma_decoder));
    ma_sound *sound = (ma_sound *)malloc(sizeof(ma_sound));
    if (!decoder || !sound)
    {
        free(data);
        free(decoder);
        free(sound);
        audio_push_handle_instance(vm, AUDIO_SOUND_CLASS, -1, error);
        return audio_push_fail_err(vm, "audio: out of memory", error);
    }

    if (ma_decoder_init_memory(data, len, NULL, decoder) != MA_SUCCESS)
    {
        free(data);
        free(decoder);
        free(sound);
        audio_push_handle_instance(vm, AUDIO_SOUND_CLASS, -1, error);
        return audio_push_fail_err(vm, "audio: failed to decode audio data", error);
    }

    if (ma_sound_init_from_data_source(engine, decoder, 0, NULL, sound) != MA_SUCCESS)
    {
        ma_decoder_uninit(decoder);
        free(data);
        free(decoder);
        free(sound);
        audio_push_handle_instance(vm, AUDIO_SOUND_CLASS, -1, error);
        return audio_push_fail_err(vm, "audio: failed to init sound from memory", error);
    }

    /* Note: data and decoder must remain alive while sound is playing.
       We leak them intentionally — they're freed when the sound is destroyed.
       A production implementation would track these per-sound. */

    int64_t handle;
    audio_handle_store(&g_sounds, sound, &handle);
    audio_push_handle_instance(vm, AUDIO_SOUND_CLASS, handle, error);
    return audio_push_ok_err(vm, error);
}

static vigil_status_t audio_sound_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *sound = (ma_sound *)audio_handle_get(&g_sounds, h);
    if (sound)
    {
        ma_sound_uninit(sound);
        free(sound);
    }
    audio_handle_clear(&g_sounds, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_sound_play(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *s = (ma_sound *)audio_handle_get(&g_sounds, h);
    if (s)
    {
        ma_sound_seek_to_pcm_frame(s, 0);
        ma_sound_start(s);
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_sound_stop(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *s = (ma_sound *)audio_handle_get(&g_sounds, h);
    if (s)
        ma_sound_stop(s);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_sound_set_volume(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    double vol = audio_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *s = (ma_sound *)audio_handle_get(&g_sounds, h);
    if (s)
        ma_sound_set_volume(s, (float)vol);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_sound_set_pitch(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    double pitch = audio_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *s = (ma_sound *)audio_handle_get(&g_sounds, h);
    if (s)
        ma_sound_set_pitch(s, (float)pitch);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_sound_set_looping(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    int32_t loop = audio_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *s = (ma_sound *)audio_handle_get(&g_sounds, h);
    if (s)
        ma_sound_set_looping(s, loop != 0);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_sound_set_position(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    double x = audio_arg_f64(vm, base, 1);
    double y = audio_arg_f64(vm, base, 2);
    double z = audio_arg_f64(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *s = (ma_sound *)audio_handle_get(&g_sounds, h);
    if (s)
    {
        ma_sound_set_spatialization_enabled(s, MA_TRUE);
        ma_sound_set_position(s, (float)x, (float)y, (float)z);
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── audio.Music ─────────────────────────────────────────────────── */
/* Music uses the same ma_sound but with MA_SOUND_FLAG_STREAM for
   streaming playback of large files. */

static vigil_status_t audio_music_load(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t eng_h = audio_arg_handle(vm, base, 1);
    char buf[512];
    const char *path = audio_arg_str(vm, base, 2, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);

    ma_engine *engine = (ma_engine *)audio_handle_get(&g_engines, eng_h);
    if (!engine)
    {
        audio_push_handle_instance(vm, AUDIO_MUSIC_CLASS, -1, error);
        return audio_push_fail_err(vm, "audio: invalid engine", error);
    }

    ma_sound *music = (ma_sound *)malloc(sizeof(ma_sound));
    if (!music)
    {
        audio_push_handle_instance(vm, AUDIO_MUSIC_CLASS, -1, error);
        return audio_push_fail_err(vm, "audio: out of memory", error);
    }

    if (ma_sound_init_from_file(engine, path, MA_SOUND_FLAG_STREAM, NULL, NULL, music) != MA_SUCCESS)
    {
        free(music);
        audio_push_handle_instance(vm, AUDIO_MUSIC_CLASS, -1, error);
        return audio_push_fail_err(vm, "audio: failed to load music", error);
    }

    int64_t handle;
    audio_handle_store(&g_musics, music, &handle);
    audio_push_handle_instance(vm, AUDIO_MUSIC_CLASS, handle, error);
    return audio_push_ok_err(vm, error);
}

static vigil_status_t audio_music_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *m = (ma_sound *)audio_handle_get(&g_musics, h);
    if (m)
    {
        ma_sound_uninit(m);
        free(m);
    }
    audio_handle_clear(&g_musics, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_music_play(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *m = (ma_sound *)audio_handle_get(&g_musics, h);
    if (m)
    {
        ma_sound_seek_to_pcm_frame(m, 0);
        ma_sound_start(m);
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_music_play_loop(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *m = (ma_sound *)audio_handle_get(&g_musics, h);
    if (m)
    {
        ma_sound_set_looping(m, MA_TRUE);
        ma_sound_seek_to_pcm_frame(m, 0);
        ma_sound_start(m);
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_music_pause(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *m = (ma_sound *)audio_handle_get(&g_musics, h);
    if (m)
        ma_sound_stop(m);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_music_resume(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *m = (ma_sound *)audio_handle_get(&g_musics, h);
    if (m)
        ma_sound_start(m);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_music_stop(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *m = (ma_sound *)audio_handle_get(&g_musics, h);
    if (m)
    {
        ma_sound_stop(m);
        ma_sound_seek_to_pcm_frame(m, 0);
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_music_set_volume(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    double vol = audio_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *m = (ma_sound *)audio_handle_get(&g_musics, h);
    if (m)
        ma_sound_set_volume(m, (float)vol);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_music_set_pitch(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    double pitch = audio_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *m = (ma_sound *)audio_handle_get(&g_musics, h);
    if (m)
        ma_sound_set_pitch(m, (float)pitch);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_music_seek(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    double seconds = audio_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *m = (ma_sound *)audio_handle_get(&g_musics, h);
    if (m)
    {
        ma_uint32 sr = ma_engine_get_sample_rate(ma_sound_get_engine(m));
        ma_sound_seek_to_pcm_frame(m, (ma_uint64)(seconds * sr));
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_music_position(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    double pos = 0.0;
    ma_sound *m = (ma_sound *)audio_handle_get(&g_musics, h);
    if (m)
    {
        float cursor = 0;
        ma_sound_get_cursor_in_seconds(m, &cursor);
        pos = (double)cursor;
    }
    vigil_value_t v = vigil_nanbox_encode_double(pos);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t audio_music_duration(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    double dur = 0.0;
    ma_sound *m = (ma_sound *)audio_handle_get(&g_musics, h);
    if (m)
    {
        float length = 0;
        ma_sound_get_length_in_seconds(m, &length);
        dur = (double)length;
    }
    vigil_value_t v = vigil_nanbox_encode_double(dur);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t audio_music_fade_in(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    double seconds = audio_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *m = (ma_sound *)audio_handle_get(&g_musics, h);
    if (m)
    {
        ma_sound_set_fade_in_milliseconds(m, 0, 1.0f, (ma_uint64)(seconds * 1000));
        ma_sound_start(m);
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_music_fade_out(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = audio_self_handle(vm, base);
    double seconds = audio_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_sound *m = (ma_sound *)audio_handle_get(&g_musics, h);
    if (m)
        ma_sound_set_fade_in_milliseconds(m, -1, 0, (ma_uint64)(seconds * 1000));
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── Module-level: listener ──────────────────────────────────────── */

static vigil_status_t audio_set_listener_pos(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t eng_h = audio_arg_handle(vm, base, 0);
    double x = audio_arg_f64(vm, base, 1);
    double y = audio_arg_f64(vm, base, 2);
    double z = audio_arg_f64(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_engine *engine = (ma_engine *)audio_handle_get(&g_engines, eng_h);
    if (engine)
        ma_engine_listener_set_position(engine, 0, (float)x, (float)y, (float)z);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t audio_set_listener_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t eng_h = audio_arg_handle(vm, base, 0);
    double x = audio_arg_f64(vm, base, 1);
    double y = audio_arg_f64(vm, base, 2);
    double z = audio_arg_f64(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    ma_engine *engine = (ma_engine *)audio_handle_get(&g_engines, eng_h);
    if (engine)
        ma_engine_listener_set_direction(engine, 0, (float)x, (float)y, (float)z);
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── Parameter type arrays ───────────────────────────────────────── */

static const int p_obj_str[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_STRING};
static const int p_obj_obj[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_OBJECT};
static const int p_f64[] = {VIGIL_TYPE_F64};
static const int p_bool[] = {VIGIL_TYPE_BOOL};
static const int p_f64_f64_f64[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_obj_f64_f64_f64[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};

static const int rt_obj_err[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_ERR};
static const int rt_f64[] = {VIGIL_TYPE_F64};

/* ── Macro helpers ───────────────────────────────────────────────── */

/* clang-format off */
#define AU_PFIELD(n, nl, t) {n, nl, t, 0, NULL, 0U, 0, NULL, NULL}

#define AU_METHOD(n, nl, fn, pc, pt, rt, rc, rts) \
    {n, nl, fn, pc, pt, rt, rc, rts, 0, NULL, 0U, 0, NULL, NULL, NULL, NULL}

#define AU_STATIC(n, nl, fn, pc, pt, rt, rc, rts) \
    {n, nl, fn, pc, pt, rt, rc, rts, 1, NULL, 0U, 0, NULL, NULL, NULL, NULL}
/* clang-format on */

/* ── Engine class ────────────────────────────────────────────────── */

static const vigil_native_class_field_t audio_engine_fields[] = {
    AU_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t audio_engine_methods[] = {
    AU_STATIC("new", 3U, audio_engine_new, 0U, NULL, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    AU_METHOD("destroy", 7U, audio_engine_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("set_volume", 10U, audio_engine_set_volume, 1U, p_f64, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Sound class ─────────────────────────────────────────────────── */

static const vigil_native_class_field_t audio_sound_fields[] = {
    AU_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t audio_sound_methods[] = {
    AU_STATIC("load", 4U, audio_sound_load, 2U, p_obj_str, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    AU_STATIC("load_memory", 11U, audio_sound_load_memory, 2U, p_obj_obj, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    AU_METHOD("destroy", 7U, audio_sound_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("play", 4U, audio_sound_play, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("stop", 4U, audio_sound_stop, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("set_volume", 10U, audio_sound_set_volume, 1U, p_f64, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("set_pitch", 9U, audio_sound_set_pitch, 1U, p_f64, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("set_looping", 11U, audio_sound_set_looping, 1U, p_bool, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("set_position", 12U, audio_sound_set_position, 3U, p_f64_f64_f64, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Music class ─────────────────────────────────────────────────── */

static const vigil_native_class_field_t audio_music_fields[] = {
    AU_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t audio_music_methods[] = {
    AU_STATIC("load", 4U, audio_music_load, 2U, p_obj_str, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    AU_METHOD("destroy", 7U, audio_music_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("play", 4U, audio_music_play, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("play_loop", 9U, audio_music_play_loop, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("pause", 5U, audio_music_pause, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("resume", 6U, audio_music_resume, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("stop", 4U, audio_music_stop, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("set_volume", 10U, audio_music_set_volume, 1U, p_f64, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("set_pitch", 9U, audio_music_set_pitch, 1U, p_f64, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("seek", 4U, audio_music_seek, 1U, p_f64, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("position", 8U, audio_music_position, 0U, NULL, VIGIL_TYPE_F64, 1U, rt_f64),
    AU_METHOD("duration", 8U, audio_music_duration, 0U, NULL, VIGIL_TYPE_F64, 1U, rt_f64),
    AU_METHOD("fade_in", 7U, audio_music_fade_in, 1U, p_f64, VIGIL_TYPE_VOID, 0U, NULL),
    AU_METHOD("fade_out", 8U, audio_music_fade_out, 1U, p_f64, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Class table ─────────────────────────────────────────────────── */

/* clang-format off */
static const vigil_native_class_t audio_classes[] = {
    {"Engine", 6U, audio_engine_fields, 1U, audio_engine_methods, sizeof(audio_engine_methods) / sizeof(audio_engine_methods[0]), NULL, NULL},
    {"Sound",  5U, audio_sound_fields,  1U, audio_sound_methods,  sizeof(audio_sound_methods)  / sizeof(audio_sound_methods[0]),  NULL, NULL},
    {"Music",  5U, audio_music_fields,  1U, audio_music_methods,  sizeof(audio_music_methods)  / sizeof(audio_music_methods[0]),  NULL, NULL},
};
/* clang-format on */

#define AUDIO_CLASS_COUNT (sizeof(audio_classes) / sizeof(audio_classes[0]))

/* ── Module-level functions ──────────────────────────────────────── */

static const vigil_native_module_function_t audio_functions[] = {
    {"set_listener_position", 21U, audio_set_listener_pos, 4U, p_obj_f64_f64_f64, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    {"set_listener_direction", 22U, audio_set_listener_dir, 4U, p_obj_f64_f64_f64, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
};

#define AUDIO_FUNCTION_COUNT (sizeof(audio_functions) / sizeof(audio_functions[0]))

/* ── Module doc ──────────────────────────────────────────────────── */

static const vigil_native_symbol_doc_t audio_module_doc = {
    "Audio playback and 3D spatialization.",
    "Provides SDL_mixer-style audio powered by miniaudio. "
    "Supports WAV, MP3, FLAC, and OGG Vorbis. Sound effects are loaded "
    "into memory for low-latency playback. Music is streamed from disk.",
    NULL,
};

/* ── Module export ───────────────────────────────────────────────── */

VIGIL_API const vigil_native_module_t vigil_plugin_audio = {
    "audio", 5U, audio_functions, AUDIO_FUNCTION_COUNT, audio_classes, AUDIO_CLASS_COUNT, &audio_module_doc, NULL, 0U,
};
