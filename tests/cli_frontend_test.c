#include "vigil_test.h"

#include <stdlib.h>
#include <string.h>

#include "internal/vigil_cli_frontend.h"
#include "platform/platform.h"

typedef struct CliFrontendTest
{
    vigil_error_t error;
} CliFrontendTest;

static void CliFrontendTest_SetUp(void *fixture)
{
    memset(fixture, 0, sizeof(CliFrontendTest));
}

static void CliFrontendTest_TearDown(void *fixture)
{
    vigil_error_clear(&((CliFrontendTest *)fixture)->error);
}

static void remove_path_if_present(const char *path, vigil_error_t *error)
{
    vigil_error_clear(error);
    (void)vigil_platform_remove(path, error);
    vigil_error_clear(error);
}

static void remove_cli_frontend_project(const char *root, const char *module_name, vigil_error_t *error)
{
    char path[4096];

    snprintf(path, sizeof(path), "%s/lib/%s.vigil", root, module_name);
    remove_path_if_present(path, error);
    snprintf(path, sizeof(path), "%s/test/main_test.vigil", root);
    remove_path_if_present(path, error);
    snprintf(path, sizeof(path), "%s/main.vigil", root);
    remove_path_if_present(path, error);
    snprintf(path, sizeof(path), "%s/vigil.toml", root);
    remove_path_if_present(path, error);
    snprintf(path, sizeof(path), "%s/lib", root);
    remove_path_if_present(path, error);
    snprintf(path, sizeof(path), "%s/test", root);
    remove_path_if_present(path, error);
    remove_path_if_present(root, error);
}

static vigil_status_t write_text_fixture(const char *path, const char *text, vigil_error_t *error)
{
    return vigil_platform_write_file(path, text, strlen(text), error);
}

static int setup_project_root_fixture(const char *root, int with_manifest, vigil_error_t *error)
{
    static const char test_source[] = "import \"test\";\nfn test_ok(test.T t) -> void { t.assert(true, \"ok\"); }\n";
    char path[4096];

    if (vigil_platform_mkdir_p(root, error) != VIGIL_STATUS_OK)
        return 0;
    snprintf(path, sizeof(path), "%s/test", root);
    if (vigil_platform_mkdir_p(path, error) != VIGIL_STATUS_OK)
        return 0;
    if (with_manifest)
    {
        snprintf(path, sizeof(path), "%s/vigil.toml", root);
        if (write_text_fixture(path, "[project]\nname = \"root\"\n", error) != VIGIL_STATUS_OK)
            return 0;
    }
    snprintf(path, sizeof(path), "%s/test/main_test.vigil", root);
    if (write_text_fixture(path, test_source, error) != VIGIL_STATUS_OK)
        return 0;
    return 1;
}

static int setup_register_project_fixture(const char *root, const char *import_name, const char *main_source,
                                          vigil_error_t *error)
{
    char path[4096];

    if (vigil_platform_mkdir_p(root, error) != VIGIL_STATUS_OK)
        return 0;
    snprintf(path, sizeof(path), "%s/lib", root);
    if (vigil_platform_mkdir_p(path, error) != VIGIL_STATUS_OK)
        return 0;
    snprintf(path, sizeof(path), "%s/vigil.toml", root);
    if (write_text_fixture(path, "[project]\nname = \"register\"\n", error) != VIGIL_STATUS_OK)
        return 0;
    if (import_name != NULL)
    {
        snprintf(path, sizeof(path), "%s/lib/%s.vigil", root, import_name);
        if (write_text_fixture(path, "pub fn value() -> i32 { return 7; }\n", error) != VIGIL_STATUS_OK)
            return 0;
    }
    snprintf(path, sizeof(path), "%s/main.vigil", root);
    if (write_text_fixture(path, main_source, error) != VIGIL_STATUS_OK)
        return 0;
    return 1;
}

static const vigil_source_file_t *find_registered_source(const vigil_source_registry_t *registry, const char *path)
{
    size_t index;

    for (index = 1U; index <= vigil_source_registry_count(registry); index += 1U)
    {
        const vigil_source_file_t *source;

        source = vigil_source_registry_get(registry, (vigil_source_id_t)index);
        if (source != NULL && strcmp(vigil_string_c_str(&source->path), path) == 0)
            return source;
    }
    return NULL;
}

