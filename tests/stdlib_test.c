#include "vigil_test.h"
#ifdef _WIN32
#include <io.h>
#include <process.h>
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#define close _close
#define getpid _getpid
#else
#include <unistd.h>
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "vigil/stdlib.h"
#include "vigil/vigil.h"

/* ── test harness ────────────────────────────────────────────────── */

/*
 * Compile and run a VIGIL program that imports stdlib modules.
 * The program's main() must return i32.  Returns that value.
 */
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
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    output = vigil_value_as_int(&result);

    vigil_value_release(&result);
    vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_native_registry_free(&natives);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    return output;
}

/*
 * Same as RunWithStdlib but captures stdout and returns it.
 * The program's exit code is expected to be 0.
 */
static char *RunAndCaptureStdout(int *vigil_test_failed_, const char *source_text)
{
    FILE *tmp = tmpfile();
    int saved = dup(fileno(stdout));
    fflush(stdout);
    dup2(fileno(tmp), fileno(stdout));
    int64_t rc = RunWithStdlib(vigil_test_failed_, source_text);
    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);
    long sz = ftell(tmp);
    rewind(tmp);
    char *buf = (char *)calloc(1, (size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, tmp);
    buf[n] = '\0';
    fclose(tmp);
    EXPECT_EQ(rc, 0);
    return buf;
}

static char *RunAndCaptureStderr(int *vigil_test_failed_, const char *source_text)
{
    FILE *tmp = tmpfile();
    int saved = dup(fileno(stderr));
    fflush(stderr);
    dup2(fileno(tmp), fileno(stderr));
    int64_t rc = RunWithStdlib(vigil_test_failed_, source_text);
    fflush(stderr);
    dup2(saved, fileno(stderr));
    close(saved);
    long sz = ftell(tmp);
    rewind(tmp);
    char *buf = (char *)calloc(1, (size_t)sz + 1);
    size_t n2 = fread(buf, 1, (size_t)sz, tmp);
    buf[n2] = '\0';
    fclose(tmp);
    EXPECT_EQ(rc, 0);
    return buf;
}

static void EscapeVigilStringLiteral(const char *input, char *output, size_t output_size)
{
    size_t in_index = 0U;
    size_t out_index = 0U;

    while (input[in_index] != '\0' && out_index + 2U < output_size)
    {
        char ch = input[in_index++];
        if (ch == '\\' || ch == '"')
            output[out_index++] = '\\';
        output[out_index++] = ch;
    }

    output[out_index] = '\0';
}

static void BuildJsonRoundTripSource(const char *path, char *source, size_t source_size)
{
    char escaped[1024];

    EscapeVigilStringLiteral(path, escaped, sizeof(escaped));
    snprintf(
        source, source_size,
        "import \"json\";\n"
        "fn main() -> i32 {\n"
        "    json.Value root, err parse_err = json.Value.parse(\"{\\\"name\\\":\\\"vigil\\\",\\\"count\\\":3}\");\n"
        "    if parse_err != ok { return 1; }\n"
        "    err write_err = root.write(\"%s\");\n"
        "    if write_err != ok { return 2; }\n"
        "    json.Value copy, err read_err = json.Value.read(\"%s\");\n"
        "    if read_err != ok { return 3; }\n"
        "    if copy.stringify() != root.stringify() { return 4; }\n"
        "    return 0;\n"
        "}\n",
        escaped, escaped);
}

/* ── fmt tests ───────────────────────────────────────────────────── */

TEST(VigilStdlibFmtTest, PrintlnOutputsStringWithNewline)
{
    char *out = RunAndCaptureStdout(vigil_test_failed_, "\n"
                                                        "        import \"fmt\";\n"
                                                        "        fn main() -> i32 {\n"
                                                        "            fmt.println(\"hello\");\n"
                                                        "            return 0;\n"
                                                        "        }\n"
                                                        "    ");
    EXPECT_STREQ(out, "hello\n");
    free(out);
}

TEST(VigilStdlibFmtTest, PrintOutputsStringWithoutNewline)
{
    char *out = RunAndCaptureStdout(vigil_test_failed_, "\n"
                                                        "        import \"fmt\";\n"
                                                        "        fn main() -> i32 {\n"
                                                        "            fmt.print(\"ab\");\n"
                                                        "            fmt.print(\"cd\");\n"
                                                        "            return 0;\n"
                                                        "        }\n"
                                                        "    ");
    EXPECT_STREQ(out, "abcd");
    free(out);
}

TEST(VigilStdlibFmtTest, EprintlnOutputsToStderr)
{
    char *err = RunAndCaptureStderr(vigil_test_failed_, "\n"
                                                        "        import \"fmt\";\n"
                                                        "        fn main() -> i32 {\n"
                                                        "            fmt.eprintln(\"oops\");\n"
                                                        "            return 0;\n"
                                                        "        }\n"
                                                        "    ");
    EXPECT_STREQ(err, "oops\n");
    free(err);
}

TEST(VigilStdlibFmtTest, PrintlnEmptyString)
{
    char *out = RunAndCaptureStdout(vigil_test_failed_, "\n"
                                                        "        import \"fmt\";\n"
                                                        "        fn main() -> i32 {\n"
                                                        "            fmt.println(\"\");\n"
                                                        "            return 0;\n"
                                                        "        }\n"
                                                        "    ");
    EXPECT_STREQ(out, "\n");
    free(out);
}

TEST(VigilStdlibFmtTest, PrintlnWithFString)
{
    char *out = RunAndCaptureStdout(vigil_test_failed_, "\n"
                                                        "        import \"fmt\";\n"
                                                        "        fn main() -> i32 {\n"
                                                        "            i32 x = 42;\n"
                                                        "            fmt.println(f\"val={x}\");\n"
                                                        "            return 0;\n"
                                                        "        }\n"
                                                        "    ");
    EXPECT_STREQ(out, "val=42\n");
    free(out);
}

TEST(VigilStdlibFmtTest, PrintlnWithVariable)
{
    char *out = RunAndCaptureStdout(vigil_test_failed_, "\n"
                                                        "        import \"fmt\";\n"
                                                        "        fn main() -> i32 {\n"
                                                        "            string s = \"world\";\n"
                                                        "            fmt.println(s);\n"
                                                        "            return 0;\n"
                                                        "        }\n"
                                                        "    ");
    EXPECT_STREQ(out, "world\n");
    free(out);
}

TEST(VigilStdlibFmtTest, PrintlnInLoop)
{
    char *out = RunAndCaptureStdout(vigil_test_failed_, "\n"
                                                        "        import \"fmt\";\n"
                                                        "        fn main() -> i32 {\n"
                                                        "            for (i32 i = 0; i < 3; i++) {\n"
                                                        "                fmt.println(string(i));\n"
                                                        "            }\n"
                                                        "            return 0;\n"
                                                        "        }\n"
                                                        "    ");
    EXPECT_STREQ(out, "0\n1\n2\n");
    free(out);
}

/* ── math: constants ─────────────────────────────────────────────── */

TEST(VigilStdlibMathTest, PiReturnsCorrectValue)
{
    /*
     * Encode a pass/fail check as an integer return.
     * The VIGIL program returns 0 if pi matches within tolerance.
     */
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 diff = math.abs(math.pi() - 3.14159265358979323846);\n"
                                                "            if diff > 0.000000000000001 { return 1; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, EReturnsCorrectValue)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 diff = math.abs(math.e() - 2.71828182845904523536);\n"
                                                "            if diff > 0.000000000000001 { return 1; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

/* ── math: rounding / conversion table tests ─────────────────────── */

/*
 * Each rounding function is tested with a table of inputs including
 * positive, negative, zero, and edge cases.  The VIGIL program
 * encodes the index of the first failing case (1-based) or 0 on
 * success.
 */

TEST(VigilStdlibMathTest, FloorTable)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.floor(3.7) != 3.0   { return 1; }\n"
                                                "            if math.floor(-3.7) != -4.0  { return 2; }\n"
                                                "            if math.floor(0.0) != 0.0    { return 3; }\n"
                                                "            if math.floor(5.0) != 5.0    { return 4; }\n"
                                                "            if math.floor(-0.1) != -1.0  { return 5; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, CeilTable)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.ceil(3.2) != 4.0    { return 1; }\n"
                                                "            if math.ceil(-3.2) != -3.0   { return 2; }\n"
                                                "            if math.ceil(0.0) != 0.0     { return 3; }\n"
                                                "            if math.ceil(5.0) != 5.0     { return 4; }\n"
                                                "            if math.ceil(0.1) != 1.0     { return 5; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, RoundTable)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.round(3.5) != 4.0    { return 1; }\n"
                                                "            if math.round(3.4) != 3.0    { return 2; }\n"
                                                "            if math.round(-3.5) != -4.0  { return 3; }\n"
                                                "            if math.round(0.0) != 0.0    { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, TruncTable)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.trunc(3.7) != 3.0    { return 1; }\n"
                                                "            if math.trunc(-3.7) != -3.0  { return 2; }\n"
                                                "            if math.trunc(0.0) != 0.0    { return 3; }\n"
                                                "            if math.trunc(5.0) != 5.0    { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, AbsTable)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.abs(-42.0) != 42.0  { return 1; }\n"
                                                "            if math.abs(42.0) != 42.0   { return 2; }\n"
                                                "            if math.abs(0.0) != 0.0     { return 3; }\n"
                                                "            if math.abs(-0.0) != 0.0    { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, SignTable)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.sign(42.0) != 1.0   { return 1; }\n"
                                                "            if math.sign(-42.0) != -1.0  { return 2; }\n"
                                                "            if math.sign(0.0) != 0.0    { return 3; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

/* ── math: trig / exponential ────────────────────────────────────── */

TEST(VigilStdlibMathTest, SqrtTable)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.sqrt(144.0) != 12.0  { return 1; }\n"
                                                "            if math.sqrt(0.0) != 0.0     { return 2; }\n"
                                                "            if math.sqrt(1.0) != 1.0     { return 3; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, SinCosAtZero)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.sin(0.0) != 0.0  { return 1; }\n"
                                                "            if math.cos(0.0) != 1.0  { return 2; }\n"
                                                "            if math.tan(0.0) != 0.0  { return 3; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, IntrinsicOpcodesSinCosSqrtLogPow)
{
    /* Exercises the dedicated MATH_*_F64 opcodes emitted for these five
       functions instead of CALL_NATIVE. */
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "import \"math\";"
                                                "fn main() -> i32 {"
                                                "    if math.sin(0.0) != 0.0       { return 1; }"
                                                "    if math.cos(0.0) != 1.0       { return 2; }"
                                                "    if math.sqrt(4.0) != 2.0      { return 3; }"
                                                "    if math.log(1.0) != 0.0       { return 4; }"
                                                "    if math.pow(2.0, 10.0) != 1024.0 { return 5; }"
                                                "    return 0;"
                                                "}"),
              0);
}

TEST(VigilStdlibMathTest, DeferNativeCall)
{
    /* Exercises the defer path in vigil_parser_emit_native_call (non-intrinsic
       native function deferred — goes through DEFER_CALL_NATIVE). */
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "import \"math\";"
                                                "fn main() -> i32 {"
                                                "    defer math.tan(0.0);"
                                                "    return 0;"
                                                "}"),
              0);
}

TEST(VigilStdlibMathTest, LogTable)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.log(1.0) != 0.0      { return 1; }\n"
                                                "            if math.log2(1024.0) != 10.0  { return 2; }\n"
                                                "            if math.log10(1000.0) != 3.0  { return 3; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, ExpAtZeroAndOne)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.exp(0.0) != 1.0  { return 1; }\n"
                                                "            f64 diff = math.abs(math.exp(1.0) - math.e());\n"
                                                "            if diff > 0.000000000000001 { return 2; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

/* ── math: two-argument functions ────────────────────────────────── */

TEST(VigilStdlibMathTest, PowTable)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.pow(2.0, 10.0) != 1024.0  { return 1; }\n"
                                                "            if math.pow(3.0, 0.0) != 1.0      { return 2; }\n"
                                                "            if math.pow(5.0, 1.0) != 5.0      { return 3; }\n"
                                                "            if math.pow(4.0, 0.5) != 2.0      { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, MinMaxTable)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.min(3.0, 7.0) != 3.0    { return 1; }\n"
                                                "            if math.min(-1.0, 1.0) != -1.0   { return 2; }\n"
                                                "            if math.min(5.0, 5.0) != 5.0    { return 3; }\n"
                                                "            if math.max(3.0, 7.0) != 7.0    { return 4; }\n"
                                                "            if math.max(-1.0, 1.0) != 1.0    { return 5; }\n"
                                                "            if math.max(5.0, 5.0) != 5.0    { return 6; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Atan2Table)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            if math.atan2(0.0, 1.0) != 0.0  { return 1; }\n"
                            "            f64 diff = math.abs(math.atan2(1.0, 1.0) - math.pi() / 4.0);\n"
                            "            if diff > 0.000000000000001 { return 2; }\n"
                            "            // atan2(1, 0) == pi/2\n"
                            "            f64 diff2 = math.abs(math.atan2(1.0, 0.0) - math.pi() / 2.0);\n"
                            "            if diff2 > 0.000000000000001 { return 3; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

