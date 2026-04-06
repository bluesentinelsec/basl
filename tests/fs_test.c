/* Unit tests for VIGIL fs module platform functions. */
#include "platform/platform.h"
#include "vigil_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper to build temp file path */
static void make_temp_path(char *buf, size_t bufsize, const char *name)
{
    char *tmp = NULL;
    vigil_platform_temp_dir(NULL, &tmp, NULL);
    snprintf(buf, bufsize, "%s/%s", tmp ? tmp : ".", name);
    free(tmp);
}

/* ── Path operations (tested via platform layer) ─────────────────── */

TEST(VigilFsTest, PathJoin)
{
    char buf[256];
    vigil_status_t s = vigil_platform_path_join("a", "b", buf, sizeof(buf), NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_STREQ(buf, "a/b");
}

TEST(VigilFsTest, PathJoinTrailingSlash)
{
    char buf[256];
    vigil_status_t s = vigil_platform_path_join("a/", "b", buf, sizeof(buf), NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_STREQ(buf, "a/b");
}

/* ── File operations ─────────────────────────────────────────────── */

TEST(VigilFsTest, WriteAndRead)
{
    char path[512];
    make_temp_path(path, sizeof(path), "vigil_fs_test.txt");
    const char *data = "hello world";

    vigil_status_t s = vigil_platform_write_file(path, data, strlen(data), NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);

    char *read_data = NULL;
    size_t read_len = 0;
    s = vigil_platform_read_file(NULL, path, &read_data, &read_len, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_STREQ(read_data, data);
    free(read_data);

    vigil_platform_remove(path, NULL);
}

TEST(VigilFsTest, FileExists)
{
    char path[512];
    make_temp_path(path, sizeof(path), "vigil_fs_exists_test.txt");
    vigil_platform_write_file(path, "x", 1, NULL);

    int exists = 0;
    vigil_platform_file_exists(path, &exists);
    EXPECT_EQ(exists, 1);

    vigil_platform_remove(path, NULL);
    vigil_platform_file_exists(path, &exists);
    EXPECT_EQ(exists, 0);
}

TEST(VigilFsTest, IsDirectory)
{
    char *tmp = NULL;
    vigil_platform_temp_dir(NULL, &tmp, NULL);
    int is_dir = 0;
    vigil_platform_is_directory(tmp, &is_dir);
    EXPECT_EQ(is_dir, 1);
    free(tmp);

    char path[512];
    make_temp_path(path, sizeof(path), "vigil_fs_isdir_test.txt");
    vigil_platform_write_file(path, "x", 1, NULL);
    vigil_platform_is_directory(path, &is_dir);
    EXPECT_EQ(is_dir, 0);
    vigil_platform_remove(path, NULL);
}

TEST(VigilFsTest, IsFile)
{
    char *tmp = NULL;
    vigil_platform_temp_dir(NULL, &tmp, NULL);
    int is_file = 0;
    vigil_platform_is_file(tmp, &is_file);
    EXPECT_EQ(is_file, 0);
    free(tmp);

    char path[512];
    make_temp_path(path, sizeof(path), "vigil_fs_isfile_test.txt");
    vigil_platform_write_file(path, "x", 1, NULL);
    vigil_platform_is_file(path, &is_file);
    EXPECT_EQ(is_file, 1);
    vigil_platform_remove(path, NULL);
}

TEST(VigilFsTest, CopyFile)
{
    char src[512], dst[512];
    make_temp_path(src, sizeof(src), "vigil_fs_copy_src.txt");
    make_temp_path(dst, sizeof(dst), "vigil_fs_copy_dst.txt");

    vigil_platform_write_file(src, "copy me", 7, NULL);
    vigil_status_t s = vigil_platform_copy_file(src, dst, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);

    char *data = NULL;
    size_t len = 0;
    vigil_platform_read_file(NULL, dst, &data, &len, NULL);
    EXPECT_STREQ(data, "copy me");
    free(data);

    vigil_platform_remove(src, NULL);
    vigil_platform_remove(dst, NULL);
}

TEST(VigilFsTest, RenameFile)
{
    char src[512], dst[512];
    make_temp_path(src, sizeof(src), "vigil_fs_rename_src.txt");
    make_temp_path(dst, sizeof(dst), "vigil_fs_rename_dst.txt");

    vigil_platform_write_file(src, "move me", 7, NULL);
    vigil_status_t s = vigil_platform_rename(src, dst, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);

    int exists = 0;
    vigil_platform_file_exists(src, &exists);
    EXPECT_EQ(exists, 0);
    vigil_platform_file_exists(dst, &exists);
    EXPECT_EQ(exists, 1);

    vigil_platform_remove(dst, NULL);
}

/* ── Directory operations ────────────────────────────────────────── */

TEST(VigilFsTest, MkdirAndRemove)
{
    char path[512];
    make_temp_path(path, sizeof(path), "vigil_fs_mkdir_test");

    vigil_status_t s = vigil_platform_mkdir(path, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);

    int is_dir = 0;
    vigil_platform_is_directory(path, &is_dir);
    EXPECT_EQ(is_dir, 1);

    vigil_platform_remove(path, NULL);
    vigil_platform_is_directory(path, &is_dir);
    EXPECT_EQ(is_dir, 0);
}

TEST(VigilFsTest, MkdirP)
{
    char path[1024], base[512];
    make_temp_path(base, sizeof(base), "vigil_fs_mkdirp_test");
    snprintf(path, sizeof(path), "%s/a/b/c", base);

    vigil_status_t s = vigil_platform_mkdir_p(path, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);

    int is_dir = 0;
    vigil_platform_is_directory(path, &is_dir);
    EXPECT_EQ(is_dir, 1);

    /* Cleanup */
    vigil_platform_remove(path, NULL);
    snprintf(path, sizeof(path), "%s/a/b", base);
    vigil_platform_remove(path, NULL);
    snprintf(path, sizeof(path), "%s/a", base);
    vigil_platform_remove(path, NULL);
    vigil_platform_remove(base, NULL);
}

/* ── Metadata ────────────────────────────────────────────────────── */

TEST(VigilFsTest, FileSize)
{
    char path[512];
    make_temp_path(path, sizeof(path), "vigil_fs_size_test.txt");
    vigil_platform_write_file(path, "12345", 5, NULL);

    int64_t size = vigil_platform_file_size(path);
    EXPECT_EQ(size, 5);

    vigil_platform_remove(path, NULL);
}

TEST(VigilFsTest, FileMtime)
{
    char path[512];
    make_temp_path(path, sizeof(path), "vigil_fs_mtime_test.txt");
    vigil_platform_write_file(path, "x", 1, NULL);

    int64_t mtime = vigil_platform_file_mtime(path);
    EXPECT_TRUE(mtime > 0);

    vigil_platform_remove(path, NULL);
}

/* ── Standard locations ──────────────────────────────────────────── */

TEST(VigilFsTest, TempDir)
{
    char *path = NULL;
    vigil_status_t s = vigil_platform_temp_dir(NULL, &path, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    ASSERT_NE(path, NULL);
    EXPECT_TRUE(strlen(path) > 0);
    free(path);
}

TEST(VigilFsTest, HomeDir)
{
    char *path = NULL;
    vigil_status_t s = vigil_platform_home_dir(NULL, &path, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    ASSERT_NE(path, NULL);
    EXPECT_TRUE(strlen(path) > 0);
    free(path);
}

TEST(VigilFsTest, ConfigDir)
{
    char *path = NULL;
    vigil_status_t s = vigil_platform_config_dir(NULL, &path, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    ASSERT_NE(path, NULL);
    EXPECT_TRUE(strlen(path) > 0);
    free(path);
}

TEST(VigilFsTest, TempFile)
{
    char *path = NULL;
    vigil_status_t s = vigil_platform_temp_file(NULL, "vigil", &path, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    ASSERT_NE(path, NULL);

    int exists = 0;
    vigil_platform_file_exists(path, &exists);
    EXPECT_EQ(exists, 1);

    vigil_platform_remove(path, NULL);
    free(path);
}

/* ── Symlink operations ──────────────────────────────────────────── */

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
TEST(VigilFsTest, SymlinkCreateAndRead)
{
    char target[256], link[256];
    make_temp_path(target, sizeof(target), "vigil_sym_target.txt");
    make_temp_path(link, sizeof(link), "vigil_sym_link.txt");

    vigil_platform_write_file(target, "symlink test", 12, NULL);

    vigil_status_t s = vigil_platform_symlink(target, link, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);

    int is_sym = 0;
    vigil_platform_is_symlink(link, &is_sym);
    EXPECT_EQ(is_sym, 1);

    int is_sym_target = 0;
    vigil_platform_is_symlink(target, &is_sym_target);
    EXPECT_EQ(is_sym_target, 0);

    char *read_target = NULL;
    s = vigil_platform_readlink(NULL, link, &read_target, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_STREQ(read_target, target);
    free(read_target);

    vigil_platform_remove(link, NULL);
    vigil_platform_remove(target, NULL);
}
#endif

/* ── Recursive remove ────────────────────────────────────────────── */

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

TEST(VigilFsTest, RemoveAll)
{
    char base[256], sub[1024], file1[1024], file2[1024];
    make_temp_path(base, sizeof(base), "vigil_rmall_test");
    snprintf(sub, sizeof(sub), "%s/sub/deep", base);
    snprintf(file1, sizeof(file1), "%s/top.txt", base);
    snprintf(file2, sizeof(file2), "%s/leaf.txt", sub);

    vigil_platform_mkdir_p(sub, NULL);
    vigil_platform_write_file(file1, "top", 3, NULL);
    vigil_platform_write_file(file2, "leaf", 4, NULL);

    int exists = 0;
    vigil_platform_file_exists(base, &exists);
    EXPECT_EQ(exists, 1);

    vigil_status_t s = vigil_platform_remove_all(base, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);

    vigil_platform_file_exists(base, &exists);
    EXPECT_EQ(exists, 0);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

/* ── Glob matching ───────────────────────────────────────────────── */

TEST(VigilFsTest, GlobMatch)
{
    EXPECT_EQ(vigil_platform_glob_match("*.txt", "hello.txt"), 1);
    EXPECT_EQ(vigil_platform_glob_match("*.txt", "hello.md"), 0);
    EXPECT_EQ(vigil_platform_glob_match("test_*", "test_foo"), 1);
    EXPECT_EQ(vigil_platform_glob_match("test_*", "foo_test"), 0);
    EXPECT_EQ(vigil_platform_glob_match("*.vigil", "main.vigil"), 1);
    EXPECT_EQ(vigil_platform_glob_match("?oo", "foo"), 1);
    EXPECT_EQ(vigil_platform_glob_match("?oo", "fo"), 0);
    EXPECT_EQ(vigil_platform_glob_match("*", "anything"), 1);
    EXPECT_EQ(vigil_platform_glob_match("", ""), 1);
    EXPECT_EQ(vigil_platform_glob_match("", "x"), 0);
    EXPECT_EQ(vigil_platform_glob_match("a*b*c", "aXbYc"), 1);
    EXPECT_EQ(vigil_platform_glob_match("a*b*c", "aXbY"), 0);
}

/* ── Test Registration ───────────────────────────────────────────── */

void register_fs_tests(void)
{
    REGISTER_TEST(VigilFsTest, PathJoin);
    REGISTER_TEST(VigilFsTest, PathJoinTrailingSlash);
    REGISTER_TEST(VigilFsTest, WriteAndRead);
    REGISTER_TEST(VigilFsTest, FileExists);
    REGISTER_TEST(VigilFsTest, IsDirectory);
    REGISTER_TEST(VigilFsTest, IsFile);
    REGISTER_TEST(VigilFsTest, CopyFile);
    REGISTER_TEST(VigilFsTest, RenameFile);
    REGISTER_TEST(VigilFsTest, MkdirAndRemove);
    REGISTER_TEST(VigilFsTest, MkdirP);
    REGISTER_TEST(VigilFsTest, FileSize);
    REGISTER_TEST(VigilFsTest, FileMtime);
    REGISTER_TEST(VigilFsTest, TempDir);
    REGISTER_TEST(VigilFsTest, HomeDir);
    REGISTER_TEST(VigilFsTest, ConfigDir);
    REGISTER_TEST(VigilFsTest, TempFile);
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    REGISTER_TEST(VigilFsTest, SymlinkCreateAndRead);
#endif
    REGISTER_TEST(VigilFsTest, RemoveAll);
    REGISTER_TEST(VigilFsTest, GlobMatch);
}
