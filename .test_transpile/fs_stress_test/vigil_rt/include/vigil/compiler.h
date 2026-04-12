#ifndef VIGIL_COMPILER_H
#define VIGIL_COMPILER_H

#include "vigil/diagnostic.h"
#include "vigil/export.h"
#include "vigil/native_module.h"
#include "vigil/source.h"
#include "vigil/status.h"
#include "vigil/value.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Current implementation compiles a runnable VIGIL subset across multiple
     * source files: top-level `import`, `const`, `class`, and `fn` declarations
     * with a required `fn main() -> i32 { ... }` entrypoint. Function bodies
     * support typed local declarations, integer/bool expressions, short-circuit
     * `&&`/`||`, assignment, `if`, `while`, `break`, `continue`, explicit
     * `return` statements, class construction, field reads/writes, and class
     * methods with implicit `self`.
     */
    VIGIL_API vigil_status_t vigil_compile_source(const vigil_source_registry_t *registry, vigil_source_id_t source_id,
                                                  vigil_object_t **out_function, vigil_diagnostic_list_t *diagnostics,
                                                  vigil_error_t *error);

    /*
     * Compile in REPL mode.  Top-level statements and expressions are allowed
     * without a fn main() wrapper.  The compiler synthesizes an entrypoint
     * from any non-declaration tokens found at the top level.  If the source
     * contains only declarations and no statements, *out_has_statements is set
     * to 0 (the returned function still validates global initializers).
     */
    VIGIL_API vigil_status_t vigil_compile_source_repl(const vigil_source_registry_t *registry,
                                                       vigil_source_id_t source_id,
                                                       const vigil_native_registry_t *natives,
                                                       vigil_object_t **out_function, int *out_has_statements,
                                                       vigil_diagnostic_list_t *diagnostics, vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
