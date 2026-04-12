#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/cli_lib.h"

/* ── Allocator helpers ───────────────────────────────────────────── */

static void *cli_realloc(const vigil_allocator_t *a, void *p, size_t size)
{
    return a->reallocate(a->user_data, p, size);
}

static void cli_dealloc(const vigil_allocator_t *a, void *p)
{
    a->deallocate(a->user_data, p);
}

typedef struct cli_flag_list
{
    vigil_cli_flag_t **items;
    size_t *count;
    size_t *capacity;
} cli_flag_list_t;

typedef struct cli_flag_spec
{
    const char *name;
    char short_name;
    const char *help;
    int is_bool;
    const char **out_string;
    int *out_bool;
} cli_flag_spec_t;

typedef struct cli_positional_list
{
    vigil_cli_positional_t **items;
    size_t *count;
    size_t *capacity;
} cli_positional_list_t;

typedef struct cli_positional_spec
{
    const char *name;
    const char *help;
    const char **out_value;
} cli_positional_spec_t;

typedef struct cli_parse_context
{
    vigil_cli_t *cli;
    vigil_cli_command_t *command;
    vigil_cli_flag_t *command_flags;
    size_t command_flag_count;
    vigil_cli_positional_t *positionals;
    size_t positional_count;
    size_t positional_index;
} cli_parse_context_t;

/* ── Lifecycle ───────────────────────────────────────────────────── */

void vigil_cli_init_with_allocator(vigil_cli_t *cli, const char *program_name, const char *description,
                                   const vigil_allocator_t *allocator)
{
    if (cli == NULL)
        return;
    memset(cli, 0, sizeof(*cli));
    cli->program_name = program_name;
    cli->description = description;
    cli->allocator =
        (allocator != NULL && vigil_allocator_is_valid(allocator)) ? *allocator : vigil_default_allocator();
}

void vigil_cli_init(vigil_cli_t *cli, const char *program_name, const char *description)
{
    vigil_cli_init_with_allocator(cli, program_name, description, NULL);
}

void vigil_cli_free(vigil_cli_t *cli)
{
    if (cli == NULL)
        return;
    const vigil_allocator_t *a = &cli->allocator;
    for (size_t i = 0; i < cli->command_count; i++)
    {
        cli_dealloc(a, cli->commands[i].flags);
        cli_dealloc(a, cli->commands[i].positionals);
    }
    cli_dealloc(a, cli->commands);
    cli_dealloc(a, cli->flags);
    cli_dealloc(a, cli->positionals);
    memset(cli, 0, sizeof(*cli));
}

/* ── Commands ────────────────────────────────────────────────────── */