typedef struct runtime_registry_fixture
{
    vigil_runtime_t *runtime;
    vigil_source_registry_t registry;
} runtime_registry_fixture_t;

typedef struct register_project_request
{
    runtime_registry_fixture_t *fixture;
    const char *root;
    const char *import_name;
    const char *main_source;
    vigil_source_id_t *out_source_id;
    char *main_path;
    size_t main_path_size;
    char *project_root;
    size_t project_root_size;
} register_project_request_t;

static int open_runtime_registry(runtime_registry_fixture_t *fixture, vigil_error_t *error)
{
    fixture->runtime = NULL;
    if (vigil_runtime_open(&fixture->runtime, NULL, error) != VIGIL_STATUS_OK)
        return 0;
    vigil_source_registry_init(&fixture->registry, fixture->runtime);
    return 1;
}

static void close_runtime_registry(runtime_registry_fixture_t *fixture)
{
    vigil_source_registry_free(&fixture->registry);
    vigil_runtime_close(&fixture->runtime);
}

static int register_project_main_source(const register_project_request_t *request, vigil_error_t *error)
{
    if (request->out_source_id != NULL)
        *request->out_source_id = 0U;
    request->fixture->runtime = NULL;
    if (!setup_register_project_fixture(request->root, request->import_name, request->main_source, error))
        return 0;
    snprintf(request->main_path, request->main_path_size, "%s/main.vigil", request->root);
    if (!find_project_root(request->main_path, request->project_root, request->project_root_size))
        return 0;
    if (!open_runtime_registry(request->fixture, error))
        return 0;
    return register_source_tree(&request->fixture->registry, request->main_path, request->project_root,
                                request->out_source_id, error);
}

static int verify_registered_helper_module(const char *root, vigil_error_t *error)
{
    const char *main_source = "import \"helper\";\nfn main() -> i32 { return helper.value(); }\n";
    const vigil_source_file_t *helper_source;
    runtime_registry_fixture_t fixture;
    vigil_source_id_t source_id;
    char main_path[4096];
    char helper_import_path[4096];
    char project_root[4096];
    int ok;

    ok = 0;
    source_id = 0U;
    fixture.runtime = NULL;
    if (!setup_register_project_fixture(root, "helper", main_source, error))
        return 0;
    snprintf(main_path, sizeof(main_path), "%s/main.vigil", root);
    if (!find_project_root(main_path, project_root, sizeof(project_root)))
        return 0;
    if (!open_runtime_registry(&fixture, error))
        return 0;
    if (!register_source_tree(&fixture.registry, main_path, project_root, &source_id, error))
        goto cleanup;
    if (source_id == 0U || vigil_source_registry_count(&fixture.registry) != 2U)
        goto cleanup;
    snprintf(helper_import_path, sizeof(helper_import_path), "%s/helper.vigil", root);
    helper_source = find_registered_source(&fixture.registry, helper_import_path);
    if (helper_source == NULL)
        goto cleanup;
    ok = strstr(vigil_string_c_str(&helper_source->text), "pub fn value") != NULL;

cleanup:
    close_runtime_registry(&fixture);
    return ok;
}

static int verify_stdlib_only_import_registration(const char *root, vigil_error_t *error)
{
    runtime_registry_fixture_t fixture;
    register_project_request_t request;
    vigil_source_id_t source_id;
    char main_path[4096];
    char project_root[4096];
    int ok;

    ok = 0;
    source_id = 0U;
    remove_cli_frontend_project(root, "helper", error);
    request = (register_project_request_t){
        &fixture,
        root,
        NULL,
        "import \"fmt\";\nfn main() -> i32 { return 0; }\n",
        &source_id,
        main_path,
        sizeof(main_path),
        project_root,
        sizeof(project_root),
    };
    if (!register_project_main_source(&request, error))
    {
        return 0;
    }
    ok = vigil_source_registry_count(&fixture.registry) == 1U && source_id != 0U;
    close_runtime_registry(&fixture);
    remove_cli_frontend_project(root, "helper", error);
    return ok;
}

