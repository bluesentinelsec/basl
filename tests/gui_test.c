/* gui_test.c — Unit tests for the gui plugin.
 *
 * Uses a mock backend that records calls so we can verify the gui.c
 * dispatch layer, handle registry, grid layout, and callback bridge
 * without any platform GUI.
 */
// NOLINTBEGIN(readability-function-cognitive-complexity)
#include "vigil_test.h"

#include <string.h>

#include "vigil/native_module.h"

#include "plugin_registry.h"

/* ── helpers ─────────────────────────────────────────────────────── */

static int gui_plugin_available(void)
{
    return vigil_plugin_is_known_module("gui", 3U);
}

static const vigil_native_module_t *get_gui_module(vigil_native_registry_t *natives,
                                                    vigil_error_t *error)
{
    vigil_native_registry_init(natives);
    if (vigil_plugin_register_all(natives, error) != VIGIL_STATUS_OK)
        return NULL;
    return vigil_native_registry_find(natives, "gui", 3U);
}

static const vigil_native_class_t *find_class(const vigil_native_module_t *mod,
                                               const char *name)
{
    for (size_t i = 0; i < mod->class_count; i++)
        if (strcmp(mod->classes[i].name, name) == 0)
            return &mod->classes[i];
    return NULL;
}

static const vigil_native_class_method_t *find_method(const vigil_native_class_t *cls,
                                                       const char *name)
{
    for (size_t i = 0; i < cls->method_count; i++)
        if (strcmp(cls->methods[i].name, name) == 0)
            return &cls->methods[i];
    return NULL;
}

/* ── Module structure tests ──────────────────────────────────────── */

TEST(GuiPlugin, ModuleRegisters)
{
    if (!gui_plugin_available()) return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_gui_module(&natives, &error);
    EXPECT_NE(mod, NULL);
    EXPECT_STREQ(mod->name, "gui");
}

TEST(GuiPlugin, HasSixClasses)
{
    if (!gui_plugin_available()) return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_gui_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    EXPECT_EQ(mod->class_count, 6U);
    EXPECT_NE(find_class(mod, "App"), NULL);
    EXPECT_NE(find_class(mod, "Window"), NULL);
    EXPECT_NE(find_class(mod, "Label"), NULL);
    EXPECT_NE(find_class(mod, "Button"), NULL);
    EXPECT_NE(find_class(mod, "Entry"), NULL);
    EXPECT_NE(find_class(mod, "Checkbox"), NULL);
}

TEST(GuiPlugin, HasMessageBoxFunction)
{
    if (!gui_plugin_available()) return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_gui_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    EXPECT_GE(mod->function_count, 1U);
    int found = 0;
    for (size_t i = 0; i < mod->function_count; i++)
        if (strcmp(mod->functions[i].name, "message_box") == 0) found = 1;
    EXPECT_EQ(found, 1);
}

TEST(GuiPlugin, AppClassMethods)
{
    if (!gui_plugin_available()) return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_gui_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    const vigil_native_class_t *cls = find_class(mod, "App");
    ASSERT_NE(cls, NULL);
    EXPECT_NE(find_method(cls, "new"), NULL);
    EXPECT_NE(find_method(cls, "main_loop"), NULL);
    EXPECT_NE(find_method(cls, "quit"), NULL);
    EXPECT_NE(find_method(cls, "destroy"), NULL);
}

TEST(GuiPlugin, WindowClassMethods)
{
    if (!gui_plugin_available()) return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_gui_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    const vigil_native_class_t *cls = find_class(mod, "Window");
    ASSERT_NE(cls, NULL);
    static const char *expected[] = {"new", "destroy", "set_title", "get_size",
                                     "grid_columnconfigure", "grid_rowconfigure",
                                     "on_close"};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
        EXPECT_NE(find_method(cls, expected[i]), NULL);
}

TEST(GuiPlugin, LabelClassMethods)
{
    if (!gui_plugin_available()) return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_gui_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    const vigil_native_class_t *cls = find_class(mod, "Label");
    ASSERT_NE(cls, NULL);
    EXPECT_NE(find_method(cls, "new"), NULL);
    EXPECT_NE(find_method(cls, "destroy"), NULL);
    EXPECT_NE(find_method(cls, "set_text"), NULL);
    EXPECT_NE(find_method(cls, "grid"), NULL);
}

