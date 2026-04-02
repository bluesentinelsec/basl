# ── Standard library modules ─────────────────────────────────────────
# Always-on modules are platform-independent.  Optional modules require
# POSIX/Win32 APIs and are gated by VIGIL_STDLIB_* options.
#
# When adding a new stdlib module:
#   1. Add the source to VIGIL_STDLIB_ALWAYS_SOURCES (if portable) or
#      add a new optional block below (if platform-dependent).
#   2. For optional modules, add the VIGIL_HAS_STDLIB_<NAME> definition.

set(VIGIL_STDLIB_ALWAYS_SOURCES
    src/stdlib/args.c
    src/stdlib/atomic.c
    src/stdlib/compress.c
    src/stdlib/crypto.c
    src/stdlib/csv.c
    src/stdlib/fmt.c
    src/stdlib/json.c
    src/stdlib/log.c
    src/stdlib/math.c
    src/stdlib/parse.c
    src/stdlib/random.c
    src/stdlib/regex.c
    src/stdlib/regex_engine.c
    src/stdlib/test.c
    src/stdlib/unsafe.c
    src/stdlib/url.c
    src/stdlib/yaml.c
)

# ── Optional stdlib modules ──────────────────────────────────────────

set(VIGIL_STDLIB_OPTIONAL_SOURCES)
set(VIGIL_STDLIB_COMPILE_DEFINITIONS)

macro(_vigil_optional_stdlib _option _source _define)
    if(${_option})
        list(APPEND VIGIL_STDLIB_OPTIONAL_SOURCES ${_source})
        list(APPEND VIGIL_STDLIB_COMPILE_DEFINITIONS ${_define})
    endif()
endmacro()

_vigil_optional_stdlib(VIGIL_STDLIB_FFI      src/stdlib/ffi.c      VIGIL_HAS_STDLIB_FFI)
_vigil_optional_stdlib(VIGIL_STDLIB_FS       src/stdlib/fs.c       VIGIL_HAS_STDLIB_FS)
_vigil_optional_stdlib(VIGIL_STDLIB_HTTP     src/stdlib/http.c     VIGIL_HAS_STDLIB_HTTP)
_vigil_optional_stdlib(VIGIL_STDLIB_NET      src/stdlib/net.c      VIGIL_HAS_STDLIB_NET)
_vigil_optional_stdlib(VIGIL_STDLIB_READLINE src/stdlib/readline.c VIGIL_HAS_STDLIB_READLINE)
_vigil_optional_stdlib(VIGIL_STDLIB_THREAD   src/stdlib/thread.c   VIGIL_HAS_STDLIB_THREAD)
_vigil_optional_stdlib(VIGIL_STDLIB_TIME     src/stdlib/time.c     VIGIL_HAS_STDLIB_TIME)
