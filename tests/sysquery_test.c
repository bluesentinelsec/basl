/* sysquery_test.c — Unit tests for the sysquery plugin. */
// NOLINTBEGIN(readability-function-cognitive-complexity)
#include "vigil_test.h"

#include <string.h>

#include "vigil/native_module.h"

#include "plugin_registry.h"

static int sysquery_available(void)
{
    return vigil_plugin_is_known_module("sysquery", 8U);
}

static const vigil_native_module_t *get_module(vigil_native_registry_t *natives, vigil_error_t *error)
{
    vigil_native_registry_init(natives);
    if (vigil_plugin_register_all(natives, error) != VIGIL_STATUS_OK)
        return NULL;
    return vigil_native_registry_find(natives, "sysquery", 8U);
}

TEST(SysqueryPlugin, ModuleRegisters)
{
    if (!sysquery_available())
        return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_module(&natives, &error);
    EXPECT_NE(mod, NULL);
    EXPECT_STREQ(mod->name, "sysquery");
    vigil_native_registry_free(&natives);
}

TEST(SysqueryPlugin, HasSixClasses)
{
    if (!sysquery_available())
        return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    EXPECT_EQ(mod->class_count, 6U);
    vigil_native_registry_free(&natives);
}

TEST(SysqueryPlugin, HasThirteenFunctions)
{
    if (!sysquery_available())
        return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    EXPECT_EQ(mod->function_count, 13U);
    static const char *expected[] = {"sysinfo", "getuid",   "getsid",   "localtime", "getproxy", "resolve", "ps",
                                     "pgrep",   "ifconfig", "ipconfig", "netstat",   "arp",      "route"};
    for (size_t j = 0; j < 13; j++)
    {
        int found = 0;
        for (size_t i = 0; i < mod->function_count; i++)
            if (strcmp(mod->functions[i].name, expected[j]) == 0)
                found = 1;
        EXPECT_EQ(found, 1);
    }
    vigil_native_registry_free(&natives);
}

TEST(SysqueryPlugin, ModuleHasDoc)
{
    if (!sysquery_available())
        return;
    vigil_native_registry_t natives;
    vigil_error_t error;
    const vigil_native_module_t *mod = get_module(&natives, &error);
    ASSERT_NE(mod, NULL);
    EXPECT_NE(mod->doc, NULL);
    EXPECT_NE(mod->doc->summary, NULL);
    vigil_native_registry_free(&natives);
}

void register_sysquery_tests(void)
{
    REGISTER_TEST(SysqueryPlugin, ModuleRegisters);
    REGISTER_TEST(SysqueryPlugin, HasSixClasses);
    REGISTER_TEST(SysqueryPlugin, HasThirteenFunctions);
    REGISTER_TEST(SysqueryPlugin, ModuleHasDoc);
}
// NOLINTEND(readability-function-cognitive-complexity)