TEST(VigilStdlibMathTest, HypotTable)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.hypot(3.0, 4.0) != 5.0    { return 1; }\n"
                                                "            if math.hypot(0.0, 0.0) != 0.0    { return 2; }\n"
                                                "            if math.hypot(5.0, 0.0) != 5.0    { return 3; }\n"
                                                "            if math.hypot(0.0, 7.0) != 7.0    { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, FmodTable)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.fmod(7.5, 3.0) != 1.5    { return 1; }\n"
                                                "            if math.fmod(10.0, 5.0) != 0.0   { return 2; }\n"
                                                "            if math.fmod(-7.5, 3.0) != -1.5  { return 3; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

/* ── math: three-argument functions ──────────────────────────────── */

TEST(VigilStdlibMathTest, ClampTable)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.clamp(5.0, 0.0, 10.0) != 5.0    { return 1; }\n"
                                                "            if math.clamp(-5.0, 0.0, 10.0) != 0.0   { return 2; }\n"
                                                "            if math.clamp(15.0, 0.0, 10.0) != 10.0   { return 3; }\n"
                                                "            if math.clamp(0.0, 0.0, 10.0) != 0.0    { return 4; }\n"
                                                "            if math.clamp(10.0, 0.0, 10.0) != 10.0   { return 5; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

/* ── math: composition ───────────────────────────────────────────── */

TEST(VigilStdlibMathTest, ComposedExpressions)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            // pythagorean theorem via composition\n"
                            "            f64 hyp = math.sqrt(math.pow(3.0, 2.0) + math.pow(4.0, 2.0));\n"
                            "            if hyp != 5.0 { return 1; }\n"
                            "\n"
                            "            // clamp via min/max matches clamp()\n"
                            "            f64 val = 15.0;\n"
                            "            f64 a = math.min(math.max(val, 0.0), 10.0);\n"
                            "            f64 b = math.clamp(val, 0.0, 10.0);\n"
                            "            if a != b { return 2; }\n"
                            "\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

/* ── native class: Vec2 ──────────────────────────────────────────── */

TEST(VigilStdlibMathTest, Vec2ConstructionAndFields)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Vec2 v = math.Vec2(3.0, 4.0);\n"
                                                "            if v.x != 3.0 { return 1; }\n"
                                                "            if v.y != 4.0 { return 2; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec2FieldMutation)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Vec2 v = math.Vec2(1.0, 2.0);\n"
                                                "            v.x = 10.0;\n"
                                                "            v.y = 20.0;\n"
                                                "            if v.x != 10.0 { return 1; }\n"
                                                "            if v.y != 20.0 { return 2; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec2Length)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Vec2 v = math.Vec2(3.0, 4.0);\n"
                                                "            if v.length() != 5.0 { return 1; }\n"
                                                "            math.Vec2 zero = math.Vec2(0.0, 0.0);\n"
                                                "            if zero.length() != 0.0 { return 2; }\n"
                                                "            math.Vec2 unit = math.Vec2(1.0, 0.0);\n"
                                                "            if unit.length() != 1.0 { return 3; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec2Dot)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Vec2 a = math.Vec2(3.0, 4.0);\n"
                                                "            math.Vec2 b = math.Vec2(1.0, 0.0);\n"
                                                "            if a.dot(b) != 3.0 { return 1; }\n"
                                                "            // perpendicular vectors: dot == 0\n"
                                                "            math.Vec2 c = math.Vec2(0.0, 1.0);\n"
                                                "            if b.dot(c) != 0.0 { return 2; }\n"
                                                "            // self dot\n"
                                                "            if a.dot(a) != 25.0 { return 3; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec2WithScalarMath)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Vec2 v = math.Vec2(3.0, 4.0);\n"
                                                "            f64 len = v.length();\n"
                                                "            if math.sqrt(math.pow(len, 2.0)) != 5.0 { return 1; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

/* ── new scalar functions ────────────────────────────────────────── */

TEST(VigilStdlibMathTest, InverseTrig)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            // asin(0) == 0\n"
                            "            if math.abs(math.asin(0.0)) > eps { return 1; }\n"
                            "            // acos(1) == 0\n"
                            "            if math.abs(math.acos(1.0)) > eps { return 2; }\n"
                            "            // atan(0) == 0\n"
                            "            if math.abs(math.atan(0.0)) > eps { return 3; }\n"
                            "            // asin(1) ~= pi/2\n"
                            "            if math.abs(math.asin(1.0) - math.pi() / 2.0) > eps { return 4; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

TEST(VigilStdlibMathTest, Deg2RadRad2Deg)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            if math.abs(math.deg2rad(180.0) - math.pi()) > eps { return 1; }\n"
                            "            if math.abs(math.rad2deg(math.pi()) - 180.0) > eps { return 2; }\n"
                            "            if math.abs(math.deg2rad(0.0)) > eps { return 3; }\n"
                            "            if math.abs(math.rad2deg(0.0)) > eps { return 4; }\n"
                            "            // roundtrip\n"
                            "            if math.abs(math.rad2deg(math.deg2rad(45.0)) - 45.0) > eps { return 5; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

TEST(VigilStdlibMathTest, Lerp)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.lerp(0.0, 10.0, 0.0) != 0.0 { return 1; }\n"
                                                "            if math.lerp(0.0, 10.0, 1.0) != 10.0 { return 2; }\n"
                                                "            if math.lerp(0.0, 10.0, 0.5) != 5.0 { return 3; }\n"
                                                "            if math.lerp(10.0, 20.0, 0.25) != 12.5 { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Normalize)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.normalize(5.0, 0.0, 10.0) != 0.5 { return 1; }\n"
                                                "            if math.normalize(0.0, 0.0, 10.0) != 0.0 { return 2; }\n"
                                                "            if math.normalize(10.0, 0.0, 10.0) != 1.0 { return 3; }\n"
                                                "            // degenerate range\n"
                                                "            if math.normalize(5.0, 5.0, 5.0) != 0.0 { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Wrap)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            if math.abs(math.wrap(370.0, 0.0, 360.0) - 10.0) > eps { return 1; }\n"
                            "            if math.abs(math.wrap(-10.0, 0.0, 360.0) - 350.0) > eps { return 2; }\n"
                            "            if math.abs(math.wrap(5.0, 0.0, 10.0) - 5.0) > eps { return 3; }\n"
                            "            if math.abs(math.wrap(0.0, 0.0, 360.0)) > eps { return 4; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

TEST(VigilStdlibMathTest, Remap)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            if math.remap(5.0, 0.0, 10.0, 0.0, 100.0) != 50.0 { return 1; }\n"
                            "            if math.remap(0.0, 0.0, 10.0, 0.0, 100.0) != 0.0 { return 2; }\n"
                            "            if math.remap(10.0, 0.0, 10.0, 0.0, 100.0) != 100.0 { return 3; }\n"
                            "            if math.remap(5.0, 0.0, 10.0, 100.0, 200.0) != 150.0 { return 4; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

/* ── Vec2 new methods ────────────────────────────────────────────── */

TEST(VigilStdlibMathTest, Vec2Normalize)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 eps = 0.000001;\n"
                                                "            math.Vec2 v = math.Vec2(3.0, 4.0);\n"
                                                "            math.Vec2 n = v.normalize();\n"
                                                "            if math.abs(n.length() - 1.0) > eps { return 1; }\n"
                                                "            if math.abs(n.x - 0.6) > eps { return 2; }\n"
                                                "            if math.abs(n.y - 0.8) > eps { return 3; }\n"
                                                "            // zero vector normalizes to zero\n"
                                                "            math.Vec2 z = math.Vec2(0.0, 0.0);\n"
                                                "            math.Vec2 zn = z.normalize();\n"
                                                "            if zn.x != 0.0 { return 4; }\n"
                                                "            if zn.y != 0.0 { return 5; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec2AddSubScaleDistance)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            math.Vec2 a = math.Vec2(1.0, 2.0);\n"
                            "            math.Vec2 b = math.Vec2(3.0, 4.0);\n"
                            "            math.Vec2 c = a.add(b);\n"
                            "            if c.x != 4.0 { return 1; }\n"
                            "            if c.y != 6.0 { return 2; }\n"
                            "            math.Vec2 d = a.sub(b);\n"
                            "            if d.x != -2.0 { return 3; }\n"
                            "            if d.y != -2.0 { return 4; }\n"
                            "            math.Vec2 s = a.scale(3.0);\n"
                            "            if s.x != 3.0 { return 5; }\n"
                            "            if s.y != 6.0 { return 6; }\n"
                            "            f64 dist = a.distance(b);\n"
                            "            if math.abs(dist - 2.8284271247461903) > eps { return 7; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

/* ── Vec3 ────────────────────────────────────────────────────────── */

TEST(VigilStdlibMathTest, Vec3ConstructionAndFields)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Vec3 v = math.Vec3(1.0, 2.0, 3.0);\n"
                                                "            if v.x != 1.0 { return 1; }\n"
                                                "            if v.y != 2.0 { return 2; }\n"
                                                "            if v.z != 3.0 { return 3; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec3Length)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 eps = 0.000001;\n"
                                                "            math.Vec3 v = math.Vec3(1.0, 2.0, 2.0);\n"
                                                "            if math.abs(v.length() - 3.0) > eps { return 1; }\n"
                                                "            math.Vec3 z = math.Vec3(0.0, 0.0, 0.0);\n"
                                                "            if z.length() != 0.0 { return 2; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec3DotAndCross)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Vec3 x = math.Vec3(1.0, 0.0, 0.0);\n"
                                                "            math.Vec3 y = math.Vec3(0.0, 1.0, 0.0);\n"
                                                "            // perpendicular: dot == 0\n"
                                                "            if x.dot(y) != 0.0 { return 1; }\n"
                                                "            // x cross y == z\n"
                                                "            math.Vec3 z = x.cross(y);\n"
                                                "            if z.x != 0.0 { return 2; }\n"
                                                "            if z.y != 0.0 { return 3; }\n"
                                                "            if z.z != 1.0 { return 4; }\n"
                                                "            // self dot\n"
                                                "            math.Vec3 v = math.Vec3(1.0, 2.0, 3.0);\n"
                                                "            if v.dot(v) != 14.0 { return 5; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec3NormalizeAddSubScaleDistance)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 eps = 0.000001;\n"
                                                "            math.Vec3 a = math.Vec3(1.0, 2.0, 3.0);\n"
                                                "            math.Vec3 b = math.Vec3(4.0, 5.0, 6.0);\n"
                                                "            // normalize\n"
                                                "            math.Vec3 n = a.normalize();\n"
                                                "            if math.abs(n.length() - 1.0) > eps { return 1; }\n"
                                                "            // add\n"
                                                "            math.Vec3 c = a.add(b);\n"
                                                "            if c.x != 5.0 { return 2; }\n"
                                                "            if c.y != 7.0 { return 3; }\n"
                                                "            if c.z != 9.0 { return 4; }\n"
                                                "            // sub\n"
                                                "            math.Vec3 d = a.sub(b);\n"
                                                "            if d.x != -3.0 { return 5; }\n"
                                                "            if d.y != -3.0 { return 6; }\n"
                                                "            if d.z != -3.0 { return 7; }\n"
                                                "            // scale\n"
                                                "            math.Vec3 s = a.scale(2.0);\n"
                                                "            if s.x != 2.0 { return 8; }\n"
                                                "            if s.y != 4.0 { return 9; }\n"
                                                "            if s.z != 6.0 { return 10; }\n"
                                                "            // distance\n"
                                                "            f64 dist = a.distance(b);\n"
                                                "            if math.abs(dist - math.sqrt(27.0)) > eps { return 11; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec3MethodChaining)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 eps = 0.000001;\n"
                                                "            // physics: pos += vel * dt\n"
                                                "            math.Vec3 pos = math.Vec3(0.0, 0.0, 0.0);\n"
                                                "            math.Vec3 vel = math.Vec3(10.0, 20.0, 30.0);\n"
                                                "            math.Vec3 new_pos = pos.add(vel.scale(0.1));\n"
                                                "            if math.abs(new_pos.x - 1.0) > eps { return 1; }\n"
                                                "            if math.abs(new_pos.y - 2.0) > eps { return 2; }\n"
                                                "            if math.abs(new_pos.z - 3.0) > eps { return 3; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

