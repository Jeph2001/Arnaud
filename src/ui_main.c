/*
 * ui_main.c - Main window: sidebar of actions + tabs with the tables.
 */
#include "app.h"

#include <string.h>

App app;

enum { MED_ID, MED_NAME, MED_CATEGORY, MED_QTY, MED_PRICE, MED_BATCH, MED_EXPIRY,
       MED_RX, MED_STATUS, MED_FG, MED_BG, MED_NCOLS };
enum { SALE_ID, SALE_TIME, SALE_MED, SALE_QTY, SALE_TOTAL, SALE_DISC, SALE_FINAL,
       SALE_CUSTOMER, SALE_RX, SALE_USER, SALE_NCOLS };
enum { SUP_ID, SUP_NAME, SUP_PHONE, SUP_MEDS, SUP_NCOLS };
enum { HIST_TIME, HIST_USER, HIST_ACTION, HIST_MED, HIST_DETAILS, HIST_NCOLS };

/* ------------------------- table helpers -------------------------- */

static GtkTreeViewColumn *add_column(GtkWidget *view, const char *title, int col,
                                     gboolean colored, int fg_col, int bg_col)
{
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(title, r, "text", col, NULL);
    if (colored) {
        gtk_tree_view_column_add_attribute(c, r, "foreground", fg_col);
        gtk_tree_view_column_add_attribute(c, r, "cell-background", bg_col);
    }
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_column_set_sort_column_id(c, col);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), c);
    return c;
}

static GtkWidget *scrolled(GtkWidget *child)
{
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_container_add(GTK_CONTAINER(sw), child);
    return sw;
}

/* Returns the string in `col` of the selected row of `view` (static buffer). */
static const char *selected_string(GtkWidget *view, int col)
{
    static char buf[128];
    GtkTreeModel *model;
    GtkTreeIter it;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    if (!gtk_tree_selection_get_selected(sel, &model, &it)) return NULL;
    char *v = NULL;
    gtk_tree_model_get(model, &it, col, &v, -1);
    g_strlcpy(buf, v ? v : "", sizeof buf);
    g_free(v);
    return buf;
}

const char *ui_selected_medicine_id(void)
{
    return app.med_view ? selected_string(app.med_view, MED_ID) : NULL;
}

static gint64 selected_sale_id(void)
{
    GtkTreeModel *model;
    GtkTreeIter it;
    gint64 id = -1;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(app.sales_view)), &model, &it))
        gtk_tree_model_get(model, &it, SALE_ID, &id, -1);
    return id;
}

/* ---------------------------- refresh ----------------------------- */

static void refresh_medicines(void)
{
    int expired = 0;
    gtk_list_store_clear(app.med_list);
    for (guint i = 0; i < app.store->medicines->len; i++) {
        Medicine *m = g_ptr_array_index(app.store->medicines, i);
        const char *fg = NULL, *bg = NULL;
        /* Priority: expired (grey) > low stock (red) > expiring soon (orange). */
        if (medicine_is_expired(m))            { fg = "#6b6b6b"; bg = "#e4e4e4"; expired++; }
        else if (medicine_is_low_stock(m))     { fg = "#b71c1c"; bg = "#fdecea"; }
        else if (medicine_is_expiring_soon(m)) { fg = "#bf5000"; bg = "#fff1dc"; }

        char price[32];
        char *status = medicine_status_text(m);
        g_snprintf(price, sizeof price, "%.2f", m->unit_price);
        gtk_list_store_insert_with_values(app.med_list, NULL, -1,
            MED_ID, m->id, MED_NAME, m->name, MED_CATEGORY, category_to_string(m->category),
            MED_QTY, m->quantity, MED_PRICE, price, MED_BATCH, m->batch,
            MED_EXPIRY, m->expiry_date, MED_RX, m->rx_only ? "Yes" : "No",
            MED_STATUS, status, MED_FG, fg, MED_BG, bg, -1);
        g_free(status);
    }
    char *label = g_strdup_printf("Remove _Expired (%d)", expired);
    gtk_button_set_label(GTK_BUTTON(app.remove_expired_btn), label);
    gtk_widget_set_sensitive(app.remove_expired_btn, expired > 0);
    g_free(label);
}

