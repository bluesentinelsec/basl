/* Portable line editor with history support.
 *
 * Uses platform raw-mode primitives for key reading.
 * Falls back to fgets when stdin is not a terminal.
 */
#include "internal/vigil_internal.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define line_strdup _strdup
#else
#define line_strdup strdup
#endif

/* ── Key constants ───────────────────────────────────────────────── */

enum
{
    KEY_NONE = 0,
    KEY_CTRL_A = 1,
    KEY_CTRL_B = 2,
    KEY_CTRL_C = 3,
    KEY_CTRL_D = 4,
    KEY_CTRL_E = 5,
    KEY_CTRL_F = 6,
    KEY_CTRL_H = 8,
    KEY_TAB = 9,
    KEY_ENTER = 13,
    KEY_CTRL_K = 11,
    KEY_CTRL_L = 12,
    KEY_CTRL_N = 14,
    KEY_CTRL_P = 16,
    KEY_CTRL_T = 20,
    KEY_CTRL_U = 21,
    KEY_CTRL_W = 23,
    KEY_ESC = 27,
    KEY_BACKSPACE = 127,

    /* Virtual keys for escape sequences. */
    KEY_ARROW_UP = 256,
    KEY_ARROW_DOWN = 257,
    KEY_ARROW_RIGHT = 258,
    KEY_ARROW_LEFT = 259,
    KEY_HOME = 260,
    KEY_END = 261,
    KEY_DELETE = 262,
    KEY_ALT_B = 263,
    KEY_ALT_F = 264,
    KEY_ALT_D = 265
};

typedef enum
{
    LINE_EDIT_UNHANDLED = 0,
    LINE_EDIT_REFRESH,
    LINE_EDIT_SUBMIT,
    LINE_EDIT_EOF
} line_edit_result_t;

typedef struct
{
    vigil_line_history_t *history;
    size_t index;
    char *saved_line;
} line_history_nav_t;

/* GCOVR_EXCL_START */
/* Read a keypress, translating escape sequences. */
static int read_key_bracket_number_sequence(int c3)
{
    int c4 = vigil_platform_terminal_read_byte();

    if (c4 != '~')
    {
        return KEY_NONE;
    }

    switch (c3)
    {
    case '1':
    case '7':
        return KEY_HOME;
    case '3':
        return KEY_DELETE;
    case '4':
    case '8':
        return KEY_END;
    default:
        return KEY_NONE;
    }
}

static int read_key_bracket_sequence(void)
{
    int c3 = vigil_platform_terminal_read_byte();

    if (c3 >= '0' && c3 <= '9')
    {
        return read_key_bracket_number_sequence(c3);
    }

    switch (c3)
    {
    case 'A':
        return KEY_ARROW_UP;
    case 'B':
        return KEY_ARROW_DOWN;
    case 'C':
        return KEY_ARROW_RIGHT;
    case 'D':
        return KEY_ARROW_LEFT;
    case 'H':
        return KEY_HOME;
    case 'F':
        return KEY_END;
    default:
        return KEY_NONE;
    }
}

static int read_key_o_sequence(void)
{
    int c3 = vigil_platform_terminal_read_byte();

    switch (c3)
    {
    case 'H':
        return KEY_HOME;
    case 'F':
        return KEY_END;
    default:
        return KEY_NONE;
    }
}

static int read_key_alt_sequence(int c2)
{
    switch (c2)
    {
    case 'b':
        return KEY_ALT_B;
    case 'f':
        return KEY_ALT_F;
    case 'd':
        return KEY_ALT_D;
    default:
        return KEY_NONE;
    }
}

static int read_key(void)
{
    int c = vigil_platform_terminal_read_byte();

    if (c != KEY_ESC)
    {
        return c;
    }

    int c2 = vigil_platform_terminal_read_byte();
    if (c2 == -1)
    {
        return KEY_ESC;
    }
    if (c2 == '[')
    {
        return read_key_bracket_sequence();
    }
    if (c2 == 'O')
    {
        return read_key_o_sequence();
    }

    return read_key_alt_sequence(c2);
}

/* ── Line buffer ─────────────────────────────────────────────────── */

