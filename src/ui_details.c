/*
 * ui_details.c - "Medicine details" window: every field of one medicine,
 * its suppliers, all transactions for it, and Sell / Remove buttons.
 */
#include "app.h"

enum { F_ID, F_NAME, F_CATEGORY, F_PRICE, F_QTY, F_VALUE, F_BATCH, F_MFG, F_EXPIRY,
       F_RX, F_STATUS, F_SUPPLIERS, F_UNITS_SOLD, F_COUNT };

static const char *FIELD_NAMES[F_COUNT] = {
    "Medicine ID", "Name", "Category", "Unit price", "Quantity in stock", "Stock value",
    "Batch number", "Manufacture date", "Expiry date", "Prescription only", "Status",
    "Supplied by", "Units sold (all time)"
};

typedef struct {
    GtkWidget    *window;
    char          id[ID_LEN];
    GtkWidget    *title;
    GtkWidget    *values[F_COUNT];
    GtkWidget    *sell_btn;
    GtkListStore *tx;
} DetailsWindow;

static DetailsWindow *find_window(const char *id)
{
    for (GList *l = app.details_windows; l; l = l->next) {
        DetailsWindow *w = l->data;
        if (g_ascii_strcasecmp(w->id, id) == 0) return w;
    }
    return NULL;
}

static void set_value(DetailsWindow *w, int f, const char *fmt, ...) G_GNUC_PRINTF(3, 4);
static void set_value(DetailsWindow *w, int f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *s = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    gtk_label_set_text(GTK_LABEL(w->values[f]), s);
    g_free(s);
}

static void details_refresh(DetailsWindow *w, const Medicine *m)
{
    gtk_label_set_text(GTK_LABEL(w->title), m->name);
    set_value(w, F_ID, "%s", m->id);
    set_value(w, F_NAME, "%s", m->name);
    set_value(w, F_CATEGORY, "%s", category_to_string(m->category));
    set_value(w, F_PRICE, "%.2f", m->unit_price);
    set_value(w, F_QTY, "%d", m->quantity);
    set_value(w, F_VALUE, "%.2f", medicine_stock_value(m));
    set_value(w, F_BATCH, "%s", m->batch);
    set_value(w, F_MFG, "%s", m->mfg_date);
    set_value(w, F_EXPIRY, "%s", m->expiry_date);
    set_value(w, F_RX, "%s", m->rx_only ? "Yes - prescription number required" : "No");

    char *status = medicine_status_text(m);
    const char *color = medicine_is_expired(m) ? "#6b6b6b" : medicine_is_low_stock(m) ? "#b71c1c"
                      : medicine_is_expiring_soon(m) ? "#bf5000" : PHARMACY_GREEN;
    char *markup = g_markup_printf_escaped("<span foreground='%s'><b>%s</b></span>", color, status);
    gtk_label_set_markup(GTK_LABEL(w->values[F_STATUS]), markup);
    g_free(markup);
    g_free(status);

    GString *sups = g_string_new(NULL);
    for (guint i = 0; i < app.store->suppliers->len; i++) {
        Supplier *s = g_ptr_array_index(app.store->suppliers, i);
        if (supplier_supplies(s, m->id))
            g_string_append_printf(sups, "%s%s (%s)", sups->len ? ", " : "", s->name, s->phone);
    }
    set_value(w, F_SUPPLIERS, "%s", sups->len ? sups->str : "-");
    g_string_free(sups, TRUE);

    int sold = 0;
    for (guint i = 0; i < app.store->sales->len; i++) {
        Sale *s = g_ptr_array_index(app.store->sales, i);
        if (g_ascii_strcasecmp(s->medicine_id, m->id) == 0) sold += s->quantity;
    }
    set_value(w, F_UNITS_SOLD, "%d", sold);

    gtk_widget_set_sensitive(w->sell_btn, !medicine_is_expired(m) && m->quantity > 0);
    gtk_widget_set_tooltip_text(w->sell_btn, medicine_is_expired(m) ? "Expired medicine cannot be sold"
                                           : m->quantity == 0 ? "Out of stock" : NULL);

    gtk_list_store_clear(w->tx);
    for (guint i = app.store->history->len; i-- > 0;) {   /* newest first */
        Transaction *t = g_ptr_array_index(app.store->history, i);
        if (g_ascii_strcasecmp(t->medicine_id, m->id) == 0)
            gtk_list_store_insert_with_values(w->tx, NULL, -1, 0, t->timestamp, 1, t->action,
                                              2, t->user, 3, t->details, -1);
    }
}