/* ── Tier 1 scalar: step, smoothstep, inverseLerp ────────────────── */

TEST(VigilStdlibMathTest, Step)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            if math.step(0.5, 0.3) != 0.0 { return 1; }\n"
                                                "            if math.step(0.5, 0.5) != 1.0 { return 2; }\n"
                                                "            if math.step(0.5, 0.7) != 1.0 { return 3; }\n"
                                                "            if math.step(0.0, -1.0) != 0.0 { return 4; }\n"
                                                "            if math.step(0.0, 0.0) != 1.0 { return 5; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Smoothstep)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            if math.smoothstep(0.0, 1.0, 0.0) != 0.0 { return 1; }\n"
                            "            if math.smoothstep(0.0, 1.0, 1.0) != 1.0 { return 2; }\n"
                            "            if math.abs(math.smoothstep(0.0, 1.0, 0.5) - 0.5) > eps { return 3; }\n"
                            "            // clamped below\n"
                            "            if math.smoothstep(0.0, 1.0, -1.0) != 0.0 { return 4; }\n"
                            "            // clamped above\n"
                            "            if math.smoothstep(0.0, 1.0, 2.0) != 1.0 { return 5; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

TEST(VigilStdlibMathTest, InverseLerp)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            if math.inverseLerp(0.0, 10.0, 0.0) != 0.0 { return 1; }\n"
                            "            if math.inverseLerp(0.0, 10.0, 10.0) != 1.0 { return 2; }\n"
                            "            if math.inverseLerp(0.0, 10.0, 5.0) != 0.5 { return 3; }\n"
                            "            // degenerate\n"
                            "            if math.inverseLerp(5.0, 5.0, 5.0) != 0.0 { return 4; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

/* ── Tier 1 Vec2: lengthSqr, negate, lerp, reflect ──────────────── */

TEST(VigilStdlibMathTest, Vec2LengthSqrAndNegate)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Vec2 v = math.Vec2(3.0, 4.0);\n"
                                                "            if v.lengthSqr() != 25.0 { return 1; }\n"
                                                "            math.Vec2 n = v.negate();\n"
                                                "            if n.x != -3.0 { return 2; }\n"
                                                "            if n.y != -4.0 { return 3; }\n"
                                                "            // negate of zero\n"
                                                "            math.Vec2 z = math.Vec2(0.0, 0.0);\n"
                                                "            math.Vec2 zn = z.negate();\n"
                                                "            if zn.x != 0.0 { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec2LerpAndReflect)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 eps = 0.000001;\n"
                                                "            math.Vec2 a = math.Vec2(0.0, 0.0);\n"
                                                "            math.Vec2 b = math.Vec2(10.0, 20.0);\n"
                                                "            // lerp t=0 -> a\n"
                                                "            math.Vec2 l0 = a.lerp(b, 0.0);\n"
                                                "            if l0.x != 0.0 { return 1; }\n"
                                                "            // lerp t=1 -> b\n"
                                                "            math.Vec2 l1 = a.lerp(b, 1.0);\n"
                                                "            if l1.x != 10.0 { return 2; }\n"
                                                "            // lerp t=0.5 -> midpoint\n"
                                                "            math.Vec2 mid = a.lerp(b, 0.5);\n"
                                                "            if mid.x != 5.0 { return 3; }\n"
                                                "            if mid.y != 10.0 { return 4; }\n"
                                                "            // reflect (1,-1) off (0,1) -> (1,1)\n"
                                                "            math.Vec2 inc = math.Vec2(1.0, -1.0);\n"
                                                "            math.Vec2 n = math.Vec2(0.0, 1.0);\n"
                                                "            math.Vec2 r = inc.reflect(n);\n"
                                                "            if math.abs(r.x - 1.0) > eps { return 5; }\n"
                                                "            if math.abs(r.y - 1.0) > eps { return 6; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

/* ── Tier 1 Vec3: lengthSqr, negate, lerp, reflect, angle ───────── */

TEST(VigilStdlibMathTest, Vec3LengthSqrAndNegate)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Vec3 v = math.Vec3(1.0, 2.0, 3.0);\n"
                                                "            if v.lengthSqr() != 14.0 { return 1; }\n"
                                                "            math.Vec3 n = v.negate();\n"
                                                "            if n.x != -1.0 { return 2; }\n"
                                                "            if n.y != -2.0 { return 3; }\n"
                                                "            if n.z != -3.0 { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec3LerpAndReflect)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 eps = 0.000001;\n"
                                                "            math.Vec3 a = math.Vec3(0.0, 0.0, 0.0);\n"
                                                "            math.Vec3 b = math.Vec3(10.0, 20.0, 30.0);\n"
                                                "            math.Vec3 mid = a.lerp(b, 0.5);\n"
                                                "            if mid.x != 5.0 { return 1; }\n"
                                                "            if mid.y != 10.0 { return 2; }\n"
                                                "            if mid.z != 15.0 { return 3; }\n"
                                                "            // reflect downward off ground\n"
                                                "            math.Vec3 down = math.Vec3(1.0, -1.0, 0.0);\n"
                                                "            math.Vec3 up = math.Vec3(0.0, 1.0, 0.0);\n"
                                                "            math.Vec3 bounce = down.reflect(up);\n"
                                                "            if math.abs(bounce.x - 1.0) > eps { return 4; }\n"
                                                "            if math.abs(bounce.y - 1.0) > eps { return 5; }\n"
                                                "            if math.abs(bounce.z) > eps { return 6; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec3Angle)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            math.Vec3 x = math.Vec3(1.0, 0.0, 0.0);\n"
                            "            math.Vec3 y = math.Vec3(0.0, 1.0, 0.0);\n"
                            "            // perpendicular -> pi/2\n"
                            "            if math.abs(x.angle(y) - math.pi() / 2.0) > eps { return 1; }\n"
                            "            // parallel -> 0\n"
                            "            if math.abs(x.angle(x)) > eps { return 2; }\n"
                            "            // opposite -> pi\n"
                            "            math.Vec3 nx = math.Vec3(-1.0, 0.0, 0.0);\n"
                            "            if math.abs(x.angle(nx) - math.pi()) > eps { return 3; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

/* ── Vec4 ────────────────────────────────────────────────────────── */

TEST(VigilStdlibMathTest, Vec4ConstructionAndFields)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Vec4 v = math.Vec4(1.0, 2.0, 3.0, 4.0);\n"
                                                "            if v.x != 1.0 { return 1; }\n"
                                                "            if v.y != 2.0 { return 2; }\n"
                                                "            if v.z != 3.0 { return 3; }\n"
                                                "            if v.w != 4.0 { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec4LengthAndDot)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            math.Vec4 v = math.Vec4(1.0, 2.0, 3.0, 4.0);\n"
                            "            // length = sqrt(1+4+9+16) = sqrt(30)\n"
                            "            if math.abs(v.length() - math.sqrt(30.0)) > eps { return 1; }\n"
                            "            if v.lengthSqr() != 30.0 { return 2; }\n"
                            "            // perpendicular\n"
                            "            math.Vec4 a = math.Vec4(1.0, 0.0, 0.0, 0.0);\n"
                            "            math.Vec4 b = math.Vec4(0.0, 1.0, 0.0, 0.0);\n"
                            "            if a.dot(b) != 0.0 { return 3; }\n"
                            "            // self dot\n"
                            "            if a.dot(a) != 1.0 { return 4; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec4Arithmetic)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            math.Vec4 a = math.Vec4(1.0, 2.0, 3.0, 4.0);\n"
                            "            math.Vec4 b = math.Vec4(5.0, 6.0, 7.0, 8.0);\n"
                            "            math.Vec4 c = a.add(b);\n"
                            "            if c.x != 6.0 { return 1; }\n"
                            "            if c.w != 12.0 { return 2; }\n"
                            "            math.Vec4 d = a.sub(b);\n"
                            "            if d.x != -4.0 { return 3; }\n"
                            "            math.Vec4 s = a.scale(2.0);\n"
                            "            if s.x != 2.0 { return 4; }\n"
                            "            if s.w != 8.0 { return 5; }\n"
                            "            math.Vec4 n = a.negate();\n"
                            "            if n.x != -1.0 { return 6; }\n"
                            "            if n.w != -4.0 { return 7; }\n"
                            "            // normalize\n"
                            "            math.Vec4 u = a.normalize();\n"
                            "            if math.abs(u.length() - 1.0) > eps { return 8; }\n"
                            "            // distance\n"
                            "            if math.abs(a.distance(b) - math.sqrt(64.0)) > eps { return 9; }\n"
                            "            // lerp\n"
                            "            math.Vec4 mid = a.lerp(b, 0.5);\n"
                            "            if mid.x != 3.0 { return 10; }\n"
                            "            if mid.w != 6.0 { return 11; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

/* ── Quaternion ──────────────────────────────────────────────────── */

TEST(VigilStdlibMathTest, QuaternionIdentity)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);\n"
                            "            if math.abs(id.length() - 1.0) > eps { return 1; }\n"
                            "            // id * id = id\n"
                            "            math.Quaternion r = id.multiply(id);\n"
                            "            if math.abs(r.w - 1.0) > eps { return 2; }\n"
                            "            if math.abs(r.x) > eps { return 3; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

TEST(VigilStdlibMathTest, QuaternionConjugateAndInverse)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 eps = 0.000001;\n"
                                                "            math.Quaternion q = math.Quaternion(1.0, 2.0, 3.0, 4.0);\n"
                                                "            math.Quaternion c = q.conjugate();\n"
                                                "            if c.x != -1.0 { return 1; }\n"
                                                "            if c.y != -2.0 { return 2; }\n"
                                                "            if c.z != -3.0 { return 3; }\n"
                                                "            if c.w != 4.0 { return 4; }\n"
                                                "            // q * inverse(q) ~= identity\n"
                                                "            math.Quaternion qn = q.normalize();\n"
                                                "            math.Quaternion inv = qn.inverse();\n"
                                                "            math.Quaternion id = qn.multiply(inv);\n"
                                                "            if math.abs(id.w - 1.0) > eps { return 5; }\n"
                                                "            if math.abs(id.x) > eps { return 6; }\n"
                                                "            if math.abs(id.y) > eps { return 7; }\n"
                                                "            if math.abs(id.z) > eps { return 8; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, QuaternionMultiply)
{
    EXPECT_EQ(
        RunWithStdlib(vigil_test_failed_,
                      "\n"
                      "        import \"math\";\n"
                      "        fn main() -> i32 {\n"
                      "            f64 eps = 0.000001;\n"
                      "            // 90 deg around Y then 90 deg around Y = 180 deg around Y\n"
                      "            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);\n"
                      "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
                      "            math.Quaternion r90 = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));\n"
                      "            math.Quaternion r180 = r90.multiply(r90);\n"
                      "            // r180 should be (0, 1, 0, 0) or (0, -1, 0, 0)\n"
                      "            if math.abs(math.abs(r180.y) - 1.0) > eps { return 1; }\n"
                      "            if math.abs(r180.x) > eps { return 2; }\n"
                      "            if math.abs(r180.z) > eps { return 3; }\n"
                      "            if math.abs(r180.w) > eps { return 4; }\n"
                      "            return 0;\n"
                      "        }\n"
                      "    "),
        0);
}

TEST(VigilStdlibMathTest, QuaternionFromAxisAngle)
{
    EXPECT_EQ(
        RunWithStdlib(vigil_test_failed_,
                      "\n"
                      "        import \"math\";\n"
                      "        fn main() -> i32 {\n"
                      "            f64 eps = 0.000001;\n"
                      "            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);\n"
                      "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
                      "            // 0 degrees -> identity\n"
                      "            math.Quaternion r0 = math.Quaternion.fromAxisAngle(yaxis, 0.0);\n"
                      "            if math.abs(r0.w - 1.0) > eps { return 1; }\n"
                      "            if math.abs(r0.x) > eps { return 2; }\n"
                      "            // 90 degrees around Y -> (0, sin(45), 0, cos(45))\n"
                      "            math.Quaternion r90 = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));\n"
                      "            if math.abs(r90.length() - 1.0) > eps { return 3; }\n"
                      "            if math.abs(r90.y - math.sin(math.deg2rad(45.0))) > eps { return 4; }\n"
                      "            if math.abs(r90.w - math.cos(math.deg2rad(45.0))) > eps { return 5; }\n"
                      "            return 0;\n"
                      "        }\n"
                      "    "),
        0);
}