typedef struct
{
    char *buf;
    size_t len;
    size_t cap;
    size_t pos; /* cursor position */
} line_buf_t;

static void lb_init(line_buf_t *lb, char *buf, size_t cap)
{
    lb->buf = buf;
    lb->len = 0;
    lb->cap = cap - 1; /* reserve space for NUL */
    lb->pos = 0;
    lb->buf[0] = '\0';
}

static void lb_insert(line_buf_t *lb, char c)
{
    if (lb->len >= lb->cap)
        return;
    memmove(lb->buf + lb->pos + 1, lb->buf + lb->pos, lb->len - lb->pos);
    lb->buf[lb->pos] = c;
    lb->len++;
    lb->pos++;
    lb->buf[lb->len] = '\0';
}

static void lb_delete_at(line_buf_t *lb, size_t pos)
{
    if (pos >= lb->len)
        return;
    memmove(lb->buf + pos, lb->buf + pos + 1, lb->len - pos - 1);
    lb->len--;
    lb->buf[lb->len] = '\0';
}

static void lb_set(line_buf_t *lb, const char *s)
{
    size_t slen = strlen(s);
    if (slen > lb->cap)
        slen = lb->cap;
    memcpy(lb->buf, s, slen);
    lb->len = slen;
    lb->pos = slen;
    lb->buf[lb->len] = '\0';
}

static void lb_kill_to_end(line_buf_t *lb)
{
    lb->len = lb->pos;
    lb->buf[lb->len] = '\0';
}

static void lb_kill_to_start(line_buf_t *lb)
{
    memmove(lb->buf, lb->buf + lb->pos, lb->len - lb->pos);
    lb->len -= lb->pos;
    lb->pos = 0;
    lb->buf[lb->len] = '\0';
}

static void lb_move_word_backward(line_buf_t *lb)
{
    while (lb->pos > 0 && lb->buf[lb->pos - 1] == ' ')
    {
        lb->pos--;
    }
    while (lb->pos > 0 && lb->buf[lb->pos - 1] != ' ')
    {
        lb->pos--;
    }
}

static void lb_move_word_forward(line_buf_t *lb)
{
    while (lb->pos < lb->len && lb->buf[lb->pos] == ' ')
    {
        lb->pos++;
    }
    while (lb->pos < lb->len && lb->buf[lb->pos] != ' ')
    {
        lb->pos++;
    }
}

static void lb_kill_word_backward(line_buf_t *lb)
{
    size_t old = lb->pos;

    lb_move_word_backward(lb);
    memmove(lb->buf + lb->pos, lb->buf + old, lb->len - old);
    lb->len -= (old - lb->pos);
    lb->buf[lb->len] = '\0';
}

static void lb_kill_word_forward(line_buf_t *lb)
{
    size_t start = lb->pos;

    lb_move_word_forward(lb);
    memmove(lb->buf + start, lb->buf + lb->pos, lb->len - lb->pos);
    lb->len -= (lb->pos - start);
    lb->pos = start;
    lb->buf[lb->len] = '\0';
}

static void lb_transpose_chars(line_buf_t *lb)
{
    char tmp;

    if (lb->pos == 0 || lb->pos >= lb->len)
    {
        return;
    }

    tmp = lb->buf[lb->pos - 1];
    lb->buf[lb->pos - 1] = lb->buf[lb->pos];
    lb->buf[lb->pos] = tmp;
    lb->pos++;
}

/* ── Screen refresh ──────────────────────────────────────────────── */

static void refresh_line(const char *prompt, const line_buf_t *lb)
{
    size_t plen = prompt ? strlen(prompt) : 0;
    /* \r: go to column 0, print prompt + buffer, \x1b[K: clear to EOL */
    fputs("\r", stdout);
    if (prompt)
        fputs(prompt, stdout);
    fwrite(lb->buf, 1, lb->len, stdout);
    fputs("\x1b[K", stdout);
    /* Move cursor to correct position. */
    if (plen + lb->pos < plen + lb->len)
    {
        fprintf(stdout, "\r\x1b[%zuC", plen + lb->pos);
    }
    fflush(stdout);
}
/* GCOVR_EXCL_STOP */

