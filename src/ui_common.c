/*
 * ui_common.c - Theme, notifications and small form helpers.
 *
 * Notification policy:
 *   success -> toast at the bottom of the window, disappears after 3 s
 *   error   -> modal error dialog, stays until the user closes it
 *   confirm -> Yes/No dialog that closes itself after 4 s (= "No")
 */
#include "app.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define TOAST_MS        3000
#define CONFIRM_SECONDS 4

static const char *CSS =
    "headerbar { background: " PHARMACY_GREEN "; color: white; }\n"
    "headerbar .title, headerbar .subtitle { color: white; }\n"
    "headerbar button { color: white; }\n"
    /* tints rather than fixed colours so light and dark desktop themes both work */
    ".sidebar { background: alpha(" PHARMACY_GREEN ", 0.08); border-right: 1px solid alpha(" PHARMACY_GREEN ", 0.3); }\n"
    ".sidebar-title { font-weight: bold; color: " PHARMACY_GREEN "; margin: 6px 0; }\n"
    "button.primary { background: " PHARMACY_GREEN "; background-image: none; color: white;"
    "  border-color: #006b31; font-weight: bold; }\n"
    "button.primary:hover { background: #009a48; }\n"
    "button.primary:disabled { background: #9cc9ad; color: #eef; }\n"
    "button.danger { background: #c62828; background-image: none; color: white; border-color: #a11; }\n"
    "button.danger:hover { background: #d84343; }\n"
    ".toast { background: " PHARMACY_GREEN "; color: white; border-radius: 18px;"
    "  padding: 10px 22px; font-weight: bold; margin-bottom: 18px; }\n"
    ".summary { padding: 6px 12px; background: #e6f4ec; color: #114d2c; }\n"
    ".details-title { font-size: 20px; font-weight: bold; color: " PHARMACY_GREEN "; }\n"
    ".field-name { opacity: 0.7; }\n"
    ".mono { font-family: monospace; }\n"
    "notebook tab:checked label { color: " PHARMACY_GREEN "; font-weight: bold; }\n"
    "notebook > header.top > tabs > tab:checked { box-shadow: inset 0 -3px " PHARMACY_GREEN "; }\n";

void ui_install_css(void)
{
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p, CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(p),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

static GtkWindow *as_window(GtkWidget *w)
{
    GtkWidget *top = w ? gtk_widget_get_toplevel(w) : NULL;
    if (top && GTK_IS_WINDOW(top)) return GTK_WINDOW(top);
    return app.window ? GTK_WINDOW(app.window) : NULL;
}

/* ------------------------------ toasts ---------------------------- */

typedef struct {
    GtkWidget *revealer;
    GtkWidget *label;
    guint      timer;
} Toast;

static void toast_free(gpointer p)
{
    Toast *t = p;
    if (t->timer) g_source_remove(t->timer);
    g_free(t);
}

/* Wrap a window's content in an overlay that can show a toast. */
GtkWidget *ui_toast_host(GtkWidget *window, GtkWidget *content)
{
    GtkWidget *overlay = gtk_overlay_new();
    Toast *t = g_new0(Toast, 1);

    gtk_container_add(GTK_CONTAINER(overlay), content);
    t->revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(t->revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_widget_set_halign(t->revealer, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(t->revealer, GTK_ALIGN_END);
    t->label = gtk_label_new(NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(t->label), "toast");
    gtk_container_add(GTK_CONTAINER(t->revealer), t->label);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), t->revealer);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(overlay), t->revealer, TRUE);

    /* Freed (and its timer cancelled) together with the window. */
    g_object_set_data_full(G_OBJECT(window), "toast", t, toast_free);
    return overlay;
}

static gboolean toast_hide(gpointer p)
{
    Toast *t = p;
    gtk_revealer_set_reveal_child(GTK_REVEALER(t->revealer), FALSE);
    t->timer = 0;
    return G_SOURCE_REMOVE;
}

void ui_toast(GtkWidget *near, const char *fmt, ...)
{
    GtkWindow *win = as_window(near);
    Toast *t = win ? g_object_get_data(G_OBJECT(win), "toast") : NULL;
    if (!t && app.window) t = g_object_get_data(G_OBJECT(app.window), "toast");
    if (!t) return;

    va_list ap;
    va_start(ap, fmt);
    char *msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    char *text = g_strdup_printf("✔  %s", msg);
    gtk_label_set_text(GTK_LABEL(t->label), text);
    gtk_revealer_set_reveal_child(GTK_REVEALER(t->revealer), TRUE);
    if (t->timer) g_source_remove(t->timer);
    t->timer = g_timeout_add(TOAST_MS, toast_hide, t);
    g_free(text);
    g_free(msg);
}

/* ----------------------------- dialogs ---------------------------- */