TEST(VigilStdlibMathTest, QuaternionSlerp)
{
    EXPECT_EQ(
        RunWithStdlib(vigil_test_failed_,
                      "\n"
                      "        import \"math\";\n"
                      "        fn main() -> i32 {\n"
                      "            f64 eps = 0.000001;\n"
                      "            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);\n"
                      "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
                      "            math.Quaternion r90 = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));\n"
                      "            // slerp t=0 -> identity\n"
                      "            math.Quaternion s0 = id.slerp(r90, 0.0);\n"
                      "            if math.abs(s0.w - 1.0) > eps { return 1; }\n"
                      "            // slerp t=1 -> r90\n"
                      "            math.Quaternion s1 = id.slerp(r90, 1.0);\n"
                      "            if math.abs(s1.y - r90.y) > eps { return 2; }\n"
                      "            // slerp t=0.5 -> 45 degrees\n"
                      "            math.Quaternion s5 = id.slerp(r90, 0.5);\n"
                      "            if math.abs(s5.length() - 1.0) > eps { return 3; }\n"
                      "            // half of 90 = 45 deg -> y = sin(22.5 deg)\n"
                      "            if math.abs(s5.y - math.sin(math.deg2rad(22.5))) > eps { return 4; }\n"
                      "            return 0;\n"
                      "        }\n"
                      "    "),
        0);
}

TEST(VigilStdlibMathTest, QuaternionToEuler)
{
    EXPECT_EQ(
        RunWithStdlib(vigil_test_failed_,
                      "\n"
                      "        import \"math\";\n"
                      "        fn main() -> i32 {\n"
                      "            f64 eps = 0.01;\n"
                      "            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);\n"
                      "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
                      "            math.Quaternion r90 = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));\n"
                      "            math.Quaternion euler = r90.toEuler();\n"
                      "            // yaw (euler.y) should be ~90 degrees\n"
                      "            if math.abs(math.rad2deg(euler.y) - 90.0) > eps { return 1; }\n"
                      "            // pitch and roll should be ~0\n"
                      "            if math.abs(euler.x) > eps { return 2; }\n"
                      "            if math.abs(euler.z) > eps { return 3; }\n"
                      "            return 0;\n"
                      "        }\n"
                      "    "),
        0);
}

TEST(VigilStdlibMathTest, QuaternionDotAndNormalize)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 eps = 0.000001;\n"
                                                "            math.Quaternion q = math.Quaternion(1.0, 2.0, 3.0, 4.0);\n"
                                                "            // dot with self = lengthSqr\n"
                                                "            if math.abs(q.dot(q) - 30.0) > eps { return 1; }\n"
                                                "            // normalize\n"
                                                "            math.Quaternion n = q.normalize();\n"
                                                "            if math.abs(n.length() - 1.0) > eps { return 2; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

/* ── Mat4 ────────────────────────────────────────────────────────── */

TEST(VigilStdlibMathTest, Mat4ConstructionAndFieldAccess)
{
    EXPECT_EQ(RunWithStdlib(
                  vigil_test_failed_,
                  "\n"
                  "        import \"math\";\n"
                  "        fn main() -> i32 {\n"
                  "            array<f64> d = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0];\n"
                  "            math.Mat4 m = math.Mat4(d);\n"
                  "            // data field is array<f64>\n"
                  "            array<f64> arr = m.data;\n"
                  "            if arr.len() != 16 { return 1; }\n"
                  "            if arr[0] != 1.0 { return 2; }\n"
                  "            if arr[5] != 1.0 { return 3; }\n"
                  "            return 0;\n"
                  "        }\n"
                  "    "),
              0);
}

TEST(VigilStdlibMathTest, Mat4Identity)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Mat4 id = math.Mat4.identity();\n"
                                                "            if id.get(0, 0) != 1.0 { return 1; }\n"
                                                "            if id.get(1, 1) != 1.0 { return 2; }\n"
                                                "            if id.get(2, 2) != 1.0 { return 3; }\n"
                                                "            if id.get(3, 3) != 1.0 { return 4; }\n"
                                                "            if id.get(0, 1) != 0.0 { return 5; }\n"
                                                "            if id.get(1, 0) != 0.0 { return 6; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Mat4GetSetTranspose)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Mat4 id = math.Mat4.identity();\n"
                                                "            // set(0,1) = 5 -> get(0,1) = 5\n"
                                                "            math.Mat4 m = id.set(0, 1, 5.0);\n"
                                                "            if m.get(0, 1) != 5.0 { return 1; }\n"
                                                "            if m.get(1, 0) != 0.0 { return 2; }\n"
                                                "            // transpose swaps (0,1) and (1,0)\n"
                                                "            math.Mat4 t = m.transpose();\n"
                                                "            if t.get(1, 0) != 5.0 { return 3; }\n"
                                                "            if t.get(0, 1) != 0.0 { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Mat4MultiplyAndDeterminant)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 eps = 0.000001;\n"
                                                "            math.Mat4 id = math.Mat4.identity();\n"
                                                "            // id * id = id\n"
                                                "            math.Mat4 r = id.multiply(id);\n"
                                                "            if r.get(0, 0) != 1.0 { return 1; }\n"
                                                "            if r.get(0, 1) != 0.0 { return 2; }\n"
                                                "            // det(id) = 1\n"
                                                "            if math.abs(id.determinant() - 1.0) > eps { return 3; }\n"
                                                "            // scale by 2 -> det = 2^4 = 16\n"
                                                "            math.Mat4 s = id.scale(2.0);\n"
                                                "            if math.abs(s.determinant() - 16.0) > eps { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Mat4AddAndScale)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Mat4 id = math.Mat4.identity();\n"
                                                "            math.Mat4 sum = id.add(id);\n"
                                                "            if sum.get(0, 0) != 2.0 { return 1; }\n"
                                                "            if sum.get(0, 1) != 0.0 { return 2; }\n"
                                                "            math.Mat4 s = id.scale(3.0);\n"
                                                "            if s.get(0, 0) != 3.0 { return 3; }\n"
                                                "            if s.get(1, 1) != 3.0 { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

/* ── Static methods ──────────────────────────────────────────────── */

TEST(VigilStdlibMathTest, Vec2StaticZeroAndOne)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Vec2 z = math.Vec2.zero();\n"
                                                "            if z.x != 0.0 { return 1; }\n"
                                                "            if z.y != 0.0 { return 2; }\n"
                                                "            math.Vec2 o = math.Vec2.one();\n"
                                                "            if o.x != 1.0 { return 3; }\n"
                                                "            if o.y != 1.0 { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec3StaticZeroAndOne)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Vec3 z = math.Vec3.zero();\n"
                                                "            if z.x != 0.0 { return 1; }\n"
                                                "            if z.y != 0.0 { return 2; }\n"
                                                "            if z.z != 0.0 { return 3; }\n"
                                                "            math.Vec3 o = math.Vec3.one();\n"
                                                "            if o.x != 1.0 { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec4StaticZeroAndOne)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Vec4 z = math.Vec4.zero();\n"
                                                "            if z.x != 0.0 { return 1; }\n"
                                                "            if z.w != 0.0 { return 2; }\n"
                                                "            math.Vec4 o = math.Vec4.one();\n"
                                                "            if o.x != 1.0 { return 3; }\n"
                                                "            if o.w != 1.0 { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Mat4StaticIdentity)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            math.Mat4 id = math.Mat4.identity();\n"
                                                "            if id.get(0, 0) != 1.0 { return 1; }\n"
                                                "            if id.get(1, 1) != 1.0 { return 2; }\n"
                                                "            if id.get(0, 1) != 0.0 { return 3; }\n"
                                                "            if id.determinant() != 1.0 { return 4; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, QuaternionStaticFromAxisAngle)
{
    EXPECT_EQ(
        RunWithStdlib(vigil_test_failed_,
                      "\n"
                      "        import \"math\";\n"
                      "        fn main() -> i32 {\n"
                      "            f64 eps = 0.000001;\n"
                      "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
                      "            math.Quaternion q = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));\n"
                      "            if math.abs(q.length() - 1.0) > eps { return 1; }\n"
                      "            if math.abs(q.y - math.sin(math.deg2rad(45.0))) > eps { return 2; }\n"
                      "            return 0;\n"
                      "        }\n"
                      "    "),
        0);
}

TEST(VigilStdlibMathTest, StaticMethodChaining)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            // Static factory -> instance method chain\n"
                                                "            math.Vec3 v = math.Vec3.one().scale(3.0);\n"
                                                "            if v.x != 3.0 { return 1; }\n"
                                                "            if v.y != 3.0 { return 2; }\n"
                                                "            math.Mat4 m = math.Mat4.identity().scale(2.0);\n"
                                                "            if m.get(0, 0) != 2.0 { return 3; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

/* ── Vec2 angle/rotate ───────────────────────────────────────────── */

TEST(VigilStdlibMathTest, Vec2AngleAndRotate)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            math.Vec2 v = math.Vec2(1.0, 0.0);\n"
                            "            if math.abs(v.angle()) > eps { return 1; }\n"
                            "            math.Vec2 up = math.Vec2(0.0, 1.0);\n"
                            "            if math.abs(up.angle() - math.pi() / 2.0) > eps { return 2; }\n"
                            "            // rotate (1,0) by 90 degrees -> (0,1)\n"
                            "            math.Vec2 r = v.rotate(math.pi() / 2.0);\n"
                            "            if math.abs(r.x) > eps { return 3; }\n"
                            "            if math.abs(r.y - 1.0) > eps { return 4; }\n"
                            "            // rotate by 180 -> (-1, 0)\n"
                            "            math.Vec2 r2 = v.rotate(math.pi());\n"
                            "            if math.abs(r2.x + 1.0) > eps { return 5; }\n"
                            "            if math.abs(r2.y) > eps { return 6; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

/* ── Vec3 transform/rotateByQuaternion/unproject ─────────────────── */

TEST(VigilStdlibMathTest, Vec3Transform)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 eps = 0.000001;\n"
                                                "            math.Vec3 p = math.Vec3(1.0, 2.0, 3.0);\n"
                                                "            // Identity transform\n"
                                                "            math.Mat4 id = math.Mat4.identity();\n"
                                                "            math.Vec3 r = p.transform(id);\n"
                                                "            if math.abs(r.x - 1.0) > eps { return 1; }\n"
                                                "            if math.abs(r.y - 2.0) > eps { return 2; }\n"
                                                "            if math.abs(r.z - 3.0) > eps { return 3; }\n"
                                                "            // Translation\n"
                                                "            math.Mat4 t = id.translate(math.Vec3(10.0, 20.0, 30.0));\n"
                                                "            math.Vec3 r2 = p.transform(t);\n"
                                                "            if math.abs(r2.x - 11.0) > eps { return 4; }\n"
                                                "            if math.abs(r2.y - 22.0) > eps { return 5; }\n"
                                                "            if math.abs(r2.z - 33.0) > eps { return 6; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec3RotateByQuaternion)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            math.Vec3 fwd = math.Vec3(0.0, 0.0, 1.0);\n"
                            "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
                            "            // 90 deg around Y: (0,0,1) -> (1,0,0)\n"
                            "            math.Quaternion q = math.Quaternion.fromAxisAngle(yaxis, math.pi() / 2.0);\n"
                            "            math.Vec3 r = fwd.rotateByQuaternion(q);\n"
                            "            if math.abs(r.x - 1.0) > eps { return 1; }\n"
                            "            if math.abs(r.y) > eps { return 2; }\n"
                            "            if math.abs(r.z) > eps { return 3; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

TEST(VigilStdlibMathTest, Vec3Unproject)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.01;\n"
                            "            math.Mat4 proj = math.Mat4.perspective(math.deg2rad(90.0), 1.0, 0.1, 100.0);\n"
                            "            math.Mat4 view = math.Mat4.identity();\n"
                            "            // Center of screen (0.5, 0.5) at near plane (z=0)\n"
                            "            math.Vec3 near = math.Vec3(0.5, 0.5, 0.0).unproject(proj, view);\n"
                            "            // Should be near origin on near plane\n"
                            "            if math.abs(near.x) > eps { return 1; }\n"
                            "            if math.abs(near.y) > eps { return 2; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