static void line_history_save_current(const line_buf_t *lb, line_history_nav_t *nav)
{
    if (nav->index != nav->history->count)
    {
        return;
    }

    free(nav->saved_line);
    nav->saved_line = malloc(lb->len + 1);
    if (nav->saved_line != NULL)
    {
        memcpy(nav->saved_line, lb->buf, lb->len + 1);
    }
}

static int line_history_can_move_up(const line_history_nav_t *nav)
{
    return nav->history != NULL && nav->history->count > 0 && nav->index > 0;
}

static int line_history_can_move_down(const line_history_nav_t *nav)
{
    return nav->history != NULL && nav->history->count > 0 && nav->index < nav->history->count;
}

static void line_history_move_up(line_buf_t *lb, line_history_nav_t *nav)
{
    if (!line_history_can_move_up(nav))
    {
        return;
    }

    line_history_save_current(lb, nav);
    nav->index--;
    lb_set(lb, nav->history->entries[nav->index]);
}

static void line_history_move_down(line_buf_t *lb, line_history_nav_t *nav)
{
    if (!line_history_can_move_down(nav) || !nav->history)
    {
        return;
    }

    nav->index++;
    if (nav->index == nav->history->count)
    {
        lb_set(lb, nav->saved_line ? nav->saved_line : "");
        return;
    }

    lb_set(lb, nav->history->entries[nav->index]);
}

/* ── History ─────────────────────────────────────────────────────── */

void vigil_line_history_init(vigil_line_history_t *h, size_t max_entries)
{
    h->entries = NULL;
    h->count = 0;
    h->capacity = 0;
    h->max_entries = max_entries > 0 ? max_entries : 1000;
}

void vigil_line_history_free(vigil_line_history_t *h)
{
    for (size_t i = 0; i < h->count; i++)
        free(h->entries[i]);
    free(h->entries);
    h->entries = NULL;
    h->count = 0;
    h->capacity = 0;
}

void vigil_line_history_add(vigil_line_history_t *h, const char *line)
{
    if (!line || !line[0])
        return;
    /* Skip duplicates of the most recent entry. */
    if (h->count > 0 && strcmp(h->entries[h->count - 1], line) == 0)
        return;
    /* Evict oldest if at capacity. */
    if (h->count >= h->max_entries)
    {
        free(h->entries[0]);
        if (h->count > 1)
            memmove(h->entries, h->entries + 1, (h->count - 1) * sizeof(char *));
        h->count--;
    }
    if (h->count >= h->capacity)
    {
        size_t new_cap = h->capacity == 0 ? 64 : h->capacity * 2;
        char **new_entries = realloc(h->entries, new_cap * sizeof(char *));
        if (!new_entries)
            return;
        h->entries = new_entries;
        h->capacity = new_cap;
    }
    h->entries[h->count] = line_strdup(line);
    if (h->entries[h->count])
    {
        h->count++;
    }
}

const char *vigil_line_history_get(const vigil_line_history_t *h, size_t index)
{
    if (index >= h->count)
        return NULL;
    return h->entries[index];
}

void vigil_line_history_clear(vigil_line_history_t *h)
{
    for (size_t i = 0; i < h->count; i++)
        free(h->entries[i]);
    h->count = 0;
}

vigil_status_t vigil_line_history_load(vigil_line_history_t *h, const char *path, vigil_error_t *error)
{
    FILE *f = fopen(path, "r");
    if (!f)
    {
        /* Missing file is not an error — just no history. */
        (void)error;
        return VIGIL_STATUS_OK;
    }
    char line[4096];
    while (fgets(line, (int)sizeof(line), f))
    {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        vigil_line_history_add(h, line);
    }
    fclose(f);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_line_history_save(const vigil_line_history_t *h, const char *path, vigil_error_t *error)
{
    FILE *f = fopen(path, "w");
    if (!f)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "cannot open history file");
        return VIGIL_STATUS_INTERNAL;
    }
    for (size_t i = 0; i < h->count; i++)
        fprintf(f, "%s\n", h->entries[i]);
    fclose(f);
    return VIGIL_STATUS_OK;
}

/* ── Main editing loop ───────────────────────────────────────────── */

