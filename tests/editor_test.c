/* Tests for vigil/editor.h — editor integration install/uninstall. */
#include "vigil_test.h"

#include "vigil/editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"

/* ── supported editors ───────────────────────────────────────────── */

TEST(EditorIntegration, SupportedListIsNonEmpty)
{
    const char *const *editors = vigil_editor_supported();
    ASSERT_NE(editors, NULL);
    ASSERT_NE(editors[0], NULL);
}

TEST(EditorIntegration, IsSupportedRecognizesKnownEditors)
{
    EXPECT_EQ(vigil_editor_is_supported("vim"), 1);
    EXPECT_EQ(vigil_editor_is_supported("vscode"), 1);
}

TEST(EditorIntegration, IsSupportedRejectsUnknown)
{
    EXPECT_EQ(vigil_editor_is_supported("emacs"), 0);
    EXPECT_EQ(vigil_editor_is_supported("notepad"), 0);
    EXPECT_EQ(vigil_editor_is_supported(NULL), 0);
}

/* ── install / uninstall round-trip ──────────────────────────────── */

TEST(EditorIntegration, VimInstallUninstallRoundTrip)
{
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/vigil_editor_test_vim_%d", __LINE__);

    vigil_editor_result_t r = vigil_editor_install("vim", "/usr/local/bin/vigil", tmpdir);
    EXPECT_EQ(r.status, VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_editor_is_installed("vim", tmpdir), 1);

    r = vigil_editor_uninstall("vim", tmpdir);
    EXPECT_EQ(r.status, VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_editor_is_installed("vim", tmpdir), 0);

    vigil_platform_remove_all(tmpdir, NULL);
}

TEST(EditorIntegration, VscodeInstallUninstallRoundTrip)
{
    /* Skip if npm is not available (e.g., CI sanitizer containers). */
    if (system("npm --version > /dev/null 2>&1") != 0)
        return;

    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/vigil_editor_test_vsc_%d", __LINE__);

    vigil_editor_result_t r = vigil_editor_install("vscode", "/usr/local/bin/vigil", tmpdir);
    EXPECT_EQ(r.status, VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_editor_is_installed("vscode", tmpdir), 1);

    r = vigil_editor_uninstall("vscode", tmpdir);
    EXPECT_EQ(r.status, VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_editor_is_installed("vscode", tmpdir), 0);

    vigil_platform_remove_all(tmpdir, NULL);
}

/* ── error handling ──────────────────────────────────────────────── */

TEST(EditorIntegration, UnknownEditorReturnsError)
{
    vigil_editor_result_t r = vigil_editor_install("emacs", "/usr/bin/vigil", "/tmp");
    EXPECT_NE(r.status, VIGIL_STATUS_OK);
}

TEST(EditorIntegration, NullArgsReturnError)
{
    vigil_editor_result_t r = vigil_editor_install(NULL, NULL, NULL);
    EXPECT_NE(r.status, VIGIL_STATUS_OK);
    r = vigil_editor_uninstall(NULL, NULL);
    EXPECT_NE(r.status, VIGIL_STATUS_OK);
}

/* ── registration ────────────────────────────────────────────────── */

void register_editor_tests(void)
{
    REGISTER_TEST(EditorIntegration, SupportedListIsNonEmpty);
    REGISTER_TEST(EditorIntegration, IsSupportedRecognizesKnownEditors);
    REGISTER_TEST(EditorIntegration, IsSupportedRejectsUnknown);
    REGISTER_TEST(EditorIntegration, VimInstallUninstallRoundTrip);
    REGISTER_TEST(EditorIntegration, VscodeInstallUninstallRoundTrip);
    REGISTER_TEST(EditorIntegration, UnknownEditorReturnsError);
    REGISTER_TEST(EditorIntegration, NullArgsReturnError);
}