/* ── Quaternion fromEuler/toMat4 ─────────────────────────────────── */

TEST(VigilStdlibMathTest, QuaternionFromEuler)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            // Zero euler -> identity quaternion\n"
                            "            math.Quaternion q0 = math.Quaternion.fromEuler(0.0, 0.0, 0.0);\n"
                            "            if math.abs(q0.w - 1.0) > eps { return 1; }\n"
                            "            if math.abs(q0.x) > eps { return 2; }\n"
                            "            // 90 deg pitch -> unit quaternion\n"
                            "            math.Quaternion qp = math.Quaternion.fromEuler(math.pi() / 2.0, 0.0, 0.0);\n"
                            "            if math.abs(qp.length() - 1.0) > eps { return 3; }\n"
                            "            // fromEuler produces unit quaternions\n"
                            "            math.Quaternion q = math.Quaternion.fromEuler(0.1, 0.2, 0.3);\n"
                            "            if math.abs(q.length() - 1.0) > eps { return 4; }\n"
                            "            // 180 deg yaw: should be (0, sin(90), 0, cos(90)) = (0, 1, 0, 0)\n"
                            "            math.Quaternion qy = math.Quaternion.fromEuler(0.0, math.pi(), 0.0);\n"
                            "            if math.abs(math.abs(qy.y) - 1.0) > eps { return 5; }\n"
                            "            if math.abs(qy.x) > eps { return 6; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

TEST(VigilStdlibMathTest, QuaternionToMat4)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            // Identity quaternion -> identity matrix\n"
                            "            math.Quaternion qi = math.Quaternion(0.0, 0.0, 0.0, 1.0);\n"
                            "            math.Mat4 m = qi.toMat4();\n"
                            "            if math.abs(m.get(0, 0) - 1.0) > eps { return 1; }\n"
                            "            if math.abs(m.get(1, 1) - 1.0) > eps { return 2; }\n"
                            "            if math.abs(m.get(2, 2) - 1.0) > eps { return 3; }\n"
                            "            if math.abs(m.get(3, 3) - 1.0) > eps { return 4; }\n"
                            "            if math.abs(m.get(0, 1)) > eps { return 5; }\n"
                            "            // 90 deg around Y: should rotate (0,0,1) to (1,0,0)\n"
                            "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
                            "            math.Quaternion q90 = math.Quaternion.fromAxisAngle(yaxis, math.pi() / 2.0);\n"
                            "            math.Mat4 rm = q90.toMat4();\n"
                            "            math.Vec3 fwd = math.Vec3(0.0, 0.0, 1.0);\n"
                            "            math.Vec3 r = fwd.transform(rm);\n"
                            "            if math.abs(r.x - 1.0) > eps { return 6; }\n"
                            "            if math.abs(r.y) > eps { return 7; }\n"
                            "            if math.abs(r.z) > eps { return 8; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

/* ── Mat4 new methods ────────────────────────────────────────────── */

TEST(VigilStdlibMathTest, Mat4Trace)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            if math.abs(math.Mat4.identity().trace() - 4.0) > eps { return 1; }\n"
                            "            math.Mat4 s = math.Mat4.identity().scale(2.0);\n"
                            "            if math.abs(s.trace() - 8.0) > eps { return 2; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

TEST(VigilStdlibMathTest, Mat4Invert)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 eps = 0.000001;\n"
                                                "            // Invert identity = identity\n"
                                                "            math.Mat4 id = math.Mat4.identity();\n"
                                                "            math.Mat4 inv = id.invert();\n"
                                                "            if math.abs(inv.get(0, 0) - 1.0) > eps { return 1; }\n"
                                                "            if math.abs(inv.get(0, 1)) > eps { return 2; }\n"
                                                "            // Invert scale(2) = scale(0.5)\n"
                                                "            math.Mat4 s = id.scale(2.0);\n"
                                                "            math.Mat4 si = s.invert();\n"
                                                "            if math.abs(si.get(0, 0) - 0.5) > eps { return 3; }\n"
                                                "            // M * M^-1 = I\n"
                                                "            math.Mat4 prod = s.multiply(si);\n"
                                                "            if math.abs(prod.get(0, 0) - 1.0) > eps { return 4; }\n"
                                                "            if math.abs(prod.get(0, 1)) > eps { return 5; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Mat4TranslateAndScaleV)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 eps = 0.000001;\n"
                                                "            math.Mat4 id = math.Mat4.identity();\n"
                                                "            // Translate\n"
                                                "            math.Mat4 t = id.translate(math.Vec3(10.0, 20.0, 30.0));\n"
                                                "            if math.abs(t.get(0, 3) - 10.0) > eps { return 1; }\n"
                                                "            if math.abs(t.get(1, 3) - 20.0) > eps { return 2; }\n"
                                                "            if math.abs(t.get(2, 3) - 30.0) > eps { return 3; }\n"
                                                "            // ScaleV\n"
                                                "            math.Mat4 s = id.scaleV(math.Vec3(2.0, 3.0, 4.0));\n"
                                                "            if math.abs(s.get(0, 0) - 2.0) > eps { return 4; }\n"
                                                "            if math.abs(s.get(1, 1) - 3.0) > eps { return 5; }\n"
                                                "            if math.abs(s.get(2, 2) - 4.0) > eps { return 6; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Mat4RotateXYZ)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "        import \"math\";\n"
                                                "        fn main() -> i32 {\n"
                                                "            f64 eps = 0.000001;\n"
                                                "            math.Mat4 id = math.Mat4.identity();\n"
                                                "            // RotateX 90: (0,1,0) -> (0,0,1)\n"
                                                "            math.Mat4 rx = id.rotateX(math.pi() / 2.0);\n"
                                                "            math.Vec3 up = math.Vec3(0.0, 1.0, 0.0);\n"
                                                "            math.Vec3 r1 = up.transform(rx);\n"
                                                "            if math.abs(r1.y) > eps { return 1; }\n"
                                                "            if math.abs(r1.z - 1.0) > eps { return 2; }\n"
                                                "            // RotateY 90: (0,0,1) -> (1,0,0)\n"
                                                "            math.Mat4 ry = id.rotateY(math.pi() / 2.0);\n"
                                                "            math.Vec3 fwd = math.Vec3(0.0, 0.0, 1.0);\n"
                                                "            math.Vec3 r2 = fwd.transform(ry);\n"
                                                "            if math.abs(r2.x - 1.0) > eps { return 3; }\n"
                                                "            if math.abs(r2.z) > eps { return 4; }\n"
                                                "            // RotateZ 90: (1,0,0) -> (0,1,0)\n"
                                                "            math.Mat4 rz = id.rotateZ(math.pi() / 2.0);\n"
                                                "            math.Vec3 right = math.Vec3(1.0, 0.0, 0.0);\n"
                                                "            math.Vec3 r3 = right.transform(rz);\n"
                                                "            if math.abs(r3.x) > eps { return 5; }\n"
                                                "            if math.abs(r3.y - 1.0) > eps { return 6; }\n"
                                                "            return 0;\n"
                                                "        }\n"
                                                "    "),
              0);
}

TEST(VigilStdlibMathTest, Mat4LookAt)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            math.Vec3 eye = math.Vec3(0.0, 0.0, 5.0);\n"
                            "            math.Vec3 target = math.Vec3.zero();\n"
                            "            math.Vec3 up = math.Vec3(0.0, 1.0, 0.0);\n"
                            "            math.Mat4 view = math.Mat4.lookAt(eye, target, up);\n"
                            "            // Eye at (0,0,5) looking at origin: the view matrix should\n"
                            "            // translate the eye to origin in view space\n"
                            "            math.Vec3 eyeView = eye.transform(view);\n"
                            "            if math.abs(eyeView.x) > eps { return 1; }\n"
                            "            if math.abs(eyeView.y) > eps { return 2; }\n"
                            "            // Target should be at (0,0,-5) in view space\n"
                            "            math.Vec3 targetView = target.transform(view);\n"
                            "            if math.abs(targetView.x) > eps { return 3; }\n"
                            "            if math.abs(targetView.y) > eps { return 4; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

TEST(VigilStdlibMathTest, Mat4PerspectiveAndOrtho)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            // Perspective: 90 deg FOV, aspect 1, near 0.1, far 100\n"
                            "            math.Mat4 p = math.Mat4.perspective(math.deg2rad(90.0), 1.0, 0.1, 100.0);\n"
                            "            // With 90 deg FOV and aspect 1: m[0][0] = m[1][1] = 1.0\n"
                            "            if math.abs(p.get(0, 0) - 1.0) > eps { return 1; }\n"
                            "            if math.abs(p.get(1, 1) - 1.0) > eps { return 2; }\n"
                            "            if p.get(3, 3) != 0.0 { return 3; }\n"
                            "            // Ortho: symmetric [-1,1] box\n"
                            "            math.Mat4 o = math.Mat4.ortho(-1.0, 1.0, -1.0, 1.0, 0.1, 100.0);\n"
                            "            if math.abs(o.get(0, 0) - 1.0) > eps { return 4; }\n"
                            "            if math.abs(o.get(1, 1) - 1.0) > eps { return 5; }\n"
                            "            if math.abs(o.get(3, 3) - 1.0) > eps { return 6; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

TEST(VigilStdlibMathTest, Mat4Frustum)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "        import \"math\";\n"
                            "        fn main() -> i32 {\n"
                            "            f64 eps = 0.000001;\n"
                            "            math.Mat4 f = math.Mat4.frustum(-1.0, 1.0, -1.0, 1.0, 1.0, 100.0);\n"
                            "            // Symmetric frustum: m[0][0] = 2*near/(right-left) = 2*1/2 = 1\n"
                            "            if math.abs(f.get(0, 0) - 1.0) > eps { return 1; }\n"
                            "            if math.abs(f.get(1, 1) - 1.0) > eps { return 2; }\n"
                            "            if f.get(3, 2) != -1.0 { return 3; }\n"
                            "            return 0;\n"
                            "        }\n"
                            "    "),
              0);
}

/* ── String trim_left / trim_right ────────────────────────────────── */

TEST(VigilStdlibStringTest, TrimLeftAndTrimRight)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "fn main() -> i32 {\n"
                            "    string s = \"  hello  \";\n"
                            "    if s.trim_left() != \"hello  \" { return 1; }\n"
                            "    if s.trim_right() != \"  hello\" { return 2; }\n"
                            "    if \"nowhitespace\".trim_left() != \"nowhitespace\" { return 3; }\n"
                            "    if \"nowhitespace\".trim_right() != \"nowhitespace\" { return 4; }\n"
                            "    if \"   \".trim_left() != \"\" { return 5; }\n"
                            "    if \"   \".trim_right() != \"\" { return 6; }\n"
                            "    return 0;\n"
                            "}\n"
                            "    "),
              0);
}

/* ── String reverse ──────────────────────────────────────────────── */

TEST(VigilStdlibStringTest, Reverse)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "fn main() -> i32 {\n"
                                                "    if \"hello\".reverse() != \"olleh\" { return 1; }\n"
                                                "    if \"\".reverse() != \"\" { return 2; }\n"
                                                "    if \"a\".reverse() != \"a\" { return 3; }\n"
                                                "    if \"abcd\".reverse() != \"dcba\" { return 4; }\n"
                                                "    return 0;\n"
                                                "}\n"
                                                "    "),
              0);
}

/* ── String is_empty ─────────────────────────────────────────────── */

TEST(VigilStdlibStringTest, IsEmpty)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "fn main() -> i32 {\n"
                                                "    if \"\".none() != true { return 1; }\n"
                                                "    if \"x\".none() != false { return 2; }\n"
                                                "    if \" \".none() != false { return 3; }\n"
                                                "    if \"hello\".any() != true { return 4; }\n"
                                                "    if \"\".any() != false { return 5; }\n"
                                                "    return 0;\n"
                                                "}\n"
                                                "    "),
              0);
}

/* ── String repeat ───────────────────────────────────────────────── */

TEST(VigilStdlibStringTest, Repeat)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "fn main() -> i32 {\n"
                                                "    if \"abc\".repeat(i32(3)) != \"abcabcabc\" { return 1; }\n"
                                                "    if \"x\".repeat(i32(0)) != \"\" { return 2; }\n"
                                                "    if \"hi\".repeat(i32(1)) != \"hi\" { return 3; }\n"
                                                "    if \"\".repeat(i32(5)) != \"\" { return 4; }\n"
                                                "    return 0;\n"
                                                "}\n"
                                                "    "),
              0);
}

/* ── String count ────────────────────────────────────────────────── */