static line_edit_result_t edit_line_handle_terminal_keys(int key, line_buf_t *lb, char **saved_line)
{
    if (key == KEY_ENTER)
    {
        fputs("\r\n", stdout);
        fflush(stdout);
        free(*saved_line);
        *saved_line = NULL;
        return LINE_EDIT_SUBMIT;
    }

    if (key == KEY_CTRL_C)
    {
        lb->len = 0;
        lb->pos = 0;
        lb->buf[0] = '\0';
        fputs("^C\r\n", stdout);
        return LINE_EDIT_REFRESH;
    }

    if (key == -1 || key == KEY_CTRL_D)
    {
        if (lb->len == 0)
        {
            free(*saved_line);
            *saved_line = NULL;
            return LINE_EDIT_EOF;
        }
        lb_delete_at(lb, lb->pos);
        return LINE_EDIT_REFRESH;
    }

    return LINE_EDIT_UNHANDLED;
}

static line_edit_result_t edit_line_handle_delete_keys(int key, line_buf_t *lb)
{
    switch (key)
    {
    case KEY_BACKSPACE:
    case KEY_CTRL_H:
        if (lb->pos > 0)
        {
            lb->pos--;
            lb_delete_at(lb, lb->pos);
        }
        return LINE_EDIT_REFRESH;
    case KEY_DELETE:
        lb_delete_at(lb, lb->pos);
        return LINE_EDIT_REFRESH;
    default:
        return LINE_EDIT_UNHANDLED;
    }
}

static line_edit_result_t edit_line_handle_cursor_keys(int key, line_buf_t *lb)
{
    switch (key)
    {
    case KEY_ARROW_LEFT:
    case KEY_CTRL_B:
        if (lb->pos > 0)
        {
            lb->pos--;
        }
        return LINE_EDIT_REFRESH;
    case KEY_ARROW_RIGHT:
    case KEY_CTRL_F:
        if (lb->pos < lb->len)
        {
            lb->pos++;
        }
        return LINE_EDIT_REFRESH;
    default:
        return LINE_EDIT_UNHANDLED;
    }
}

static line_edit_result_t edit_line_handle_home_end_keys(int key, line_buf_t *lb)
{
    switch (key)
    {
    case KEY_HOME:
    case KEY_CTRL_A:
        lb->pos = 0;
        return LINE_EDIT_REFRESH;
    case KEY_END:
    case KEY_CTRL_E:
        lb->pos = lb->len;
        return LINE_EDIT_REFRESH;
    default:
        return LINE_EDIT_UNHANDLED;
    }
}

static line_edit_result_t edit_line_handle_history_keys(int key, line_buf_t *lb, line_history_nav_t *nav)
{
    switch (key)
    {
    case KEY_ARROW_UP:
    case KEY_CTRL_P:
        line_history_move_up(lb, nav);
        return LINE_EDIT_REFRESH;
    case KEY_ARROW_DOWN:
    case KEY_CTRL_N:
        line_history_move_down(lb, nav);
        return LINE_EDIT_REFRESH;
    default:
        return LINE_EDIT_UNHANDLED;
    }
}

static line_edit_result_t edit_line_handle_kill_keys(int key, line_buf_t *lb)
{
    switch (key)
    {
    case KEY_CTRL_K:
        lb_kill_to_end(lb);
        return LINE_EDIT_REFRESH;
    case KEY_CTRL_U:
        lb_kill_to_start(lb);
        return LINE_EDIT_REFRESH;
    case KEY_CTRL_W:
        lb_kill_word_backward(lb);
        return LINE_EDIT_REFRESH;
    case KEY_CTRL_L:
        fputs("\x1b[H\x1b[2J", stdout);
        return LINE_EDIT_REFRESH;
    case KEY_CTRL_T:
        lb_transpose_chars(lb);
        return LINE_EDIT_REFRESH;
    default:
        return LINE_EDIT_UNHANDLED;
    }
}

static line_edit_result_t edit_line_handle_word_keys(int key, line_buf_t *lb)
{
    switch (key)
    {
    case KEY_ALT_B:
        lb_move_word_backward(lb);
        return LINE_EDIT_REFRESH;
    case KEY_ALT_F:
        lb_move_word_forward(lb);
        return LINE_EDIT_REFRESH;
    case KEY_ALT_D:
        lb_kill_word_forward(lb);
        return LINE_EDIT_REFRESH;
    default:
        return LINE_EDIT_UNHANDLED;
    }
}