vigil_cli_command_t *vigil_cli_add_command(vigil_cli_t *cli, const char *name, const char *help)
{
    if (cli == NULL)
        return NULL;
    const vigil_allocator_t *a = &cli->allocator;

    if (cli->command_count >= cli->command_capacity)
    {
        size_t new_cap = cli->command_capacity < 4 ? 4 : cli->command_capacity * 2;
        vigil_cli_command_t *new_cmds =
            (vigil_cli_command_t *)cli_realloc(a, cli->commands, new_cap * sizeof(vigil_cli_command_t));
        if (new_cmds == NULL)
            return NULL;
        cli->commands = new_cmds;
        cli->command_capacity = new_cap;
    }

    vigil_cli_command_t *cmd = &cli->commands[cli->command_count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->name = name;
    cmd->help = help;
    return cmd;
}

const vigil_cli_command_t *vigil_cli_matched_command(const vigil_cli_t *cli)
{
    return cli != NULL ? cli->matched_command : NULL;
}

/* ── Flag/positional helpers ─────────────────────────────────────── */

static void add_flag(const vigil_allocator_t *a, cli_flag_list_t *list, const cli_flag_spec_t *spec)
{
    if (*list->count >= *list->capacity)
    {
        size_t new_cap = *list->capacity < 8 ? 8 : *list->capacity * 2;
        vigil_cli_flag_t *nf = (vigil_cli_flag_t *)cli_realloc(a, *list->items, new_cap * sizeof(vigil_cli_flag_t));
        if (nf == NULL)
            return;
        *list->items = nf;
        *list->capacity = new_cap;
    }
    vigil_cli_flag_t *f = &(*list->items)[(*list->count)++];
    f->name = spec->name;
    f->short_name = spec->short_name;
    f->help = spec->help;
    f->is_bool = spec->is_bool;
    f->out_string = spec->out_string;
    f->out_bool = spec->out_bool;
}

static void add_positional(const vigil_allocator_t *a, cli_positional_list_t *list, const cli_positional_spec_t *spec)
{
    if (*list->count >= *list->capacity)
    {
        size_t new_cap = *list->capacity < 4 ? 4 : *list->capacity * 2;
        vigil_cli_positional_t *np =
            (vigil_cli_positional_t *)cli_realloc(a, *list->items, new_cap * sizeof(vigil_cli_positional_t));
        if (np == NULL)
            return;
        *list->items = np;
        *list->capacity = new_cap;
    }
    vigil_cli_positional_t *p = &(*list->items)[(*list->count)++];
    p->name = spec->name;
    p->help = spec->help;
    p->out_value = spec->out_value;
}

/* ── Public flag/positional API ──────────────────────────────────── */

void vigil_cli_add_string_flag(vigil_cli_command_t *cmd, const char *name, char short_name, const char *help,
                               const char **out_value)
{
    cli_flag_list_t list;
    cli_flag_spec_t spec;

    if (cmd == NULL)
        return;
    /* Commands don't own an allocator — use default. Caller must keep
       the cli alive.  We store into the command's own arrays. */
    vigil_allocator_t a = vigil_default_allocator();
    list.items = &cmd->flags;
    list.count = &cmd->flag_count;
    list.capacity = &cmd->flag_capacity;
    spec.name = name;
    spec.short_name = short_name;
    spec.help = help;
    spec.is_bool = 0;
    spec.out_string = out_value;
    spec.out_bool = NULL;
    add_flag(&a, &list, &spec);
}

void vigil_cli_add_bool_flag(vigil_cli_command_t *cmd, const char *name, char short_name, const char *help,
                             int *out_value)
{
    cli_flag_list_t list;
    cli_flag_spec_t spec;

    if (cmd == NULL)
        return;
    vigil_allocator_t a = vigil_default_allocator();
    list.items = &cmd->flags;
    list.count = &cmd->flag_count;
    list.capacity = &cmd->flag_capacity;
    spec.name = name;
    spec.short_name = short_name;
    spec.help = help;
    spec.is_bool = 1;
    spec.out_string = NULL;
    spec.out_bool = out_value;
    add_flag(&a, &list, &spec);
}

void vigil_cli_add_global_string_flag(vigil_cli_t *cli, const char *name, char short_name, const char *help,
                                      const char **out_value)
{
    cli_flag_list_t list;
    cli_flag_spec_t spec;

    if (cli == NULL)
        return;
    list.items = &cli->flags;
    list.count = &cli->flag_count;
    list.capacity = &cli->flag_capacity;
    spec.name = name;
    spec.short_name = short_name;
    spec.help = help;
    spec.is_bool = 0;
    spec.out_string = out_value;
    spec.out_bool = NULL;
    add_flag(&cli->allocator, &list, &spec);
}

void vigil_cli_add_global_bool_flag(vigil_cli_t *cli, const char *name, char short_name, const char *help,
                                    int *out_value)
{
    cli_flag_list_t list;
    cli_flag_spec_t spec;

    if (cli == NULL)
        return;
    list.items = &cli->flags;
    list.count = &cli->flag_count;
    list.capacity = &cli->flag_capacity;
    spec.name = name;
    spec.short_name = short_name;
    spec.help = help;
    spec.is_bool = 1;
    spec.out_string = NULL;
    spec.out_bool = out_value;
    add_flag(&cli->allocator, &list, &spec);
}

void vigil_cli_add_positional(vigil_cli_command_t *cmd, const char *name, const char *help, const char **out_value)
{
    cli_positional_list_t list;
    cli_positional_spec_t spec;

    if (cmd == NULL)
        return;
    vigil_allocator_t a = vigil_default_allocator();
    list.items = &cmd->positionals;
    list.count = &cmd->positional_count;
    list.capacity = &cmd->positional_capacity;
    spec.name = name;
    spec.help = help;
    spec.out_value = out_value;
    add_positional(&a, &list, &spec);
}

void vigil_cli_add_global_positional(vigil_cli_t *cli, const char *name, const char *help, const char **out_value)
{
    cli_positional_list_t list;
    cli_positional_spec_t spec;

    if (cli == NULL)
        return;
    list.items = &cli->positionals;
    list.count = &cli->positional_count;
    list.capacity = &cli->positional_capacity;
    spec.name = name;
    spec.help = help;
    spec.out_value = out_value;
    add_positional(&cli->allocator, &list, &spec);
}

/* ── Help printing ───────────────────────────────────────────────── */

static void print_flags(const vigil_cli_flag_t *flags, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        const vigil_cli_flag_t *f = &flags[i];
        if (f->short_name != 0)
        {
            fprintf(stderr, "  -%c, --%-16s %s\n", f->short_name, f->name, f->help ? f->help : "");
        }
        else
        {
            fprintf(stderr, "      --%-16s %s\n", f->name, f->help ? f->help : "");
        }
    }
}