static void refresh_sales(void)
{
    gtk_list_store_clear(app.sales_list);
    for (guint i = 0; i < app.store->sales->len; i++) {
        Sale *s = g_ptr_array_index(app.store->sales, i);
        char total[32], disc[48], fin[32], med[160];
        g_snprintf(total, sizeof total, "%.2f", s->total);
        g_snprintf(fin, sizeof fin, "%.2f", s->final_total);
        if (s->discount_pct > 0) g_snprintf(disc, sizeof disc, "%.1f%% %s", s->discount_pct, s->discount_reason);
        else g_strlcpy(disc, "-", sizeof disc);
        g_snprintf(med, sizeof med, "%s (%s)", s->medicine_name, s->medicine_id);
        gtk_list_store_insert_with_values(app.sales_list, NULL, 0,   /* newest first */
            SALE_ID, s->id, SALE_TIME, s->timestamp, SALE_MED, med, SALE_QTY, s->quantity,
            SALE_TOTAL, total, SALE_DISC, disc, SALE_FINAL, fin, SALE_CUSTOMER, s->customer,
            SALE_RX, s->prescription_no, SALE_USER, s->user, -1);
    }
}

static void refresh_suppliers(void)
{
    gtk_list_store_clear(app.sup_list);
    for (guint i = 0; i < app.store->suppliers->len; i++) {
        Supplier *s = g_ptr_array_index(app.store->suppliers, i);
        char *meds = supplier_medicines_text(s);
        gtk_list_store_insert_with_values(app.sup_list, NULL, -1, SUP_ID, s->id, SUP_NAME, s->name,
                                          SUP_PHONE, s->phone, SUP_MEDS, meds, -1);
        g_free(meds);
    }
}

static void refresh_history(void)
{
    gtk_list_store_clear(app.hist_list);
    for (guint i = 0; i < app.store->history->len; i++) {
        Transaction *t = g_ptr_array_index(app.store->history, i);
        gtk_list_store_insert_with_values(app.hist_list, NULL, -1, HIST_TIME, t->timestamp,
                                          HIST_USER, t->user, HIST_ACTION, t->action,
                                          HIST_MED, t->medicine_id, HIST_DETAILS, t->details, -1);
    }
    /* Keep the newest entry visible. */
    int n = gtk_tree_model_iter_n_children(app.hist_filter, NULL);
    if (n > 0) {
        GtkTreePath *p = gtk_tree_path_new_from_indices(n - 1, -1);
        gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(app.hist_view), p, NULL, FALSE, 0, 0);
        gtk_tree_path_free(p);
    }
}

static void refresh_summary(void)
{
    StoreStats st;
    store_stats(app.store, &st);
    char *text = g_strdup_printf(
        "<b>%d</b> medicines  •  <b>%ld</b> units  •  stock value <b>%.2f</b>  •  "
        "<span foreground='#b71c1c'><b>%d</b> low stock</span>  •  "
        "<span foreground='#bf5000'><b>%d</b> expiring ≤30 days</span>  •  "
        "<span foreground='#6b6b6b'><b>%d</b> expired</span>  •  "
        "<b>%d</b> sale(s) today (%.2f)",
        st.total_medicines, st.total_quantity, st.stock_value, st.low_stock,
        st.expiring_soon, st.expired, st.sales_today_count, st.sales_today_total);
    gtk_label_set_markup(GTK_LABEL(app.summary_label), text);
    g_free(text);
}

void app_refresh_all(void)
{
    refresh_medicines();
    refresh_sales();
    refresh_suppliers();
    refresh_history();
    refresh_summary();
    ui_details_refresh_all();
}

/* ---------------------------- filters ----------------------------- */

static gboolean contains_ci(const char *hay, const char *needle)
{
    if (!needle || !*needle) return TRUE;
    char *h = g_utf8_casefold(hay ? hay : "", -1);
    char *n = g_utf8_casefold(needle, -1);
    gboolean r = strstr(h, n) != NULL;
    g_free(h);
    g_free(n);
    return r;
}