static void message(GtkWidget *parent, GtkMessageType type, const char *title, const char *text)
{
    GtkWidget *d = gtk_message_dialog_new(as_window(parent), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          type, GTK_BUTTONS_CLOSE, "%s", title);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", text);
    gtk_window_set_title(GTK_WINDOW(d), title);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

void ui_error(GtkWidget *parent, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    message(parent, GTK_MESSAGE_ERROR, "Error", msg);
    g_free(msg);
}

void ui_warning(GtkWidget *parent, const char *title, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    message(parent, GTK_MESSAGE_WARNING, title, msg);
    g_free(msg);
}

/* Modal popup showing monospace text (receipts, stock value, ...). */
void ui_text_popup(GtkWidget *parent, const char *title, const char *text)
{
    GtkWidget *d = gtk_dialog_new_with_buttons(title, as_window(parent),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               "_Close", GTK_RESPONSE_CLOSE, NULL);
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "mono");
    gtk_widget_set_margin_start(label, 24);
    gtk_widget_set_margin_end(label, 24);
    gtk_widget_set_margin_top(label, 18);
    gtk_widget_set_margin_bottom(label, 12);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), label);
    gtk_widget_show_all(d);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

typedef struct {
    GtkWidget *dialog;
    char      *text;
    int        left;
    guint      timer;
} Countdown;

static void countdown_label(Countdown *c)
{
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(c->dialog),
        "%s\n\nThis dialog closes in %d s (treated as \"No\").", c->text, c->left);
}

static gboolean countdown_tick(gpointer p)
{
    Countdown *c = p;
    if (--c->left <= 0) {
        c->timer = 0;
        gtk_dialog_response(GTK_DIALOG(c->dialog), GTK_RESPONSE_NO);
        return G_SOURCE_REMOVE;
    }
    countdown_label(c);
    return G_SOURCE_CONTINUE;
}

gboolean ui_confirm(GtkWidget *parent, const char *title, const char *fmt, ...)
{
    va_list ap;
    Countdown c = { 0 };
    va_start(ap, fmt);
    c.text = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    c.left = CONFIRM_SECONDS;

    c.dialog = gtk_message_dialog_new(as_window(parent), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                      GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", title);
    gtk_window_set_title(GTK_WINDOW(c.dialog), "Please confirm");
    gtk_dialog_set_default_response(GTK_DIALOG(c.dialog), GTK_RESPONSE_NO);
    countdown_label(&c);
    c.timer = g_timeout_add_seconds(1, countdown_tick, &c);

    int r = gtk_dialog_run(GTK_DIALOG(c.dialog));
    if (c.timer) g_source_remove(c.timer);
    gtk_widget_destroy(c.dialog);
    g_free(c.text);
    return r == GTK_RESPONSE_YES;
}

/* ------------------------------ forms ----------------------------- */

GtkWidget *ui_form_dialog(GtkWidget *parent, const char *title, const char *ok_label, GtkGrid **grid)
{
    GtkWidget *d = gtk_dialog_new_with_buttons(title, as_window(parent),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               "_Cancel", GTK_RESPONSE_CANCEL,
                                               ok_label, GTK_RESPONSE_OK, NULL);
    GtkWidget *ok = gtk_dialog_get_widget_for_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_style_context_add_class(gtk_widget_get_style_context(ok), "primary");
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);

    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 8);
    gtk_grid_set_column_spacing(GTK_GRID(g), 12);
    g_object_set(g, "margin", 18, NULL);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), g);
    *grid = GTK_GRID(g);
    return d;
}

void ui_form_row(GtkGrid *grid, int row, const char *label, GtkWidget *w)
{
    GtkWidget *l = gtk_label_new(label);
    gtk_widget_set_halign(l, GTK_ALIGN_END);
    gtk_grid_attach(grid, l, 0, row, 1, 1);
    gtk_widget_set_hexpand(w, TRUE);
    gtk_grid_attach(grid, w, 1, row, 1, 1);
}

GtkWidget *ui_form_entry(GtkGrid *grid, int row, const char *label, const char *placeholder)
{
    GtkWidget *e = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(e), 28);
    if (placeholder) gtk_entry_set_placeholder_text(GTK_ENTRY(e), placeholder);
    ui_form_row(grid, row, label, e);
    return e;
}

char *ui_entry_text(GtkWidget *entry)
{
    return g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))));
}

gboolean ui_parse_int(const char *s, int *out)
{
    char *end;
    if (!s || !*s) return FALSE;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || *end || v < G_MININT || v > G_MAXINT) return FALSE;
    *out = (int)v;
    return TRUE;
}

gboolean ui_parse_double(const char *s, double *out)
{
    char *end;
    if (!s || !*s) return FALSE;
    double v = g_ascii_strtod(s, &end);   /* locale independent: always '.' */
    if (*end || v != v) return FALSE;
    *out = v;
    return TRUE;
}

GtkWidget *ui_button(const char *label, const char *css_class, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new_with_mnemonic(label);
    if (css_class) gtk_style_context_add_class(gtk_widget_get_style_context(b), css_class);
    if (cb) g_signal_connect_swapped(b, "clicked", cb, data);
    return b;
}