void vigil_cli_print_help(const vigil_cli_t *cli)
{
    if (cli == NULL)
        return;
    fprintf(stderr, "%s", cli->program_name ? cli->program_name : "program");
    if (cli->description != NULL)
    {
        fprintf(stderr, " - %s", cli->description);
    }
    fprintf(stderr, "\n\nUsage:\n");

    if (cli->command_count > 0)
    {
        fprintf(stderr, "  %s <command> [options]\n\n", cli->program_name ? cli->program_name : "program");
        fprintf(stderr, "Commands:\n");
        for (size_t i = 0; i < cli->command_count; i++)
        {
            fprintf(stderr, "  %-20s %s\n", cli->commands[i].name, cli->commands[i].help ? cli->commands[i].help : "");
        }
    }
    else
    {
        fprintf(stderr, "  %s [options]", cli->program_name ? cli->program_name : "program");
        for (size_t i = 0; i < cli->positional_count; i++)
        {
            fprintf(stderr, " <%s>", cli->positionals[i].name);
        }
        fprintf(stderr, "\n");
    }

    if (cli->flag_count > 0)
    {
        fprintf(stderr, "\nGlobal options:\n");
        print_flags(cli->flags, cli->flag_count);
    }
}

void vigil_cli_print_command_help(const vigil_cli_t *cli, const vigil_cli_command_t *cmd)
{
    if (cli == NULL || cmd == NULL)
        return;
    fprintf(stderr, "Usage: %s %s [options]", cli->program_name ? cli->program_name : "program", cmd->name);
    for (size_t i = 0; i < cmd->positional_count; i++)
    {
        fprintf(stderr, " <%s>", cmd->positionals[i].name);
    }
    fprintf(stderr, "\n");
    if (cmd->help != NULL)
    {
        fprintf(stderr, "\n%s\n", cmd->help);
    }
    if (cmd->flag_count > 0)
    {
        fprintf(stderr, "\nOptions:\n");
        print_flags(cmd->flags, cmd->flag_count);
    }
    if (cli->flag_count > 0)
    {
        fprintf(stderr, "\nGlobal options:\n");
        print_flags(cli->flags, cli->flag_count);
    }
}

/* ── Parsing ─────────────────────────────────────────────────────── */

static vigil_cli_flag_t *find_long_flag(vigil_cli_flag_t *flags, size_t count, const char *name, size_t name_len)
{
    for (size_t i = 0; i < count; i++)
    {
        if (strlen(flags[i].name) == name_len && memcmp(flags[i].name, name, name_len) == 0)
        {
            return &flags[i];
        }
    }
    return NULL;
}

static vigil_cli_flag_t *find_short_flag(vigil_cli_flag_t *flags, size_t count, char c)
{
    for (size_t i = 0; i < count; i++)
    {
        if (flags[i].short_name == c)
            return &flags[i];
    }
    return NULL;
}

static void set_flag_value(vigil_cli_flag_t *f, const char *value)
{
    if (f->is_bool && f->out_bool != NULL)
    {
        *f->out_bool = 1;
    }
    else if (!f->is_bool && f->out_string != NULL)
    {
        *f->out_string = value;
    }
}

static vigil_status_t handle_help_flag(cli_parse_context_t *context)
{
    if (context->command != NULL)
    {
        vigil_cli_print_command_help(context->cli, context->command);
    }
    else
    {
        vigil_cli_print_help(context->cli);
    }

    context->cli->matched_command = NULL;
    context->cli->help_shown = 1;
    return VIGIL_STATUS_OK;
}

static vigil_cli_command_t *match_command(vigil_cli_t *cli, int argc, char **argv, int *arg_start, vigil_error_t *error)
{
    vigil_cli_command_t *cmd = NULL;

    if (cli->command_count == 0 || argc <= 1)
    {
        return NULL;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
    {
        vigil_cli_print_help(cli);
        cli->help_shown = 1;
        return NULL;
    }

    for (size_t i = 0; i < cli->command_count; i++)
    {
        if (strcmp(argv[1], cli->commands[i].name) == 0)
        {
            cmd = &cli->commands[i];
            cmd->matched = 1;
            cli->matched_command = cmd;
            *arg_start = 2;
            return cmd;
        }
    }

    if (cli->positional_count == 0 && argv[1][0] != '-')
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "cli: unknown command");
    }

    return NULL;
}

static void init_parse_context(cli_parse_context_t *context, vigil_cli_t *cli, vigil_cli_command_t *cmd)
{
    context->cli = cli;
    context->command = cmd;
    context->command_flags = cmd != NULL ? cmd->flags : NULL;
    context->command_flag_count = cmd != NULL ? cmd->flag_count : 0U;
    context->positionals = cmd != NULL ? cmd->positionals : cli->positionals;
    context->positional_count = cmd != NULL ? cmd->positional_count : cli->positional_count;
    context->positional_index = 0U;
}