static line_edit_result_t edit_line_handle_literal_key(int key, line_buf_t *lb)
{
    if (key == KEY_TAB || key == KEY_NONE)
    {
        return LINE_EDIT_UNHANDLED;
    }

    if (key >= 32 && key < 127)
    {
        lb_insert(lb, (char)key);
        return LINE_EDIT_REFRESH;
    }

    return LINE_EDIT_UNHANDLED;
}

static line_edit_result_t edit_line_dispatch_key(int key, line_buf_t *lb, line_history_nav_t *nav)
{
    line_edit_result_t result;

    result = edit_line_handle_terminal_keys(key, lb, &nav->saved_line);
    if (result != LINE_EDIT_UNHANDLED)
    {
        return result;
    }

    result = edit_line_handle_delete_keys(key, lb);
    if (result != LINE_EDIT_UNHANDLED)
    {
        return result;
    }

    result = edit_line_handle_cursor_keys(key, lb);
    if (result != LINE_EDIT_UNHANDLED)
    {
        return result;
    }

    result = edit_line_handle_home_end_keys(key, lb);
    if (result != LINE_EDIT_UNHANDLED)
    {
        return result;
    }

    result = edit_line_handle_history_keys(key, lb, nav);
    if (result != LINE_EDIT_UNHANDLED)
    {
        return result;
    }

    result = edit_line_handle_kill_keys(key, lb);
    if (result != LINE_EDIT_UNHANDLED)
    {
        return result;
    }

    result = edit_line_handle_word_keys(key, lb);
    if (result != LINE_EDIT_UNHANDLED)
    {
        return result;
    }

    return edit_line_handle_literal_key(key, lb);
}

static vigil_status_t edit_line(const char *prompt, char *out_buf, size_t buf_size, vigil_line_history_t *history)
{
    line_buf_t lb;
    line_history_nav_t nav;
    vigil_status_t status = VIGIL_STATUS_OK;

    lb_init(&lb, out_buf, buf_size);
    nav.history = history;
    nav.index = history ? history->count : 0;
    nav.saved_line = NULL;

    refresh_line(prompt, &lb);

    for (;;)
    {
        int key = read_key();
        line_edit_result_t result = edit_line_dispatch_key(key, &lb, &nav);

        if (result == LINE_EDIT_SUBMIT)
        {
            goto done;
        }
        if (result == LINE_EDIT_EOF)
        {
            status = VIGIL_STATUS_INTERNAL;
            goto done;
        }

        if (result == LINE_EDIT_REFRESH)
        {
            refresh_line(prompt, &lb);
        }
    }

done:
    free(nav.saved_line);
    return status;
}

/* ── Public API ──────────────────────────────────────────────────── */

vigil_status_t vigil_line_editor_readline(const char *prompt, char *out_buf, size_t buf_size,
                                          vigil_line_history_t *history, vigil_error_t *error)
{
    vigil_terminal_state_t *term_state = NULL;
    vigil_status_t status;

    if (!out_buf || buf_size == 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "platform: NULL argument");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Non-terminal: fall back to fgets. */
    if (!vigil_platform_is_terminal())
    {
        size_t len;
        if (prompt)
        {
            fputs(prompt, stdout);
            fflush(stdout);
        }
        if (!fgets(out_buf, (int)buf_size, stdin))
        {
            out_buf[0] = '\0';
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "platform: EOF on stdin");
            return VIGIL_STATUS_INTERNAL;
        }
        len = strlen(out_buf);
        while (len > 0 && (out_buf[len - 1] == '\n' || out_buf[len - 1] == '\r'))
            out_buf[--len] = '\0';
        return VIGIL_STATUS_OK;
    }

    /* Terminal: enter raw mode, edit, restore. */
    status = vigil_platform_terminal_raw(&term_state, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = edit_line(prompt, out_buf, buf_size, history);

    vigil_platform_terminal_restore(term_state);

    if (status != VIGIL_STATUS_OK)
    {
        out_buf[0] = '\0';
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "platform: EOF on stdin");
    }
    return status;
}