static gboolean med_visible(GtkTreeModel *model, GtkTreeIter *it, gpointer data)
{
    (void)data;
    const char *q = gtk_entry_get_text(GTK_ENTRY(app.med_search));
    if (!*q) return TRUE;
    char *id, *name, *cat, *batch, *status;
    gtk_tree_model_get(model, it, MED_ID, &id, MED_NAME, &name, MED_CATEGORY, &cat,
                       MED_BATCH, &batch, MED_STATUS, &status, -1);
    gboolean v = contains_ci(id, q) || contains_ci(name, q) || contains_ci(cat, q) ||
                 contains_ci(batch, q) || contains_ci(status, q);
    g_free(id); g_free(name); g_free(cat); g_free(batch); g_free(status);
    return v;
}

static gboolean hist_visible(GtkTreeModel *model, GtkTreeIter *it, gpointer data)
{
    (void)data;
    const char *med_q = gtk_entry_get_text(GTK_ENTRY(app.hist_med_entry));
    const char *date_q = gtk_entry_get_text(GTK_ENTRY(app.hist_date_entry));
    char *med, *ts;
    gtk_tree_model_get(model, it, HIST_MED, &med, HIST_TIME, &ts, -1);
    gboolean v = contains_ci(med, med_q) && g_str_has_prefix(ts ? ts : "", date_q);
    g_free(med);
    g_free(ts);
    return v;
}

static void refilter(GtkTreeModel *filter)
{
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(filter));
}

/* ---------------------------- actions ----------------------------- */

static void on_view_details(void)
{
    const char *id = ui_selected_medicine_id();
    if (!id) { ui_error(app.window, "Select a medicine in the table first."); return; }
    ui_details_window_open(id);
}

static void on_row_activated(GtkTreeView *v, GtkTreePath *p, GtkTreeViewColumn *c, gpointer d)
{
    (void)v; (void)p; (void)c; (void)d;
    on_view_details();
}

static void on_add(void)           { ui_add_medicine_dialog(); }
static void on_update(void)        { ui_update_stock_dialog(ui_selected_medicine_id()); }
static void on_sell(void)          { ui_sell_dialog(app.window, ui_selected_medicine_id()); }
static void on_check(void)         { ui_check_stock_dialog(ui_selected_medicine_id()); }
static void on_value(void)         { ui_stock_value_dialog(); }
static void on_remove(void)        { ui_remove_medicine_dialog(ui_selected_medicine_id()); }
static void on_report(void)        { ui_report_window(); }
static void on_remove_expired(void){ ui_remove_expired(); }

static void on_discount(void)
{
    gint64 id = selected_sale_id();
    if (id < 0) { ui_error(app.window, "Select a sale in the Sales table first."); return; }
    ui_discount_dialog(id);
}

static void on_receipt(void)
{
    Sale *s = store_find_sale(app.store, selected_sale_id());
    if (!s) { ui_error(app.window, "Select a sale in the Sales table first."); return; }
    ui_show_receipt(app.window, s);
}

static void on_add_supplier(void) { ui_add_supplier_dialog(); }

static void on_remove_supplier(void)
{
    const char *id = selected_string(app.sup_view, SUP_ID);
    if (!id) { ui_error(app.window, "Select a supplier in the table first."); return; }
    ui_remove_supplier(id);
}

static void on_clear_hist_filter(void)
{
    gtk_entry_set_text(GTK_ENTRY(app.hist_med_entry), "");
    gtk_entry_set_text(GTK_ENTRY(app.hist_date_entry), "");
}

/* ----------------------------- layout ----------------------------- */

static GtkWidget *toolbar_box(void)
{
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    g_object_set(b, "margin", 8, NULL);
    return b;
}