static int verify_existing_source_registration(const char *root, vigil_error_t *error)
{
    runtime_registry_fixture_t fixture;
    register_project_request_t request;
    vigil_source_id_t first_id;
    vigil_source_id_t second_id;
    char main_path[4096];
    char project_root[4096];
    int ok;

    ok = 0;
    first_id = 0U;
    second_id = 0U;
    remove_cli_frontend_project(root, "helper", error);
    request = (register_project_request_t){
        &fixture,
        root,
        NULL,
        "fn main() -> i32 { return 0; }\n",
        &first_id,
        main_path,
        sizeof(main_path),
        project_root,
        sizeof(project_root),
    };
    if (!register_project_main_source(&request, error))
    {
        return 0;
    }
    if (register_source_tree(&fixture.registry, main_path, project_root, &second_id, error))
        ok = first_id == second_id && vigil_source_registry_count(&fixture.registry) == 1U;
    close_runtime_registry(&fixture);
    remove_cli_frontend_project(root, "helper", error);
    return ok;
}

static int verify_absolute_import_registration(const char *root, vigil_error_t *error)
{
    runtime_registry_fixture_t fixture;
    register_project_request_t request;
    vigil_source_id_t source_id;
    char *cwd;
    char helper_path[4096];
    char main_source[8192];
    char main_path[4096];
    char project_root[4096];
    size_t index;
    int ok;

    cwd = NULL;
    ok = 0;
    source_id = 0U;
    remove_cli_frontend_project(root, "helper", error);
    if (vigil_platform_getcwd(NULL, &cwd, error) != VIGIL_STATUS_OK)
        return 0;
    snprintf(helper_path, sizeof(helper_path), "%s/%s/lib/helper.vigil", cwd, root);
    snprintf(main_source, sizeof(main_source), "import \"%s\";\nfn main() -> i32 { return helper.value(); }\n",
             helper_path);
    request = (register_project_request_t){
        &fixture,          root,         "helper",
        main_source,       &source_id,   main_path,
        sizeof(main_path), project_root, sizeof(project_root),
    };
    if (!register_project_main_source(&request, error))
    {
        free(cwd);
        return 0;
    }
    ok = source_id != 0U && vigil_source_registry_count(&fixture.registry) == 2U;
    for (index = 1U; ok && index <= vigil_source_registry_count(&fixture.registry); index += 1U)
    {
        const vigil_source_file_t *source;

        source = vigil_source_registry_get(&fixture.registry, (vigil_source_id_t)index);
        if (source == NULL)
            continue;
        if (strcmp(vigil_string_c_str(&source->path), main_path) != 0 &&
            strstr(vigil_string_c_str(&source->text), "pub fn value") != NULL)
        {
            break;
        }
    }
    ok = ok && index <= vigil_source_registry_count(&fixture.registry);
    close_runtime_registry(&fixture);
    free(cwd);
    remove_cli_frontend_project(root, "helper", error);
    return ok;
}

#define ERR (&FIXTURE(CliFrontendTest)->error)

TEST_F(CliFrontendTest, FindProjectRootReturnsProjectDirectory)
{
    const char *root = "cli_frontend_root_proj";
    char path[4096];
    char out[4096];

    remove_cli_frontend_project(root, "helper", ERR);
    ASSERT_TRUE(setup_project_root_fixture(root, 1, ERR));
    snprintf(path, sizeof(path), "%s/test/main_test.vigil", root);

    ASSERT_TRUE(find_project_root(path, out, sizeof(out)));
    EXPECT_TRUE(strcmp(out, root) == 0);

    remove_cli_frontend_project(root, "helper", ERR);
}

TEST_F(CliFrontendTest, FindProjectRootReturnsZeroWithoutManifest)
{
    const char *root = "cli_frontend_no_manifest";
    char path[4096];
    char out[4096];

    remove_cli_frontend_project(root, "helper", ERR);
    ASSERT_TRUE(setup_project_root_fixture(root, 0, ERR));
    snprintf(path, sizeof(path), "%s/test/main_test.vigil", root);

    EXPECT_FALSE(find_project_root(path, out, sizeof(out)));

    remove_cli_frontend_project(root, "helper", ERR);
}