void ui_details_refresh_all(void)
{
    GList *l = app.details_windows;
    while (l) {
        GList *next = l->next;
        DetailsWindow *w = l->data;
        Medicine *m = store_find_medicine(app.store, w->id);
        if (m) details_refresh(w, m);
        else gtk_widget_destroy(w->window);       /* medicine removed */
        l = next;
    }
}

static void on_destroy(GtkWidget *win, DetailsWindow *w)
{
    (void)win;
    app.details_windows = g_list_remove(app.details_windows, w);
    g_object_unref(w->tx);
    g_free(w);
}

static void on_sell(DetailsWindow *w)   { ui_sell_dialog(w->window, w->id); }
static void on_remove(DetailsWindow *w)
{
    char id[ID_LEN];
    g_strlcpy(id, w->id, sizeof id);       /* w is freed if the removal succeeds */
    ui_remove_medicine_confirmed(w->window, id);
}
static void on_close(DetailsWindow *w)  { gtk_widget_destroy(w->window); }

void ui_details_window_open(const char *medicine_id)
{
    Medicine *m = store_find_medicine(app.store, medicine_id);
    if (!m) { ui_error(app.window, "No medicine with ID '%s'.", medicine_id); return; }

    DetailsWindow *w = find_window(m->id);
    if (w) { gtk_window_present(GTK_WINDOW(w->window)); return; }

    w = g_new0(DetailsWindow, 1);
    g_strlcpy(w->id, m->id, sizeof w->id);
    w->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_transient_for(GTK_WINDOW(w->window), GTK_WINDOW(app.window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(w->window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(w->window), 820, 640);
    char *title = g_strdup_printf("Medicine details - %s", m->id);
    gtk_window_set_title(GTK_WINDOW(w->window), title);
    g_free(title);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    g_object_set(box, "margin", 16, NULL);

    w->title = gtk_label_new(NULL);
    gtk_widget_set_halign(w->title, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(w->title), "details-title");
    gtk_box_pack_start(GTK_BOX(box), w->title, FALSE, FALSE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
    for (int f = 0; f < F_COUNT; f++) {
        /* two columns of fields */
        int col = (f % 2) * 2, row = f / 2;
        GtkWidget *name = gtk_label_new(FIELD_NAMES[f]);
        gtk_widget_set_halign(name, GTK_ALIGN_END);
        gtk_style_context_add_class(gtk_widget_get_style_context(name), "field-name");
        w->values[f] = gtk_label_new("");
        gtk_widget_set_halign(w->values[f], GTK_ALIGN_START);
        gtk_label_set_selectable(GTK_LABEL(w->values[f]), TRUE);
        gtk_label_set_line_wrap(GTK_LABEL(w->values[f]), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(w->values[f]), 32);
        gtk_grid_attach(GTK_GRID(grid), name, col, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), w->values[f], col + 1, row, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);

    GtkWidget *tx_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(tx_title), "<b>Transactions for this medicine</b>");
    gtk_widget_set_halign(tx_title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), tx_title, FALSE, FALSE, 0);

    w->tx = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(w->tx));
    const char *titles[] = { "Date / time", "Action", "User", "Details" };
    for (int i = 0; i < 4; i++) {
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
            titles[i], gtk_cell_renderer_text_new(), "text", i, NULL);
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(view), c);
    }
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), view);
    gtk_box_pack_start(GTK_BOX(box), sw, TRUE, TRUE, 0);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    w->sell_btn = ui_button("_Sell this medicine", "primary", G_CALLBACK(on_sell), w);
    gtk_box_pack_start(GTK_BOX(buttons), w->sell_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), ui_button("_Remove medicine", "danger", G_CALLBACK(on_remove), w), FALSE, FALSE, 0);
    GtkWidget *close = ui_button("_Close", NULL, G_CALLBACK(on_close), w);
    gtk_box_pack_end(GTK_BOX(buttons), close, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(w->window), ui_toast_host(w->window, box));
    g_signal_connect(w->window, "destroy", G_CALLBACK(on_destroy), w);
    app.details_windows = g_list_prepend(app.details_windows, w);

    details_refresh(w, m);
    gtk_widget_show_all(w->window);
    gtk_widget_grab_focus(close);   /* otherwise the first selectable label is highlighted */
}