static GtkWidget *build_medicines_tab(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *bar = toolbar_box();

    app.med_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.med_search), "Search by ID, name, category, batch or status...");
    gtk_widget_set_hexpand(app.med_search, TRUE);
    gtk_box_pack_start(GTK_BOX(bar), app.med_search, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bar), ui_button("View _Details", "primary", G_CALLBACK(on_view_details), NULL), FALSE, FALSE, 0);
    app.remove_expired_btn = ui_button("Remove _Expired (0)", "danger", G_CALLBACK(on_remove_expired), NULL);
    gtk_widget_set_tooltip_text(app.remove_expired_btn, "Remove every expired medicine in one click");
    gtk_box_pack_start(GTK_BOX(bar), app.remove_expired_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);

    app.med_list = gtk_list_store_new(MED_NCOLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT,
                                      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    app.med_filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(app.med_list), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(app.med_filter), med_visible, NULL, NULL);
    GtkTreeModel *sorted = gtk_tree_model_sort_new_with_model(app.med_filter);
    app.med_view = gtk_tree_view_new_with_model(sorted);
    g_object_unref(sorted);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(app.med_view), MED_NAME);

    const char *titles[] = { "ID", "Name", "Category", "Quantity", "Unit price", "Batch", "Expiry date", "Rx only", "Status" };
    for (int i = 0; i <= MED_STATUS; i++)
        add_column(app.med_view, titles[i], i, TRUE, MED_FG, MED_BG);
    g_signal_connect(app.med_view, "row-activated", G_CALLBACK(on_row_activated), NULL);
    g_signal_connect_swapped(app.med_search, "search-changed", G_CALLBACK(refilter), app.med_filter);

    gtk_box_pack_start(GTK_BOX(box), scrolled(app.med_view), TRUE, TRUE, 0);

    GtkWidget *legend = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(legend),
        "<span foreground='#b71c1c'>■ Quantity below 10</span>     "
        "<span foreground='#bf5000'>■ Expires within 30 days</span>     "
        "<span foreground='#6b6b6b'>■ Expired (cannot be sold)</span>     "
        "Double-click a row to open its details.");
    gtk_widget_set_halign(legend, GTK_ALIGN_START);
    g_object_set(legend, "margin", 6, NULL);
    gtk_box_pack_start(GTK_BOX(box), legend, FALSE, FALSE, 0);
    return box;
}

static GtkWidget *build_sales_tab(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *bar = toolbar_box();
    gtk_box_pack_start(GTK_BOX(bar), ui_button("Apply _Discount", "primary", G_CALLBACK(on_discount), NULL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), ui_button("View _Receipt", NULL, G_CALLBACK(on_receipt), NULL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);

    app.sales_list = gtk_list_store_new(SALE_NCOLS, G_TYPE_INT64, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT,
                                        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                        G_TYPE_STRING, G_TYPE_STRING);
    app.sales_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app.sales_list));
    const char *titles[] = { "Sale #", "Date / time", "Medicine", "Qty", "Amount", "Discount",
                             "Final amount", "Customer", "Prescription", "Sold by" };
    for (int i = 0; i < SALE_NCOLS; i++) add_column(app.sales_view, titles[i], i, FALSE, 0, 0);
    gtk_box_pack_start(GTK_BOX(box), scrolled(app.sales_view), TRUE, TRUE, 0);
    return box;
}

static GtkWidget *build_suppliers_tab(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *bar = toolbar_box();
    gtk_box_pack_start(GTK_BOX(bar), ui_button("_Add Supplier", "primary", G_CALLBACK(on_add_supplier), NULL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), ui_button("_Remove Supplier", "danger", G_CALLBACK(on_remove_supplier), NULL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);

    app.sup_list = gtk_list_store_new(SUP_NCOLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    app.sup_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app.sup_list));
    const char *titles[] = { "Supplier ID", "Name", "Phone", "Medicines supplied" };
    for (int i = 0; i < SUP_NCOLS; i++) add_column(app.sup_view, titles[i], i, FALSE, 0, 0);
    gtk_box_pack_start(GTK_BOX(box), scrolled(app.sup_view), TRUE, TRUE, 0);
    return box;
}

