/* Tests for vigil/editor.h — editor integration install/uninstall. */
#include "vigil_test.h"

#include "vigil/editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── supported editors ───────────────────────────────────────────── */

TEST(EditorIntegration, SupportedListIsNonEmpty)
{
    const char *const *editors = vigil_editor_supported();
    ASSERT_NE(editors, NULL);
    ASSERT_NE(editors[0], NULL);
}

TEST(EditorIntegration, IsSupportedRecognizesKnownEditors)
{
    EXPECT_EQ(vigil_editor_is_supported("nvim"), 1);
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

TEST(EditorIntegration, NvimInstallUninstallRoundTrip)
{
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/vigil_editor_test_%d", __LINE__);

    EXPECT_EQ(vigil_editor_is_installed("nvim", tmpdir), 0);

    vigil_editor_result_t r = vigil_editor_install("nvim", "/usr/local/bin/vigil", tmpdir);
    EXPECT_EQ(r.status, VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_editor_is_installed("nvim", tmpdir), 1);

    /* Install again — idempotent */
    r = vigil_editor_install("nvim", "/usr/local/bin/vigil", tmpdir);
    EXPECT_EQ(r.status, VIGIL_STATUS_OK);

    r = vigil_editor_uninstall("nvim", tmpdir);
    EXPECT_EQ(r.status, VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_editor_is_installed("nvim", tmpdir), 0);

    /* Uninstall again — safe */
    r = vigil_editor_uninstall("nvim", tmpdir);
    EXPECT_EQ(r.status, VIGIL_STATUS_OK);

    /* Cleanup */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    system(cmd);
}

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

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    system(cmd);
}

TEST(EditorIntegration, VscodeInstallUninstallRoundTrip)
{
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/vigil_editor_test_vsc_%d", __LINE__);

    vigil_editor_result_t r = vigil_editor_install("vscode", "/usr/local/bin/vigil", tmpdir);
    EXPECT_EQ(r.status, VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_editor_is_installed("vscode", tmpdir), 1);

    r = vigil_editor_uninstall("vscode", tmpdir);
    EXPECT_EQ(r.status, VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_editor_is_installed("vscode", tmpdir), 0);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    system(cmd);
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
    REGISTER_TEST(EditorIntegration, NvimInstallUninstallRoundTrip);
    REGISTER_TEST(EditorIntegration, VimInstallUninstallRoundTrip);
    REGISTER_TEST(EditorIntegration, VscodeInstallUninstallRoundTrip);
    REGISTER_TEST(EditorIntegration, UnknownEditorReturnsError);
    REGISTER_TEST(EditorIntegration, NullArgsReturnError);
}
