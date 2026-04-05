/* audio_test.c — Unit tests for the audio plugin. */
// NOLINTBEGIN(readability-function-cognitive-complexity)
#include "vigil_test.h"

#include <string.h>

#include "vigil/native_module.h"

#include "plugin_registry.h"

static int audio_plugin_available(void)
{
    return vigil_plugin_is_known_module("audio", 5U);
}

static const vigil_native_module_t *get_audio_module(vigil_native_registry_t *natives, vigil_error_t *error)
{
    vigil_native_registry_init(natives);
    if (vigil_plugin_register_all(natives, error) != VIGIL_STATUS_OK)
        return NULL;
    return vigil_native_registry_find(natives, "audio", 5U);
}

static const vigil_native_class_t *find_class(const vigil_native_module_t *mod, const char *name)
{
    for (size_t i = 0; i < mod->class_count; i++)
        if (strcmp(mod->classes[i].name, name) == 0)
            return &mod->classes[i];
    return NULL;
}

static const vigil_native_class_method_t *find_method(const vigil_native_class_t *cls, const char *name)
{
    for (size_t i = 0; i < cls->method_count; i++)
        if (strcmp(cls->methods[i].name, name) == 0)
            return &cls->methods[i];
    return NULL;
}

TEST(AudioPlugin, ModuleRegisters)
{
    if (!audio_plugin_available())
        return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_audio_module(&natives, &error);
    EXPECT_NE(mod, NULL);
    EXPECT_STREQ(mod->name, "audio");
}

TEST(AudioPlugin, HasThreeClasses)
{
    if (!audio_plugin_available())
        return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_audio_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    EXPECT_EQ(mod->class_count, 3U);
    EXPECT_NE(find_class(mod, "Engine"), NULL);
    EXPECT_NE(find_class(mod, "Sound"), NULL);
    EXPECT_NE(find_class(mod, "Music"), NULL);
}

TEST(AudioPlugin, EngineClassMethods)
{
    if (!audio_plugin_available())
        return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_audio_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    const vigil_native_class_t *cls = find_class(mod, "Engine");
    ASSERT_NE(cls, NULL);
    EXPECT_NE(find_method(cls, "new"), NULL);
    EXPECT_NE(find_method(cls, "destroy"), NULL);
    EXPECT_NE(find_method(cls, "set_volume"), NULL);
}

TEST(AudioPlugin, SoundClassMethods)
{
    if (!audio_plugin_available())
        return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_audio_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    const vigil_native_class_t *cls = find_class(mod, "Sound");
    ASSERT_NE(cls, NULL);
    EXPECT_NE(find_method(cls, "load"), NULL);
    EXPECT_NE(find_method(cls, "load_memory"), NULL);
    EXPECT_NE(find_method(cls, "destroy"), NULL);
    EXPECT_NE(find_method(cls, "play"), NULL);
    EXPECT_NE(find_method(cls, "stop"), NULL);
    EXPECT_NE(find_method(cls, "set_volume"), NULL);
    EXPECT_NE(find_method(cls, "set_pitch"), NULL);
    EXPECT_NE(find_method(cls, "set_looping"), NULL);
    EXPECT_NE(find_method(cls, "set_position"), NULL);
}

TEST(AudioPlugin, MusicClassMethods)
{
    if (!audio_plugin_available())
        return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_audio_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    const vigil_native_class_t *cls = find_class(mod, "Music");
    ASSERT_NE(cls, NULL);
    EXPECT_NE(find_method(cls, "load"), NULL);
    EXPECT_NE(find_method(cls, "destroy"), NULL);
    EXPECT_NE(find_method(cls, "play"), NULL);
    EXPECT_NE(find_method(cls, "play_loop"), NULL);
    EXPECT_NE(find_method(cls, "pause"), NULL);
    EXPECT_NE(find_method(cls, "resume"), NULL);
    EXPECT_NE(find_method(cls, "stop"), NULL);
    EXPECT_NE(find_method(cls, "set_volume"), NULL);
    EXPECT_NE(find_method(cls, "set_pitch"), NULL);
    EXPECT_NE(find_method(cls, "seek"), NULL);
    EXPECT_NE(find_method(cls, "position"), NULL);
    EXPECT_NE(find_method(cls, "duration"), NULL);
    EXPECT_NE(find_method(cls, "fade_in"), NULL);
    EXPECT_NE(find_method(cls, "fade_out"), NULL);
}

TEST(AudioPlugin, HasListenerFunctions)
{
    if (!audio_plugin_available())
        return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_audio_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    EXPECT_GE(mod->function_count, 2U);
    int found_pos = 0, found_dir = 0;
    for (size_t i = 0; i < mod->function_count; i++)
    {
        if (strcmp(mod->functions[i].name, "set_listener_position") == 0)
            found_pos = 1;
        if (strcmp(mod->functions[i].name, "set_listener_direction") == 0)
            found_dir = 1;
    }
    EXPECT_EQ(found_pos, 1);
    EXPECT_EQ(found_dir, 1);
}

TEST(AudioPlugin, ModuleHasDoc)
{
    if (!audio_plugin_available())
        return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_audio_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    EXPECT_NE(mod->doc, NULL);
    EXPECT_NE(mod->doc->summary, NULL);
}

void register_audio_tests(void)
{
    REGISTER_TEST(AudioPlugin, ModuleRegisters);
    REGISTER_TEST(AudioPlugin, HasThreeClasses);
    REGISTER_TEST(AudioPlugin, EngineClassMethods);
    REGISTER_TEST(AudioPlugin, SoundClassMethods);
    REGISTER_TEST(AudioPlugin, MusicClassMethods);
    REGISTER_TEST(AudioPlugin, HasListenerFunctions);
    REGISTER_TEST(AudioPlugin, ModuleHasDoc);
}
// NOLINTEND(readability-function-cognitive-complexity)