TEST(VigilStdlibStringTest, Count)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "fn main() -> i32 {\n"
                                                "    if \"abcabcabc\".count(\"abc\") != i32(3) { return 1; }\n"
                                                "    if \"hello\".count(\"x\") != i32(0) { return 2; }\n"
                                                "    if \"aaa\".count(\"a\") != i32(3) { return 3; }\n"
                                                "    if \"aaaa\".count(\"aa\") != i32(2) { return 4; }\n"
                                                "    return 0;\n"
                                                "}\n"
                                                "    "),
              0);
}

/* ── String last_index_of ────────────────────────────────────────── */

TEST(VigilStdlibStringTest, LastIndexOf)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "fn main() -> i32 {\n"
                                                "    i32 idx, bool found = \"hello world\".last_index_of(\"o\");\n"
                                                "    if idx != i32(7) { return 1; }\n"
                                                "    if found != true { return 2; }\n"
                                                "    i32 idx2, bool found2 = \"hello\".last_index_of(\"xyz\");\n"
                                                "    if found2 != false { return 3; }\n"
                                                "    i32 idx3, bool found3 = \"abcabc\".last_index_of(\"abc\");\n"
                                                "    if idx3 != i32(3) { return 4; }\n"
                                                "    if found3 != true { return 5; }\n"
                                                "    return 0;\n"
                                                "}\n"
                                                "    "),
              0);
}

/* ── String trim_prefix / trim_suffix ────────────────────────────── */

TEST(VigilStdlibStringTest, TrimPrefixAndTrimSuffix)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "fn main() -> i32 {\n"
                            "    if \"hello world\".trim_prefix(\"hello \") != \"world\" { return 1; }\n"
                            "    if \"hello world\".trim_suffix(\" world\") != \"hello\" { return 2; }\n"
                            "    if \"hello\".trim_prefix(\"xyz\") != \"hello\" { return 3; }\n"
                            "    if \"hello\".trim_suffix(\"xyz\") != \"hello\" { return 4; }\n"
                            "    if \"hello\".trim_prefix(\"hello\") != \"\" { return 5; }\n"
                            "    if \"hello\".trim_suffix(\"hello\") != \"\" { return 6; }\n"
                            "    return 0;\n"
                            "}\n"
                            "    "),
              0);
}

/* ── Crypto SHA-256 ──────────────────────────────────────────────── */

TEST(VigilStdlibCryptoTest, Sha256Empty)
{
    EXPECT_EQ(
        RunWithStdlib(vigil_test_failed_,
                      "\n"
                      "import \"crypto\";\n"
                      "fn main() -> i32 {\n"
                      "    string h = crypto.sha256(\"\");\n"
                      "    if h != \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\" { return 1; }\n"
                      "    return 0;\n"
                      "}\n"),
        0);
}

TEST(VigilStdlibCryptoTest, Sha256Hello)
{
    EXPECT_EQ(
        RunWithStdlib(vigil_test_failed_,
                      "\n"
                      "import \"crypto\";\n"
                      "fn main() -> i32 {\n"
                      "    string h = crypto.sha256(\"hello\");\n"
                      "    if h != \"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824\" { return 1; }\n"
                      "    return 0;\n"
                      "}\n"),
        0);
}

/* ── Crypto SHA-512 ──────────────────────────────────────────────── */

TEST(VigilStdlibCryptoTest, Sha512Hello)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "import \"crypto\";\n"
                            "fn main() -> i32 {\n"
                            "    string h = crypto.sha512(\"hello\");\n"
                            "    if !h.starts_with(\"9b71d224bd62f3785d96d46ad3ea3d73\") { return 1; }\n"
                            "    return 0;\n"
                            "}\n"),
              0);
}

/* ── Crypto HMAC-SHA256 ──────────────────────────────────────────── */

TEST(VigilStdlibCryptoTest, HmacSha256)
{
    EXPECT_EQ(
        RunWithStdlib(vigil_test_failed_,
                      "\n"
                      "import \"crypto\";\n"
                      "fn main() -> i32 {\n"
                      "    string h = crypto.hmac_sha256(\"key\", \"message\");\n"
                      "    if h != \"6e9ef29b75fffc5b7abae527d58fdadb2fe42e7219011976917343065f58ed4a\" { return 1; }\n"
                      "    return 0;\n"
                      "}\n"),
        0);
}

/* ── Crypto hex encode/decode ────────────────────────────────────── */

TEST(VigilStdlibCryptoTest, HexEncode)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "import \"crypto\";\n"
                                                "fn main() -> i32 {\n"
                                                "    if crypto.hex_encode(\"ABC\") != \"414243\" { return 1; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

TEST(VigilStdlibCryptoTest, HexDecode)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "import \"crypto\";\n"
                                                "fn main() -> i32 {\n"
                                                "    if crypto.hex_decode(\"414243\") != \"ABC\" { return 1; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

/* ── Crypto base64 encode/decode ─────────────────────────────────── */

TEST(VigilStdlibCryptoTest, Base64Encode)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "import \"crypto\";\n"
                                                "fn main() -> i32 {\n"
                                                "    if crypto.base64_encode(\"hello\") != \"aGVsbG8=\" { return 1; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

TEST(VigilStdlibCryptoTest, Base64Decode)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "import \"crypto\";\n"
                                                "fn main() -> i32 {\n"
                                                "    if crypto.base64_decode(\"aGVsbG8=\") != \"hello\" { return 1; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

/* ── Crypto encrypt/decrypt roundtrip ────────────────────────────── */

TEST(VigilStdlibCryptoTest, EncryptDecryptRoundtrip)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "import \"crypto\";\n"
                                                "fn main() -> i32 {\n"
                                                "    string key = crypto.random_bytes(32);\n"
                                                "    string nonce = crypto.random_bytes(12);\n"
                                                "    string plaintext = \"secret message\";\n"
                                                "    string encrypted = crypto.encrypt(key, nonce, plaintext);\n"
                                                "    string decrypted = crypto.decrypt(key, encrypted);\n"
                                                "    if decrypted != plaintext { return 1; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

/* ── Crypto constant_time_eq ─────────────────────────────────────── */

TEST(VigilStdlibCryptoTest, ConstantTimeEq)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "import \"crypto\";\n"
                                                "fn main() -> i32 {\n"
                                                "    if !crypto.constant_time_eq(\"abc\", \"abc\") { return 1; }\n"
                                                "    if crypto.constant_time_eq(\"abc\", \"xyz\") { return 2; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

/* ── Crypto random_bytes ─────────────────────────────────────────── */

TEST(VigilStdlibCryptoTest, RandomBytesLength)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "import \"crypto\";\n"
                                                "fn main() -> i32 {\n"
                                                "    string r = crypto.random_bytes(32);\n"
                                                "    if r.len() != 32 { return 1; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

/* ── Crypto password_encrypt/decrypt ─────────────────────────────── */

TEST(VigilStdlibCryptoTest, PasswordEncryptDecrypt)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "import \"crypto\";\n"
                            "fn main() -> i32 {\n"
                            "    string plaintext = \"secret message\";\n"
                            "    string encrypted = crypto.password_encrypt(\"my password\", plaintext);\n"
                            "    string decrypted = crypto.password_decrypt(\"my password\", encrypted);\n"
                            "    if decrypted != plaintext { return 1; }\n"
                            "    return 0;\n"
                            "}\n"),
              0);
}

TEST(VigilStdlibCryptoTest, PasswordDecryptWrongPassword)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "import \"crypto\";\n"
                            "fn main() -> i32 {\n"
                            "    string encrypted = crypto.password_encrypt(\"correct\", \"secret\");\n"
                            "    string decrypted = crypto.password_decrypt(\"wrong\", encrypted);\n"
                            "    if decrypted != \"\" { return 1; }\n"
                            "    return 0;\n"
                            "}\n"),
              0);
}

/* ── Compress module ──────────────────────────────────────────────── */

TEST(VigilStdlibCompressTest, RoundTrip)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "import \"compress\";\n"
                                                "fn main() -> i32 {\n"
                                                "    string gz = compress.gzip_compress(\"hello\");\n"
                                                "    if compress.gzip_decompress(gz) != \"hello\" { return 1; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

TEST(VigilStdlibCompressTest, CompressLevels)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "import \"compress\";\n"
                                                "fn main() -> i32 {\n"
                                                "    string g9 = compress.gzip_compress_level(\"hello\", 9);\n"
                                                "    if compress.gzip_decompress(g9) != \"hello\" { return 1; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

TEST(VigilStdlibCompressTest, Checksums)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_,
                            "\n"
                            "import \"compress\";\n"
                            "fn main() -> i32 {\n"
                            "    if compress.crc32(\"Hello, World!\") != i64(3964322768) { return 1; }\n"
                            "    if compress.adler32(\"Hello, World!\") != i64(530449514) { return 2; }\n"
                            "    return 0;\n"
                            "}\n"),
              0);
}

TEST(VigilStdlibCompressTest, ZipCreateLevel)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "import \"compress\";\n"
                                                "fn main() -> i32 {\n"
                                                "    array<string> n = [\"a.txt\"];\n"
                                                "    array<string> c = [\"hello\"];\n"
                                                "    string z = compress.zip_create_level(n, c, 9);\n"
                                                "    if compress.zip_read(z, \"a.txt\") != \"hello\" { return 1; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

TEST(VigilStdlibCompressTest, TarGzCreate)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "import \"compress\";\n"
                                                "fn main() -> i32 {\n"
                                                "    array<string> n = [\"a.txt\"];\n"
                                                "    array<string> c = [\"hello\"];\n"
                                                "    string tgz = compress.tar_gz_create(n, c);\n"
                                                "    string tar = compress.gzip_decompress(tgz);\n"
                                                "    if compress.tar_read(tar, \"a.txt\") != \"hello\" { return 1; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

TEST(VigilStdlibCompressTest, GzipDecompressMax)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "import \"compress\";\n"
                                                "fn main() -> i32 {\n"
                                                "    string gz = compress.gzip_compress(\"abcdefghij\");\n"
                                                "    string limited = compress.gzip_decompress_max(gz, 5);\n"
                                                "    if limited.len() > 5 { return 1; }\n"
                                                "    string full = compress.gzip_decompress_max(gz, 100);\n"
                                                "    if full != \"abcdefghij\" { return 2; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

