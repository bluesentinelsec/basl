/* Tests for thread.spawn with arguments and closures. */
#include "vigil_test.h"

#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#define close _close
#else
#include <unistd.h>
#endif

#include <string.h>

#include "internal/vigil_nanbox.h"
#include "vigil/stdlib.h"
#include "vigil/vigil.h"

/* ── test harness ────────────────────────────────────────────────── */

#ifndef __EMSCRIPTEN__

static int64_t RunWithStdlib(int *vigil_test_failed_, const char *source_text)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_native_registry_t natives;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_value_t result;
    vigil_source_id_t source_id = 0U;
    int64_t output = 0;

    EXPECT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_native_registry_init(&natives);
    EXPECT_EQ(vigil_stdlib_register_all(&natives, &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_source_registry_register_cstr(&registry, "main.vigil", source_text, &source_id, &error),
              VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_compile_source_with_natives(&registry, source_id, &natives, &function, &diagnostics, &error),
              VIGIL_STATUS_OK);
    EXPECT_NE(function, NULL);
    EXPECT_EQ(vigil_diagnostic_list_count(&diagnostics), 0U);

    vigil_value_init_nil(&result);
    EXPECT_EQ(vigil_vm_execute_function(vm, function, &result, &error), VIGIL_STATUS_OK);

    if (vigil_nanbox_is_int(result))
    {
        output = vigil_nanbox_decode_int(result);
    }

    vigil_value_release(&result);
    if (function)
        vigil_object_release(&function);
    vigil_native_registry_free(&natives);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    return output;
}

/* ── Tests ───────────────────────────────────────────────────────── */

TEST(VigilThreadTest, SpawnZeroArityFunction)
{
    int64_t r = RunWithStdlib(vigil_test_failed_, "import \"atomic\";\n"
                                                  "import \"thread\";\n"
                                                  "fn main() -> i32 {\n"
                                                  "    i64 a, err _ = atomic.new(i64(0));\n"
                                                  "    i64 t = thread.spawn(fn() -> void {\n"
                                                  "        atomic.store(a, i64(42));\n"
                                                  "    });\n"
                                                  "    thread.join(t);\n"
                                                  "    return i32(atomic.load(a));\n"
                                                  "}\n");
    EXPECT_EQ(r, 42);
}

TEST(VigilThreadTest, SpawnWithClosureCapture)
{
    int64_t r = RunWithStdlib(vigil_test_failed_, "import \"atomic\";\n"
                                                  "import \"thread\";\n"
                                                  "fn main() -> i32 {\n"
                                                  "    i64 a, err _ = atomic.new(i64(0));\n"
                                                  "    i64 val = i64(99);\n"
                                                  "    i64 t = thread.spawn(fn() -> void {\n"
                                                  "        atomic.store(a, val);\n"
                                                  "    });\n"
                                                  "    thread.join(t);\n"
                                                  "    return i32(atomic.load(a));\n"
                                                  "}\n");
    EXPECT_EQ(r, 99);
}

TEST(VigilThreadTest, SpawnClosureWithMultipleCaptures)
{
    int64_t r = RunWithStdlib(vigil_test_failed_, "import \"atomic\";\n"
                                                  "import \"thread\";\n"
                                                  "fn main() -> i32 {\n"
                                                  "    i64 result, err _ = atomic.new(i64(0));\n"
                                                  "    i64 a = i64(30);\n"
                                                  "    i64 b = i64(12);\n"
                                                  "    i64 t = thread.spawn(fn() -> void {\n"
                                                  "        atomic.store(result, a + b);\n"
                                                  "    });\n"
                                                  "    thread.join(t);\n"
                                                  "    return i32(atomic.load(result));\n"
                                                  "}\n");
    EXPECT_EQ(r, 42);
}

