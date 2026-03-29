/* Tests for vigil/editor.h — editor integration install/uninstall. */
#include "vigil_test.h"

#include "vigil/editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"

static int editor_test_exists(const char *path)
{
    int exists = 0;
    vigil_platform_file_exists(path, &exists);
    return exists;
}

static void editor_test_tmpdir(char *buf, size_t cap, const char *name, int line)
{
    snprintf(buf, cap, "/tmp/vigil_editor_test_%s_%d", name, line);
}

static void editor_test_sublime_syntax_path(char *buf, size_t cap, const char *tmpdir)
{
#ifdef __APPLE__
    snprintf(buf, cap, "%s/Library/Application Support/Sublime Text/Packages/Vigil/Vigil.sublime-syntax", tmpdir);
#elif defined(_WIN32)
    snprintf(buf, cap, "%s/AppData/Roaming/Sublime Text/Packages/Vigil/Vigil.sublime-syntax", tmpdir);
#else
    snprintf(buf, cap, "%s/.config/sublime-text/Packages/Vigil/Vigil.sublime-syntax", tmpdir);
#endif
}

static int editor_test_install_cycle_ok(const char *editor, const char *tmpdir, const char *installed_path)
{
    vigil_editor_result_t r = vigil_editor_install(editor, "/usr/local/bin/vigil", tmpdir);
    if (r.status != VIGIL_STATUS_OK)
        return 0;
    if (vigil_editor_is_installed(editor, tmpdir) != 1)
        return 0;
    if (editor_test_exists(installed_path) != 1)
        return 0;

    r = vigil_editor_uninstall(editor, tmpdir);
    if (r.status != VIGIL_STATUS_OK)
        return 0;
    if (vigil_editor_is_installed(editor, tmpdir) != 0)
        return 0;
    if (editor_test_exists(installed_path) != 0)
        return 0;
    return 1;
}

static int editor_test_emacs_init_contains(const char *path, const char *needle)
{
    char *content = NULL;
    size_t length = 0;
    int found = 0;
    if (vigil_platform_read_file(NULL, path, &content, &length, NULL) != VIGIL_STATUS_OK)
        return 0;
    found = strstr(content, needle) != NULL;
    free(content);
    return found;
}

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
    EXPECT_EQ(vigil_editor_is_supported("nvim"), 1);
    EXPECT_EQ(vigil_editor_is_supported("vscode"), 1);
    EXPECT_EQ(vigil_editor_is_supported("emacs"), 1);
    EXPECT_EQ(vigil_editor_is_supported("sublime"), 1);
}

TEST(EditorIntegration, IsSupportedRejectsUnknown)
{
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

TEST(EditorIntegration, NvimInstallUninstallRoundTrip)
{
    char tmpdir[256];
    char ft[512];
    editor_test_tmpdir(tmpdir, sizeof(tmpdir), "nvim", __LINE__);
    snprintf(ft, sizeof(ft), "%s/.config/nvim/after/ftdetect/vigil.vim", tmpdir);
    EXPECT_TRUE(editor_test_install_cycle_ok("nvim", tmpdir, ft));
    vigil_platform_remove_all(tmpdir, NULL);
}

TEST(EditorIntegration, EmacsInstallUninstallRoundTrip)
{
    char tmpdir[256];
    char mode_path[512];
    char init_path[512];
    vigil_editor_result_t r;
    editor_test_tmpdir(tmpdir, sizeof(tmpdir), "emacs", __LINE__);
    snprintf(mode_path, sizeof(mode_path), "%s/.emacs.d/vigil-mode.el", tmpdir);
    snprintf(init_path, sizeof(init_path), "%s/.emacs.d/init.el", tmpdir);

    r = vigil_editor_install("emacs", "/usr/local/bin/vigil", tmpdir);
    EXPECT_EQ(r.status, VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_editor_is_installed("emacs", tmpdir), 1);
    EXPECT_EQ(editor_test_exists(mode_path), 1);
    EXPECT_TRUE(editor_test_emacs_init_contains(init_path, "vigil-mode"));

    r = vigil_editor_uninstall("emacs", tmpdir);
    EXPECT_EQ(r.status, VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_editor_is_installed("emacs", tmpdir), 0);
    EXPECT_EQ(editor_test_exists(mode_path), 0);

    vigil_platform_remove_all(tmpdir, NULL);
}

TEST(EditorIntegration, SublimeInstallUninstallRoundTrip)
{
    char tmpdir[256];
    char syntax_path[512];
    editor_test_tmpdir(tmpdir, sizeof(tmpdir), "sublime", __LINE__);
    editor_test_sublime_syntax_path(syntax_path, sizeof(syntax_path), tmpdir);
    EXPECT_TRUE(editor_test_install_cycle_ok("sublime", tmpdir, syntax_path));
    vigil_platform_remove_all(tmpdir, NULL);
}

/* ── error handling ──────────────────────────────────────────────── */

TEST(EditorIntegration, UnknownEditorReturnsError)
{
    vigil_editor_result_t r = vigil_editor_install("fleet", "/usr/bin/vigil", "/tmp");
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
    REGISTER_TEST(EditorIntegration, NvimInstallUninstallRoundTrip);
    REGISTER_TEST(EditorIntegration, VscodeInstallUninstallRoundTrip);
    REGISTER_TEST(EditorIntegration, EmacsInstallUninstallRoundTrip);
    REGISTER_TEST(EditorIntegration, SublimeInstallUninstallRoundTrip);
    REGISTER_TEST(EditorIntegration, UnknownEditorReturnsError);
    REGISTER_TEST(EditorIntegration, NullArgsReturnError);
}