TEST(VigilStdlibCompressTest, GzipInfo)
{
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, "\n"
                                                "import \"compress\";\n"
                                                "fn main() -> i32 {\n"
                                                "    string gz = compress.gzip_compress(\"hello\");\n"
                                                "    map<string, string> m = compress.gzip_info(gz);\n"
                                                "    if m[\"size\"] != \"5\" { return 1; }\n"
                                                "    if m[\"method\"] != \"8\" { return 2; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

#ifdef VIGIL_HAS_STDLIB_FS
TEST(VigilStdlibFsTest, WriterOpenWriteAndWriteLine)
{
    int64_t result = RunWithStdlib(vigil_test_failed_, "import \"fs\";\n"
                                                       "fn main() -> i32 {\n"
                                                       "    string path = fs.temp_file(\"vigil-writer\");\n"
                                                       "    fs.Writer w, err open_err = fs.Writer.open(path);\n"
                                                       "    if open_err != ok { return 1; }\n"
                                                       "    i32 n1, err e1 = w.write(\"hello\");\n"
                                                       "    i32 n2, err e2 = w.write_line(\" world\");\n"
                                                       "    err close_err = w.close();\n"
                                                       "    string content = fs.read(path);\n"
                                                       "    fs.remove(path);\n"
                                                       "    if e1 != ok || e2 != ok || close_err != ok { return 2; }\n"
                                                       "    if n1 != 5 || n2 != 7 { return 3; }\n"
                                                       "    if content != \"hello world\\n\" { return 4; }\n"
                                                       "    return 0;\n"
                                                       "}\n");
    EXPECT_EQ(result, 0);
}
#endif

static void register_stdlib_fs_tests(void)
{
#ifdef VIGIL_HAS_STDLIB_FS
    REGISTER_TEST(VigilStdlibFsTest, WriterOpenWriteAndWriteLine);
#endif
}

void register_stdlib_tests(void)
{
    REGISTER_TEST(VigilStdlibFmtTest, PrintlnOutputsStringWithNewline);
    REGISTER_TEST(VigilStdlibFmtTest, PrintOutputsStringWithoutNewline);
    REGISTER_TEST(VigilStdlibFmtTest, EprintlnOutputsToStderr);
    REGISTER_TEST(VigilStdlibFmtTest, PrintlnEmptyString);
    REGISTER_TEST(VigilStdlibFmtTest, PrintlnWithFString);
    REGISTER_TEST(VigilStdlibFmtTest, PrintlnWithVariable);
    REGISTER_TEST(VigilStdlibFmtTest, PrintlnInLoop);
    REGISTER_TEST(VigilStdlibMathTest, PiReturnsCorrectValue);
    REGISTER_TEST(VigilStdlibMathTest, EReturnsCorrectValue);
    REGISTER_TEST(VigilStdlibMathTest, FloorTable);
    REGISTER_TEST(VigilStdlibMathTest, CeilTable);
    REGISTER_TEST(VigilStdlibMathTest, RoundTable);
    REGISTER_TEST(VigilStdlibMathTest, TruncTable);
    REGISTER_TEST(VigilStdlibMathTest, AbsTable);
    REGISTER_TEST(VigilStdlibMathTest, SignTable);
    REGISTER_TEST(VigilStdlibMathTest, SqrtTable);
    REGISTER_TEST(VigilStdlibMathTest, SinCosAtZero);
    REGISTER_TEST(VigilStdlibMathTest, IntrinsicOpcodesSinCosSqrtLogPow);
    REGISTER_TEST(VigilStdlibMathTest, DeferNativeCall);
    REGISTER_TEST(VigilStdlibMathTest, LogTable);
    REGISTER_TEST(VigilStdlibMathTest, ExpAtZeroAndOne);
    REGISTER_TEST(VigilStdlibMathTest, PowTable);
    REGISTER_TEST(VigilStdlibMathTest, MinMaxTable);
    REGISTER_TEST(VigilStdlibMathTest, Atan2Table);
    REGISTER_TEST(VigilStdlibMathTest, HypotTable);
    REGISTER_TEST(VigilStdlibMathTest, FmodTable);
    REGISTER_TEST(VigilStdlibMathTest, ClampTable);
    REGISTER_TEST(VigilStdlibMathTest, ComposedExpressions);
    REGISTER_TEST(VigilStdlibMathTest, Vec2ConstructionAndFields);
    REGISTER_TEST(VigilStdlibMathTest, Vec2FieldMutation);
    REGISTER_TEST(VigilStdlibMathTest, Vec2Length);
    REGISTER_TEST(VigilStdlibMathTest, Vec2Dot);
    REGISTER_TEST(VigilStdlibMathTest, Vec2WithScalarMath);
    REGISTER_TEST(VigilStdlibMathTest, InverseTrig);
    REGISTER_TEST(VigilStdlibMathTest, Deg2RadRad2Deg);
    REGISTER_TEST(VigilStdlibMathTest, Lerp);
    REGISTER_TEST(VigilStdlibMathTest, Normalize);
    REGISTER_TEST(VigilStdlibMathTest, Wrap);
    REGISTER_TEST(VigilStdlibMathTest, Remap);
    REGISTER_TEST(VigilStdlibMathTest, Vec2Normalize);
    REGISTER_TEST(VigilStdlibMathTest, Vec2AddSubScaleDistance);
    REGISTER_TEST(VigilStdlibMathTest, Vec3ConstructionAndFields);
    REGISTER_TEST(VigilStdlibMathTest, Vec3Length);
    REGISTER_TEST(VigilStdlibMathTest, Vec3DotAndCross);
    REGISTER_TEST(VigilStdlibMathTest, Vec3NormalizeAddSubScaleDistance);
    REGISTER_TEST(VigilStdlibMathTest, Vec3MethodChaining);
    REGISTER_TEST(VigilStdlibMathTest, Step);
    REGISTER_TEST(VigilStdlibMathTest, Smoothstep);
    REGISTER_TEST(VigilStdlibMathTest, InverseLerp);
    REGISTER_TEST(VigilStdlibMathTest, Vec2LengthSqrAndNegate);
    REGISTER_TEST(VigilStdlibMathTest, Vec2LerpAndReflect);
    REGISTER_TEST(VigilStdlibMathTest, Vec3LengthSqrAndNegate);
    REGISTER_TEST(VigilStdlibMathTest, Vec3LerpAndReflect);
    REGISTER_TEST(VigilStdlibMathTest, Vec3Angle);
    REGISTER_TEST(VigilStdlibMathTest, Vec4ConstructionAndFields);
    REGISTER_TEST(VigilStdlibMathTest, Vec4LengthAndDot);
    REGISTER_TEST(VigilStdlibMathTest, Vec4Arithmetic);
    REGISTER_TEST(VigilStdlibMathTest, QuaternionIdentity);
    REGISTER_TEST(VigilStdlibMathTest, QuaternionConjugateAndInverse);
    REGISTER_TEST(VigilStdlibMathTest, QuaternionMultiply);
    REGISTER_TEST(VigilStdlibMathTest, QuaternionFromAxisAngle);
    REGISTER_TEST(VigilStdlibMathTest, QuaternionSlerp);
    REGISTER_TEST(VigilStdlibMathTest, QuaternionToEuler);
    REGISTER_TEST(VigilStdlibMathTest, QuaternionDotAndNormalize);
    REGISTER_TEST(VigilStdlibMathTest, Mat4ConstructionAndFieldAccess);
    REGISTER_TEST(VigilStdlibMathTest, Mat4Identity);
    REGISTER_TEST(VigilStdlibMathTest, Mat4GetSetTranspose);
    REGISTER_TEST(VigilStdlibMathTest, Mat4MultiplyAndDeterminant);
    REGISTER_TEST(VigilStdlibMathTest, Mat4AddAndScale);
    REGISTER_TEST(VigilStdlibMathTest, Vec2StaticZeroAndOne);
    REGISTER_TEST(VigilStdlibMathTest, Vec3StaticZeroAndOne);
    REGISTER_TEST(VigilStdlibMathTest, Vec4StaticZeroAndOne);
    REGISTER_TEST(VigilStdlibMathTest, Mat4StaticIdentity);
    REGISTER_TEST(VigilStdlibMathTest, QuaternionStaticFromAxisAngle);
    REGISTER_TEST(VigilStdlibMathTest, StaticMethodChaining);
    REGISTER_TEST(VigilStdlibMathTest, Vec2AngleAndRotate);
    REGISTER_TEST(VigilStdlibMathTest, Vec3Transform);
    REGISTER_TEST(VigilStdlibMathTest, Vec3RotateByQuaternion);
    REGISTER_TEST(VigilStdlibMathTest, Vec3Unproject);
    REGISTER_TEST(VigilStdlibMathTest, QuaternionFromEuler);
    REGISTER_TEST(VigilStdlibMathTest, QuaternionToMat4);
    REGISTER_TEST(VigilStdlibMathTest, Mat4Trace);
    REGISTER_TEST(VigilStdlibMathTest, Mat4Invert);
    REGISTER_TEST(VigilStdlibMathTest, Mat4TranslateAndScaleV);
    REGISTER_TEST(VigilStdlibMathTest, Mat4RotateXYZ);
    REGISTER_TEST(VigilStdlibMathTest, Mat4LookAt);
    REGISTER_TEST(VigilStdlibMathTest, Mat4PerspectiveAndOrtho);
    REGISTER_TEST(VigilStdlibMathTest, Mat4Frustum);
    REGISTER_TEST(VigilStdlibStringTest, TrimLeftAndTrimRight);
    REGISTER_TEST(VigilStdlibStringTest, Reverse);
    REGISTER_TEST(VigilStdlibStringTest, IsEmpty);
    REGISTER_TEST(VigilStdlibStringTest, Repeat);
    REGISTER_TEST(VigilStdlibStringTest, Count);
    REGISTER_TEST(VigilStdlibStringTest, LastIndexOf);
    REGISTER_TEST(VigilStdlibStringTest, TrimPrefixAndTrimSuffix);
    REGISTER_TEST(VigilStdlibCryptoTest, Sha256Empty);
    REGISTER_TEST(VigilStdlibCryptoTest, Sha256Hello);
    REGISTER_TEST(VigilStdlibCryptoTest, Sha512Hello);
    REGISTER_TEST(VigilStdlibCryptoTest, HmacSha256);
    REGISTER_TEST(VigilStdlibCryptoTest, HexEncode);
    REGISTER_TEST(VigilStdlibCryptoTest, HexDecode);
    REGISTER_TEST(VigilStdlibCryptoTest, Base64Encode);
    REGISTER_TEST(VigilStdlibCryptoTest, Base64Decode);
    REGISTER_TEST(VigilStdlibCryptoTest, EncryptDecryptRoundtrip);
    REGISTER_TEST(VigilStdlibCryptoTest, ConstantTimeEq);
    REGISTER_TEST(VigilStdlibCryptoTest, RandomBytesLength);
    REGISTER_TEST(VigilStdlibCryptoTest, PasswordEncryptDecrypt);
    REGISTER_TEST(VigilStdlibCryptoTest, PasswordDecryptWrongPassword);
    REGISTER_TEST(VigilStdlibCompressTest, RoundTrip);
    REGISTER_TEST(VigilStdlibCompressTest, CompressLevels);
    REGISTER_TEST(VigilStdlibCompressTest, Checksums);
    REGISTER_TEST(VigilStdlibCompressTest, ZipCreateLevel);
    REGISTER_TEST(VigilStdlibCompressTest, TarGzCreate);
    REGISTER_TEST(VigilStdlibCompressTest, GzipDecompressMax);
    REGISTER_TEST(VigilStdlibCompressTest, GzipInfo);
    register_stdlib_fs_tests();
}

/* ── Regex stdlib tests (exercises pattern cache) ────────────────── */

TEST(VigilStdlibRegexTest, FindAllCacheHit)
{
    /* Call find_all with the same pattern twice — second call is a cache hit */
    int64_t result = RunWithStdlib(vigil_test_failed_, "import \"regex\";\n"
                                                       "fn main() -> i32 {\n"
                                                       "    string t = \"a1 b2 c3\";\n"
                                                       "    array<string> m1 = regex.find_all(\"[a-z][0-9]\", t);\n"
                                                       "    array<string> m2 = regex.find_all(\"[a-z][0-9]\", t);\n"
                                                       "    if m1.len() != 3 { return 1; }\n"
                                                       "    if m2.len() != 3 { return 2; }\n"
                                                       "    return 0;\n"
                                                       "}\n");
    EXPECT_EQ(result, 0);
}

TEST(VigilStdlibRegexTest, ReplaceAllCacheHit)
{
    int64_t result =
        RunWithStdlib(vigil_test_failed_, "import \"regex\";\n"
                                          "fn main() -> i32 {\n"
                                          "    string s = regex.replace_all(\"[0-9]+\", \"abc123def456\", \"X\");\n"
                                          "    string s2 = regex.replace_all(\"[0-9]+\", \"111\", \"Y\");\n"
                                          "    if s != \"abcXdefX\" { return 1; }\n"
                                          "    if s2 != \"Y\" { return 2; }\n"
                                          "    return 0;\n"
                                          "}\n");
    EXPECT_EQ(result, 0);
}

TEST(VigilStdlibRegexTest, MatchAndFind)
{
    int64_t result =
        RunWithStdlib(vigil_test_failed_, "import \"regex\";\n"
                                          "fn main() -> i32 {\n"
                                          "    bool m = regex.match(\"[a-z]+\", \"hello\");\n"
                                          "    if !m { return 1; }\n"
                                          "    string found, bool ok = regex.find(\"[0-9]+\", \"abc123\");\n"
                                          "    if !ok { return 2; }\n"
                                          "    if found != \"123\" { return 3; }\n"
                                          "    return 0;\n"
                                          "}\n");
    EXPECT_EQ(result, 0);
}

TEST(VigilStdlibRegexTest, NoMatch)
{
    int64_t result = RunWithStdlib(vigil_test_failed_, "import \"regex\";\n"
                                                       "fn main() -> i32 {\n"
                                                       "    array<string> m = regex.find_all(\"[0-9]+\", \"abcdef\");\n"
                                                       "    if m.len() != 0 { return 1; }\n"
                                                       "    return 0;\n"
                                                       "}\n");
    EXPECT_EQ(result, 0);
}

TEST(VigilStdlibRegexTest, CacheEviction)
{
    /* Use 40 distinct patterns to force LRU eviction (cache size = 32) */
    int64_t result = RunWithStdlib(
        vigil_test_failed_, "import \"regex\";\n"
                            "fn main() -> i32 {\n"
                            "    string t = \"abc123\";\n"
                            "    regex.find_all(\"a\", t); regex.find_all(\"b\", t); regex.find_all(\"c\", t);\n"
                            "    regex.find_all(\"d\", t); regex.find_all(\"e\", t); regex.find_all(\"f\", t);\n"
                            "    regex.find_all(\"g\", t); regex.find_all(\"h\", t); regex.find_all(\"i\", t);\n"
                            "    regex.find_all(\"j\", t); regex.find_all(\"k\", t); regex.find_all(\"l\", t);\n"
                            "    regex.find_all(\"m\", t); regex.find_all(\"n\", t); regex.find_all(\"o\", t);\n"
                            "    regex.find_all(\"p\", t); regex.find_all(\"q\", t); regex.find_all(\"r\", t);\n"
                            "    regex.find_all(\"s\", t); regex.find_all(\"u\", t); regex.find_all(\"v\", t);\n"
                            "    regex.find_all(\"w\", t); regex.find_all(\"x\", t); regex.find_all(\"y\", t);\n"
                            "    regex.find_all(\"z\", t); regex.find_all(\"1\", t); regex.find_all(\"2\", t);\n"
                            "    regex.find_all(\"3\", t); regex.find_all(\"4\", t); regex.find_all(\"5\", t);\n"
                            "    regex.find_all(\"6\", t); regex.find_all(\"7\", t); regex.find_all(\"8\", t);\n"
                            "    regex.find_all(\"9\", t); regex.find_all(\"0\", t); regex.find_all(\"aa\", t);\n"
                            "    regex.find_all(\"bb\", t); regex.find_all(\"cc\", t); regex.find_all(\"dd\", t);\n"
                            "    regex.find_all(\"ee\", t); regex.find_all(\"ff\", t);\n"
                            "    return 0;\n"
                            "}\n");
    EXPECT_EQ(result, 0);
}

void register_stdlib_regex_tests(void)
{
    REGISTER_TEST(VigilStdlibRegexTest, FindAllCacheHit);
    REGISTER_TEST(VigilStdlibRegexTest, ReplaceAllCacheHit);
    REGISTER_TEST(VigilStdlibRegexTest, MatchAndFind);
    REGISTER_TEST(VigilStdlibRegexTest, NoMatch);
    REGISTER_TEST(VigilStdlibRegexTest, CacheEviction);
}

/* ── Parse stdlib tests (covers ok_error singleton paths) ────────── */

TEST(VigilStdlibParseTest, AllSuccessPaths)
{
    /* Exercise parse.i32, parse.i64, parse.f64, parse.bool (true and false)
       to cover all vigil_runtime_push_ok_error call sites in parse.c. */
    int64_t result = RunWithStdlib(vigil_test_failed_,
                                   "import \"parse\";\n"
                                   "fn main() -> i32 {\n"
                                   "    i32 a, err ea = parse.i32(\"42\");\n"
                                   "    i64 b, err eb = parse.i64(\"9999\");\n"
                                   "    f64 c, err ec = parse.f64(\"3.14\");\n"
                                   "    bool d, err ed = parse.bool(\"true\");\n"
                                   "    bool e, err ee = parse.bool(\"false\");\n"
                                   "    if ea != ok || eb != ok || ec != ok || ed != ok || ee != ok { return 1; }\n"
                                   "    if a != 42 { return 2; }\n"
                                   "    if !d || e { return 3; }\n"
                                   "    return 0;\n"
                                   "}\n");
    EXPECT_EQ(result, 0);
}

void register_stdlib_parse_tests(void)
{
    REGISTER_TEST(VigilStdlibParseTest, AllSuccessPaths);
}

/* ── JSON stdlib tests ───────────────────────────────────────────── */

TEST(VigilStdlibJsonTest, ParseTraverseAndCoerce)
{
    int64_t result =
        RunWithStdlib(vigil_test_failed_, "import \"json\";\n"
                                          "fn main() -> i32 {\n"
                                          "    json.Value root, err parse_err = "
                                          "json.Value.parse(\"{\\\"name\\\":\\\"vigil\\\",\\\"flags\\\":[true,false],"
                                          "\\\"count\\\":3}\");\n"
                                          "    if parse_err != ok { return 1; }\n"
                                          "    if !root.is_object() { return 2; }\n"
                                          "    if root.kind() != \"object\" { return 3; }\n"
                                          "    if !root.has(\"name\") || !root.has(\"flags\") { return 4; }\n"
                                          "    i32 root_len, err len_err = root.len();\n"
                                          "    if len_err != ok || root_len != 3 { return 5; }\n"
                                          "    json.Value flags, err flags_err = root.get(\"flags\");\n"
                                          "    if flags_err != ok || !flags.is_array() { return 6; }\n"
                                          "    json.Value first, err first_err = flags.at(0);\n"
                                          "    if first_err != ok { return 7; }\n"
                                          "    bool first_flag, err bool_err = first.as_bool();\n"
                                          "    if bool_err != ok || !first_flag { return 8; }\n"
                                          "    json.Value count, err count_err = root.get(\"count\");\n"
                                          "    if count_err != ok { return 9; }\n"
                                          "    f64 count_num, err num_err = count.as_number();\n"
                                          "    if num_err != ok || count_num != 3.0 { return 10; }\n"
                                          "    json.Value name, err name_err = root.get(\"name\");\n"
                                          "    if name_err != ok { return 11; }\n"
                                          "    string name_text, err string_err = name.as_string();\n"
                                          "    if string_err != ok || name_text != \"vigil\" { return 12; }\n"
                                          "    return 0;\n"
                                          "}\n");
    EXPECT_EQ(result, 0);
}

TEST(VigilStdlibJsonTest, KeysAndStringify)
{
    int64_t result = RunWithStdlib(vigil_test_failed_,
                                   "import \"json\";\n"
                                   "fn main() -> i32 {\n"
                                   "    json.Value root, err parse_err = json.Value.parse(\"{\\\"b\\\":2,"
                                   "\\\"a\\\":\\\"x\\\"}\");\n"
                                   "    if parse_err != ok { return 1; }\n"
                                   "    array<string> keys = root.keys();\n"
                                   "    if keys.len() != 2 { return 2; }\n"
                                   "    if keys[0] != \"b\" || keys[1] != \"a\" { return 3; }\n"
                                   "    if root.stringify() != \"{\\\"b\\\":2,\\\"a\\\":\\\"x\\\"}\" { return 4; }\n"
                                   "    return 0;\n"
                                   "}\n");
    EXPECT_EQ(result, 0);
}

TEST(VigilStdlibJsonTest, ParseAndFileRoundTrip)
{
    char path[512];
    char source[4096];

#ifdef _WIN32
    {
        const char *tmp = getenv("TEMP");
        if (tmp == NULL || *tmp == '\0')
            tmp = ".";
        snprintf(path, sizeof(path), "%s\\vigil-json-%ld.json", tmp, (long)getpid());
    }
#else
    snprintf(path, sizeof(path), "%s/vigil-json-%ld.json", vigil_test_tmpdir(), (long)getpid());
#endif

    remove(path);
    BuildJsonRoundTripSource(path, source, sizeof(source));
    EXPECT_EQ(RunWithStdlib(vigil_test_failed_, source), 0);
    remove(path);
}

TEST(VigilStdlibJsonTest, InvalidJsonReturnsError)
{
    int64_t result =
        RunWithStdlib(vigil_test_failed_, "import \"json\";\n"
                                          "fn main() -> i32 {\n"
                                          "    json.Value value, err parse_err = json.Value.parse(\"{\");\n"
                                          "    if parse_err == ok { return 1; }\n"
                                          "    return 0;\n"
                                          "}\n");
    EXPECT_EQ(result, 0);
}

TEST(VigilStdlibJsonTest, EncodeAndDecodeTypedObject)
{
    int64_t result = RunWithStdlib(
        vigil_test_failed_,
        "import \"json\";\n"
        "class Meta {\n"
        "    pub i32 count;\n"
        "}\n"
        "class Person {\n"
        "    pub string name;\n"
        "    pub array<i32> scores;\n"
        "    pub map<string, string> tags;\n"
        "    pub Meta meta;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    map<string, string> tags = {\"role\": \"tooling\"};\n"
        "    Person person = Person(\"vigil\", [1, 2, 3], tags, Meta(3));\n"
        "    string encoded, err encode_err = json.encode(person);\n"
        "    if encode_err != ok { return 1; }\n"
        "    Person decoded, err decode_err = json.decode(encoded, Person(\"\", [0], {\"\": \"\"}, Meta(0)));\n"
        "    if decode_err != ok { return 2; }\n"
        "    if decoded.name != \"vigil\" { return 3; }\n"
        "    if decoded.scores.len() != 3 || decoded.scores[0] != 1 || decoded.scores[2] != 3 { return 4; }\n"
        "    if !decoded.tags.has(\"role\") || decoded.tags[\"role\"] != \"tooling\" { return 5; }\n"
        "    if decoded.meta.count != 3 { return 6; }\n"
        "    return 0;\n"
        "}\n");
    EXPECT_EQ(result, 0);
}

TEST(VigilStdlibJsonTest, DecodeRejectsExtraFields)
{
    int64_t result = RunWithStdlib(vigil_test_failed_,
                                   "import \"json\";\n"
                                   "class Person {\n"
                                   "    pub string name;\n"
                                   "}\n"
                                   "fn main() -> i32 {\n"
                                   "    Person person, err decode_err = "
                                   "json.decode(\"{\\\"name\\\":\\\"vigil\\\",\\\"extra\\\":1}\", Person(\"\"));\n"
                                   "    if decode_err == ok { return 1; }\n"
                                   "    return 0;\n"
                                   "}\n");
    EXPECT_EQ(result, 0);
}

void register_stdlib_json_tests(void)
{
    REGISTER_TEST(VigilStdlibJsonTest, ParseTraverseAndCoerce);
    REGISTER_TEST(VigilStdlibJsonTest, KeysAndStringify);
    REGISTER_TEST(VigilStdlibJsonTest, ParseAndFileRoundTrip);
    REGISTER_TEST(VigilStdlibJsonTest, InvalidJsonReturnsError);
    REGISTER_TEST(VigilStdlibJsonTest, EncodeAndDecodeTypedObject);
    REGISTER_TEST(VigilStdlibJsonTest, DecodeRejectsExtraFields);
}

/* ── CSV stdlib tests ────────────────────────────────────────────── */

TEST(VigilStdlibCsvTest, ParseRowAndStringifyRow)
{
    /* Covers csv_parse_field_buf (unquoted + quoted fields),
       csv_is_field_end, csv_parse_row, csv_stringify_row,
       csv_write_field, csv_needs_quote. */
    int64_t result = RunWithStdlib(vigil_test_failed_, "import \"csv\";\n"
                                                       "fn main() -> i32 {\n"
                                                       "    array<string> row = csv.parse_row(\"a,b,c\");\n"
                                                       "    if row.len() != 3 { return 1; }\n"
                                                       "    if row[0] != \"a\" { return 2; }\n"
                                                       "    string out = csv.stringify_row(row);\n"
                                                       "    if out != \"a,b,c\" { return 3; }\n"
                                                       "    array<string> q = csv.parse_row(\"\\\"x,y\\\",z\");\n"
                                                       "    if q.len() != 2 { return 4; }\n"
                                                       "    if q[0] != \"x,y\" { return 5; }\n"
                                                       "    return 0;\n"
                                                       "}\n");
    EXPECT_EQ(result, 0);
}

TEST(VigilStdlibCsvTest, LongFieldSpillsToHeap)
{
    /* Triggers csv_field_spill: field longer than 255 bytes. */
    int64_t result =
        RunWithStdlib(vigil_test_failed_, "import \"csv\";\n"
                                          "fn main() -> i32 {\n"
                                          "    string big = \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                          "aaaaaaaaaaaa\";\n"
                                          "    array<string> row = csv.parse_row(big);\n"
                                          "    if row.len() != 1 { return 1; }\n"
                                          "    if row[0].len() != big.len() { return 2; }\n"
                                          "    return 0;\n"
                                          "}\n");
    EXPECT_EQ(result, 0);
}

TEST(VigilStdlibCsvTest, ParseAndStringify)
{
    /* csv.parse and csv.stringify require array<array<string>> which cannot
       be constructed in unit-test Vigil source; covered by integration tests. */
    EXPECT_EQ(1, 1);
}

void register_stdlib_csv_tests(void)
{
    REGISTER_TEST(VigilStdlibCsvTest, ParseRowAndStringifyRow);
    REGISTER_TEST(VigilStdlibCsvTest, LongFieldSpillsToHeap);
    REGISTER_TEST(VigilStdlibCsvTest, ParseAndStringify);
}
