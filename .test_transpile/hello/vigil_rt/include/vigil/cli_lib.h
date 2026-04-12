#ifndef VIGIL_CLI_LIB_H
#define VIGIL_CLI_LIB_H

#include <stddef.h>

#include "vigil/allocator.h"
#include "vigil/export.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Minimal CLI argument parser.
     *
     * Supports:
     *   - Subcommands:       vigil run file.vigil
     *   - Long flags:        --verbose, --output=file.txt, --output file.txt
     *   - Short flags:       -v, -o file.txt
     *   - Boolean flags:     --verbose (no value)
     *   - Positional args:   vigil run file.vigil
     *   - Auto help:         --help / -h prints usage
     *
     * Usage:
     *   vigil_cli_t cli;
     *   vigil_cli_init(&cli, "vigil", "The VIGIL Scripting Language");
     *
     *   vigil_cli_command_t *run = vigil_cli_add_command(&cli, "run", "Run a script");
     *   const char *file = NULL;
     *   int verbose = 0;
     *   vigil_cli_add_positional(run, "file", "Script file to run", &file);
     *   vigil_cli_add_bool_flag(run, "verbose", 'v', "Enable verbose output", &verbose);
     *
     *   vigil_cli_parse(&cli, argc, argv, &error);
     *   // file and verbose are now populated
     *
     *   vigil_cli_free(&cli);
     */

    typedef struct vigil_cli_flag
    {
        const char *name; /* long name (without --) */
        char short_name;  /* single char (0 if none) */
        const char *help;
        int is_bool;
        /* Output pointers — exactly one is non-NULL. */
        const char **out_string;
        int *out_bool;
    } vigil_cli_flag_t;

    typedef struct vigil_cli_positional
    {
        const char *name;
        const char *help;
        const char **out_value;
    } vigil_cli_positional_t;

    typedef struct vigil_cli_command
    {
        const char *name;
        const char *help;
        vigil_cli_flag_t *flags;
        size_t flag_count;
        size_t flag_capacity;
        vigil_cli_positional_t *positionals;
        size_t positional_count;
        size_t positional_capacity;
        int matched;
    } vigil_cli_command_t;

    typedef struct vigil_cli
    {
        const char *program_name;
        const char *description;
        vigil_allocator_t allocator;
        /* Global flags (apply to all commands). */
        vigil_cli_flag_t *flags;
        size_t flag_count;
        size_t flag_capacity;
        /* Subcommands. */
        vigil_cli_command_t *commands;
        size_t command_count;
        size_t command_capacity;
        /* The matched command after parsing (NULL if none). */
        vigil_cli_command_t *matched_command;
        /* Global positionals (when no subcommands are used). */
        vigil_cli_positional_t *positionals;
        size_t positional_count;
        size_t positional_capacity;
        /* Set to 1 if help was printed during parsing. */
        int help_shown;
    } vigil_cli_t;

    /* ── Lifecycle ───────────────────────────────────────────────────── */

    VIGIL_API void vigil_cli_init(vigil_cli_t *cli, const char *program_name, const char *description);

    VIGIL_API void vigil_cli_init_with_allocator(vigil_cli_t *cli, const char *program_name, const char *description,
                                                 const vigil_allocator_t *allocator);

    VIGIL_API void vigil_cli_free(vigil_cli_t *cli);

    /* ── Commands ────────────────────────────────────────────────────── */

    VIGIL_API vigil_cli_command_t *vigil_cli_add_command(vigil_cli_t *cli, const char *name, const char *help);

    /** Returns the matched command after parsing, or NULL. */
    VIGIL_API const vigil_cli_command_t *vigil_cli_matched_command(const vigil_cli_t *cli);

    /* ── Flags (on a command, or globally on the cli) ────────────────── */

    VIGIL_API void vigil_cli_add_string_flag(vigil_cli_command_t *command, const char *name, char short_name,
                                             const char *help, const char **out_value);

    VIGIL_API void vigil_cli_add_bool_flag(vigil_cli_command_t *command, const char *name, char short_name,
                                           const char *help, int *out_value);

    /* Global flags (not tied to a command). */
    VIGIL_API void vigil_cli_add_global_string_flag(vigil_cli_t *cli, const char *name, char short_name,
                                                    const char *help, const char **out_value);

    VIGIL_API void vigil_cli_add_global_bool_flag(vigil_cli_t *cli, const char *name, char short_name, const char *help,
                                                  int *out_value);

    /* ── Positional args ─────────────────────────────────────────────── */

    VIGIL_API void vigil_cli_add_positional(vigil_cli_command_t *command, const char *name, const char *help,
                                            const char **out_value);

    /* Global positional (when not using subcommands). */
    VIGIL_API void vigil_cli_add_global_positional(vigil_cli_t *cli, const char *name, const char *help,
                                                   const char **out_value);

    /* ── Parsing ─────────────────────────────────────────────────────── */

    /**
     * Parse argc/argv.  Populates all bound output pointers.
     * Returns VIGIL_STATUS_OK on success.
     * Returns VIGIL_STATUS_INVALID_ARGUMENT on parse error (message in error).
     * Prints help and returns VIGIL_STATUS_OK if --help/-h is found.
     */
    VIGIL_API vigil_status_t vigil_cli_parse(vigil_cli_t *cli, int argc, char **argv, vigil_error_t *error);

    /* ── Help ────────────────────────────────────────────────────────── */

    VIGIL_API void vigil_cli_print_help(const vigil_cli_t *cli);
    VIGIL_API void vigil_cli_print_command_help(const vigil_cli_t *cli, const vigil_cli_command_t *command);

#ifdef __cplusplus
}
#endif

#endif