static GtkWidget *build_history_tab(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *bar = toolbar_box();

    app.hist_med_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.hist_med_entry), "Filter by medicine ID");
    app.hist_date_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.hist_date_entry), "Filter by date (YYYY-MM-DD, or YYYY-MM)");
    gtk_entry_set_width_chars(GTK_ENTRY(app.hist_date_entry), 32);
    gtk_box_pack_start(GTK_BOX(bar), app.hist_med_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), app.hist_date_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), ui_button("_Clear filters", NULL, G_CALLBACK(on_clear_hist_filter), NULL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);

    app.hist_list = gtk_list_store_new(HIST_NCOLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                       G_TYPE_STRING, G_TYPE_STRING);
    app.hist_filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(app.hist_list), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(app.hist_filter), hist_visible, NULL, NULL);
    app.hist_view = gtk_tree_view_new_with_model(app.hist_filter);
    const char *titles[] = { "Date / time", "User", "Action", "Medicine ID", "Details" };
    for (int i = 0; i < HIST_NCOLS; i++) {
        GtkTreeViewColumn *c = add_column(app.hist_view, titles[i], i, FALSE, 0, 0);
        gtk_tree_view_column_set_sort_column_id(c, -1);   /* stays chronological */
    }
    g_signal_connect_swapped(app.hist_med_entry, "search-changed", G_CALLBACK(refilter), app.hist_filter);
    g_signal_connect_swapped(app.hist_date_entry, "search-changed", G_CALLBACK(refilter), app.hist_filter);
    gtk_box_pack_start(GTK_BOX(box), scrolled(app.hist_view), TRUE, TRUE, 0);
    return box;
}

static GtkWidget *build_sidebar(void)
{
    GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(side), "sidebar");
    g_object_set(side, "margin", 0, NULL);
    gtk_widget_set_size_request(side, 200, -1);

    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    g_object_set(inner, "margin", 12, NULL);
    gtk_box_pack_start(GTK_BOX(side), inner, TRUE, TRUE, 0);

    struct { const char *label; const char *css; GCallback cb; } items[] = {
        { "_Add Medicine",          "primary", G_CALLBACK(on_add)    },
        { "_Update Stock",          NULL,      G_CALLBACK(on_update) },
        { "_Sell Medicine",         "primary", G_CALLBACK(on_sell)   },
        { "_Check Stock & Expiry", NULL,      G_CALLBACK(on_check)  },
        { "Stock _Value",           NULL,      G_CALLBACK(on_value)  },
        { "Re_move Medicine",       "danger",  G_CALLBACK(on_remove) },
        { "Generate _Report",       NULL,      G_CALLBACK(on_report) },
    };
    GtkWidget *title = gtk_label_new("ACTIONS");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "sidebar-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(inner), title, FALSE, FALSE, 0);
    for (gsize i = 0; i < G_N_ELEMENTS(items); i++)
        gtk_box_pack_start(GTK_BOX(inner), ui_button(items[i].label, items[i].css, items[i].cb, NULL), FALSE, FALSE, 0);

    GtkWidget *hint = gtk_label_new("Tip: select a medicine in the table and the dialogs will be pre-filled with its ID.");
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(hint), 22);
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "field-name");
    gtk_box_pack_end(GTK_BOX(inner), hint, FALSE, FALSE, 0);
    return side;
}

GtkWidget *ui_main_window_new(GtkApplication *gapp)
{
    GtkWidget *win = gtk_application_window_new(gapp);
    app.window = win;
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 720);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "✚ Pharmacy Management System");
    char *sub = g_strdup_printf("Signed in as %s  •  %s", app.store->user, app.store->db_path);
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), sub);
    g_free(sub);
    gtk_window_set_titlebar(GTK_WINDOW(win), header);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    app.summary_label = gtk_label_new(NULL);
    gtk_widget_set_halign(app.summary_label, GTK_ALIGN_FILL);
    gtk_label_set_xalign(GTK_LABEL(app.summary_label), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.summary_label), "summary");
    gtk_box_pack_start(GTK_BOX(root), app.summary_label, FALSE, FALSE, 0);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(body), build_sidebar(), FALSE, FALSE, 0);

    GtkWidget *nb = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), build_medicines_tab(), gtk_label_new("Medicines"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), build_sales_tab(), gtk_label_new("Sales"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), build_suppliers_tab(), gtk_label_new("Suppliers"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), build_history_tab(), gtk_label_new("Transaction History"));
    gtk_box_pack_start(GTK_BOX(body), nb, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), body, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(win), ui_toast_host(win, root));
    app_refresh_all();
    return win;
}