TEST_F(CliFrontendTest, FindProjectRootRejectsNullInputs)
{
    char out[16];

    EXPECT_FALSE(find_project_root(NULL, out, sizeof(out)));
    EXPECT_FALSE(find_project_root("main.vigil", NULL, 0U));
}

TEST_F(CliFrontendTest, FindProjectRootRejectsTooSmallOutputBuffer)
{
    const char *root = "cli_frontend_small_buffer";
    char path[4096];
    char out[4];

    remove_cli_frontend_project(root, "helper", ERR);
    ASSERT_TRUE(setup_project_root_fixture(root, 1, ERR));
    snprintf(path, sizeof(path), "%s/test/main_test.vigil", root);

    EXPECT_FALSE(find_project_root(path, out, sizeof(out)));

    remove_cli_frontend_project(root, "helper", ERR);
}

TEST_F(CliFrontendTest, RegisterSourceTreeRegistersProjectLibImports)
{
    const char *root = "cli_frontend_register_proj";
    remove_cli_frontend_project(root, "helper", ERR);
    ASSERT_TRUE(verify_registered_helper_module(root, ERR));
    remove_cli_frontend_project(root, "helper", ERR);
}

TEST_F(CliFrontendTest, RegisterSourceTreeFailsForMissingImport)
{
    const char *root = "cli_frontend_missing_import";
    const char *main_source = "import \"missing\";\nfn main() -> i32 { return 0; }\n";
    vigil_runtime_t *runtime;
    vigil_source_registry_t registry;
    char main_path[4096];
    char project_root[4096];

    runtime = NULL;
    remove_cli_frontend_project(root, "missing", ERR);
    ASSERT_TRUE(setup_register_project_fixture(root, NULL, main_source, ERR));
    snprintf(main_path, sizeof(main_path), "%s/main.vigil", root);

    ASSERT_TRUE(find_project_root(main_path, project_root, sizeof(project_root)));
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_runtime_open(&runtime, NULL, ERR));
    vigil_source_registry_init(&registry, runtime);

    EXPECT_FALSE(register_source_tree(&registry, main_path, project_root, NULL, ERR));
    EXPECT_TRUE(strcmp(vigil_error_message(ERR), "failed to read imported source") == 0);

    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
    remove_cli_frontend_project(root, "missing", ERR);
}

TEST_F(CliFrontendTest, RegisterSourceTreeHandlesStdlibOnlyImport)
{
    const char *root = "cli_frontend_stdlib_import";

    ASSERT_TRUE(verify_stdlib_only_import_registration(root, ERR));
}

TEST_F(CliFrontendTest, RegisterSourceTreeReturnsExistingSourceId)
{
    const char *root = "cli_frontend_existing_source";

    ASSERT_TRUE(verify_existing_source_registration(root, ERR));
}

TEST_F(CliFrontendTest, RegisterSourceTreeHandlesAbsoluteImportPath)
{
    const char *root = "cli_frontend_absolute_import";

    ASSERT_TRUE(verify_absolute_import_registration(root, ERR));
}

#undef ERR

void register_cli_frontend_tests(void)
{
    REGISTER_TEST_F(CliFrontendTest, FindProjectRootReturnsProjectDirectory);
    REGISTER_TEST_F(CliFrontendTest, FindProjectRootReturnsZeroWithoutManifest);
    REGISTER_TEST_F(CliFrontendTest, FindProjectRootRejectsNullInputs);
    REGISTER_TEST_F(CliFrontendTest, FindProjectRootRejectsTooSmallOutputBuffer);
    REGISTER_TEST_F(CliFrontendTest, RegisterSourceTreeRegistersProjectLibImports);
    REGISTER_TEST_F(CliFrontendTest, RegisterSourceTreeFailsForMissingImport);
    REGISTER_TEST_F(CliFrontendTest, RegisterSourceTreeHandlesStdlibOnlyImport);
    REGISTER_TEST_F(CliFrontendTest, RegisterSourceTreeReturnsExistingSourceId);
    REGISTER_TEST_F(CliFrontendTest, RegisterSourceTreeHandlesAbsoluteImportPath);
}