TEST(VigilThreadTest, SpawnMultipleThreadsWithClosures)
{
    int64_t r =
        RunWithStdlib(vigil_test_failed_, "import \"atomic\";\n"
                                          "import \"thread\";\n"
                                          "fn main() -> i32 {\n"
                                          "    i64 sum, err _ = atomic.new(i64(0));\n"
                                          "    i64 t1 = thread.spawn(fn() -> void { atomic.add(sum, i64(10)); });\n"
                                          "    i64 t2 = thread.spawn(fn() -> void { atomic.add(sum, i64(20)); });\n"
                                          "    i64 t3 = thread.spawn(fn() -> void { atomic.add(sum, i64(30)); });\n"
                                          "    thread.join(t1);\n"
                                          "    thread.join(t2);\n"
                                          "    thread.join(t3);\n"
                                          "    return i32(atomic.load(sum));\n"
                                          "}\n");
    EXPECT_EQ(r, 60);
}

TEST(VigilThreadTest, SpawnWithMutexCoordination)
{
    int64_t r = RunWithStdlib(vigil_test_failed_,
                              "import \"atomic\";\n"
                              "import \"thread\";\n"
                              "fn increment(i64 mtx, i64 counter, i64 times) -> void {\n"
                              "    for (i64 i = i64(0); i < times; i = i + i64(1)) {\n"
                              "        thread.lock(mtx);\n"
                              "        i64 cur = atomic.load(counter);\n"
                              "        atomic.store(counter, cur + i64(1));\n"
                              "        thread.unlock(mtx);\n"
                              "    }\n"
                              "}\n"
                              "fn main() -> i32 {\n"
                              "    i64 mtx = thread.mutex();\n"
                              "    i64 counter, err _ = atomic.new(i64(0));\n"
                              "    i64 t1 = thread.spawn(fn() -> void { increment(mtx, counter, i64(100)); });\n"
                              "    i64 t2 = thread.spawn(fn() -> void { increment(mtx, counter, i64(100)); });\n"
                              "    thread.join(t1);\n"
                              "    thread.join(t2);\n"
                              "    return i32(atomic.load(counter));\n"
                              "}\n");
    EXPECT_EQ(r, 200);
}

TEST(VigilThreadTest, ReturnPeepholeDoesNotRewriteLoopOperandBytes)
{
    int64_t r = RunWithStdlib(vigil_test_failed_, "import \"atomic\";\n"
                                                  "import \"thread\";\n"
                                                  "const i64 POISON_PILL = i64(-999);\n"
                                                  "fn rb_try_dequeue() -> i64 {\n"
                                                  "    return POISON_PILL;\n"
                                                  "}\n"
                                                  "fn rb_dequeue(i64 shutdown_flag) -> i64 {\n"
                                                  "    for (;;) {\n"
                                                  "        i64 val = rb_try_dequeue();\n"
                                                  "        if val != POISON_PILL {\n"
                                                  "            return val;\n"
                                                  "        }\n"
                                                  "        if atomic.load(shutdown_flag) != i64(0) {\n"
                                                  "            val = rb_try_dequeue();\n"
                                                  "            if val != POISON_PILL {\n"
                                                  "                return val;\n"
                                                  "            }\n"
                                                  "            return POISON_PILL;\n"
                                                  "        }\n"
                                                  "        thread.yield();\n"
                                                  "    }\n"
                                                  "    return POISON_PILL;\n"
                                                  "}\n"
                                                  "fn main() -> i32 {\n"
                                                  "    i64 shutdown, err _ = atomic.new(i64(1));\n"
                                                  "    return i32(rb_dequeue(shutdown));\n"
                                                  "}\n");
    EXPECT_EQ(r, -999);
}

#endif /* __EMSCRIPTEN__ */

/* ── Registration ────────────────────────────────────────────────── */

void register_thread_tests(void)
{
#ifndef __EMSCRIPTEN__
    REGISTER_TEST(VigilThreadTest, SpawnZeroArityFunction);
    REGISTER_TEST(VigilThreadTest, SpawnWithClosureCapture);
    REGISTER_TEST(VigilThreadTest, SpawnClosureWithMultipleCaptures);
    REGISTER_TEST(VigilThreadTest, SpawnMultipleThreadsWithClosures);
    REGISTER_TEST(VigilThreadTest, SpawnWithMutexCoordination);
    REGISTER_TEST(VigilThreadTest, ReturnPeepholeDoesNotRewriteLoopOperandBytes);
#endif
}