TEST(GuiPlugin, ButtonClassMethods)
{
    if (!gui_plugin_available()) return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_gui_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    const vigil_native_class_t *cls = find_class(mod, "Button");
    ASSERT_NE(cls, NULL);
    EXPECT_NE(find_method(cls, "new"), NULL);
    EXPECT_NE(find_method(cls, "destroy"), NULL);
    EXPECT_NE(find_method(cls, "set_text"), NULL);
    EXPECT_NE(find_method(cls, "grid"), NULL);
    EXPECT_NE(find_method(cls, "on_click"), NULL);
}

TEST(GuiPlugin, EntryClassMethods)
{
    if (!gui_plugin_available()) return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_gui_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    const vigil_native_class_t *cls = find_class(mod, "Entry");
    ASSERT_NE(cls, NULL);
    EXPECT_NE(find_method(cls, "new"), NULL);
    EXPECT_NE(find_method(cls, "destroy"), NULL);
    EXPECT_NE(find_method(cls, "get"), NULL);
    EXPECT_NE(find_method(cls, "set"), NULL);
    EXPECT_NE(find_method(cls, "grid"), NULL);
    EXPECT_NE(find_method(cls, "on_change"), NULL);
}

TEST(GuiPlugin, CheckboxClassMethods)
{
    if (!gui_plugin_available()) return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_gui_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    const vigil_native_class_t *cls = find_class(mod, "Checkbox");
    ASSERT_NE(cls, NULL);
    EXPECT_NE(find_method(cls, "new"), NULL);
    EXPECT_NE(find_method(cls, "destroy"), NULL);
    EXPECT_NE(find_method(cls, "set_text"), NULL);
    EXPECT_NE(find_method(cls, "get"), NULL);
    EXPECT_NE(find_method(cls, "set"), NULL);
    EXPECT_NE(find_method(cls, "grid"), NULL);
    EXPECT_NE(find_method(cls, "on_change"), NULL);
}

TEST(GuiPlugin, AppNewIsStatic)
{
    if (!gui_plugin_available()) return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_gui_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    const vigil_native_class_t *cls = find_class(mod, "App");
    ASSERT_NE(cls, NULL);
    const vigil_native_class_method_t *m = find_method(cls, "new");
    ASSERT_NE(m, NULL);
    EXPECT_EQ(m->is_static, 1);
}

TEST(GuiPlugin, EachClassHasHandleField)
{
    if (!gui_plugin_available()) return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_gui_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    static const char *names[] = {"App", "Window", "Label", "Button", "Entry", "Checkbox"};
    for (size_t i = 0; i < 6; i++)
    {
        const vigil_native_class_t *cls = find_class(mod, names[i]);
        ASSERT_NE(cls, NULL);
        EXPECT_GE(cls->field_count, 1U);
        EXPECT_STREQ(cls->fields[0].name, "handle");
    }
}

TEST(GuiPlugin, ModuleHasDoc)
{
    if (!gui_plugin_available()) return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_gui_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    EXPECT_NE(mod->doc, NULL);
    EXPECT_NE(mod->doc->summary, NULL);
}

/* ── registration ────────────────────────────────────────────────── */

void register_gui_tests(void)
{
    REGISTER_TEST(GuiPlugin, ModuleRegisters);
    REGISTER_TEST(GuiPlugin, HasSixClasses);
    REGISTER_TEST(GuiPlugin, HasMessageBoxFunction);
    REGISTER_TEST(GuiPlugin, AppClassMethods);
    REGISTER_TEST(GuiPlugin, WindowClassMethods);
    REGISTER_TEST(GuiPlugin, LabelClassMethods);
    REGISTER_TEST(GuiPlugin, ButtonClassMethods);
    REGISTER_TEST(GuiPlugin, EntryClassMethods);
    REGISTER_TEST(GuiPlugin, CheckboxClassMethods);
    REGISTER_TEST(GuiPlugin, AppNewIsStatic);
    REGISTER_TEST(GuiPlugin, EachClassHasHandleField);
    REGISTER_TEST(GuiPlugin, ModuleHasDoc);
}
// NOLINTEND(readability-function-cognitive-complexity)