static vigil_cli_flag_t *find_any_long_flag(const cli_parse_context_t *context, const char *name, size_t name_len)
{
    vigil_cli_flag_t *flag;

    flag = find_long_flag(context->command_flags, context->command_flag_count, name, name_len);
    if (flag != NULL)
    {
        return flag;
    }

    return find_long_flag(context->cli->flags, context->cli->flag_count, name, name_len);
}

static vigil_cli_flag_t *find_any_short_flag(const cli_parse_context_t *context, char short_name)
{
    vigil_cli_flag_t *flag;

    flag = find_short_flag(context->command_flags, context->command_flag_count, short_name);
    if (flag != NULL)
    {
        return flag;
    }

    return find_short_flag(context->cli->flags, context->cli->flag_count, short_name);
}

static vigil_status_t assign_remaining_positionals(cli_parse_context_t *context, int argc, char **argv, int start)
{
    for (int index = start; index < argc && context->positional_index < context->positional_count; index++)
    {
        *context->positionals[context->positional_index++].out_value = argv[index];
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t parse_long_flag(cli_parse_context_t *context, const char *arg, int argc, char **argv, int *index,
                                      vigil_error_t *error)
{
    const char *name = arg + 2;
    const char *eq = strchr(name, '=');
    size_t name_len = eq ? (size_t)(eq - name) : strlen(name);
    vigil_cli_flag_t *flag = find_any_long_flag(context, name, name_len);

    if (flag == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "cli: unknown flag");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (flag->is_bool)
    {
        set_flag_value(flag, NULL);
        return VIGIL_STATUS_OK;
    }

    if (eq != NULL)
    {
        set_flag_value(flag, eq + 1);
        return VIGIL_STATUS_OK;
    }

    if (*index + 1 < argc)
    {
        set_flag_value(flag, argv[++(*index)]);
        return VIGIL_STATUS_OK;
    }

    vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "cli: flag requires a value");
    return VIGIL_STATUS_INVALID_ARGUMENT;
}

static vigil_status_t parse_short_flag(cli_parse_context_t *context, const char *arg, int argc, char **argv, int *index,
                                       vigil_error_t *error)
{
    vigil_cli_flag_t *flag = find_any_short_flag(context, arg[1]);

    if (flag == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "cli: unknown flag");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (flag->is_bool)
    {
        set_flag_value(flag, NULL);
        return VIGIL_STATUS_OK;
    }

    if (arg[2] != '\0')
    {
        set_flag_value(flag, arg + 2);
        return VIGIL_STATUS_OK;
    }

    if (*index + 1 < argc)
    {
        set_flag_value(flag, argv[++(*index)]);
        return VIGIL_STATUS_OK;
    }

    vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "cli: flag requires a value");
    return VIGIL_STATUS_INVALID_ARGUMENT;
}

static vigil_status_t parse_positional_argument(cli_parse_context_t *context, const char *arg, vigil_error_t *error)
{
    if (context->positional_index >= context->positional_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "cli: unexpected argument");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *context->positionals[context->positional_index++].out_value = arg;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_cli_parse(vigil_cli_t *cli, int argc, char **argv, vigil_error_t *error)
{
    cli_parse_context_t context;
    if (cli == NULL || argv == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "cli: invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    cli->matched_command = NULL;
    int arg_start = 1; /* skip program name */

    vigil_cli_command_t *cmd = match_command(cli, argc, argv, &arg_start, error);
    if (cmd == NULL && error != NULL && error->type == VIGIL_STATUS_INVALID_ARGUMENT)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (cli->help_shown)
    {
        return VIGIL_STATUS_OK;
    }

    init_parse_context(&context, cli, cmd);

    for (int i = arg_start; i < argc; i++)
    {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)
        {
            return handle_help_flag(&context);
        }

        if (strcmp(arg, "--") == 0)
        {
            assign_remaining_positionals(&context, argc, argv, i + 1);
            break;
        }

        if (arg[0] == '-' && arg[1] == '-')
        {
            vigil_status_t status = parse_long_flag(&context, arg, argc, argv, &i, error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
            continue;
        }

        if (arg[0] == '-' && arg[1] != '\0')
        {
            vigil_status_t status = parse_short_flag(&context, arg, argc, argv, &i, error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
            continue;
        }

        if (parse_positional_argument(&context, arg, error) != VIGIL_STATUS_OK)
        {
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
    }

    return VIGIL_STATUS_OK;
}
