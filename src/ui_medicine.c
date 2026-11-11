/*
 * ui_medicine.c - Add / update / check / value / remove medicine dialogs.
 *
 * Form dialogs stay open after a validation error so the user can fix the
 * input; they close only on success or Cancel.
 */
#include "app.h"

void ui_add_medicine_dialog(void)
{
    GtkGrid *g;
    GtkWidget *d = ui_form_dialog(app.window, "Add Medicine", "_Save", &g);
    char today[DATE_LEN];
    date_today_str(today);

    GtkWidget *id    = ui_form_entry(g, 0, "Medicine ID", "e.g. MED001");
    GtkWidget *name  = ui_form_entry(g, 1, "Name", "e.g. Paracetamol 500mg");
    GtkWidget *cat   = gtk_combo_box_text_new();
    for (int i = 0; i < CAT_COUNT; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cat), category_to_string((MedCategory)i));
    gtk_combo_box_set_active(GTK_COMBO_BOX(cat), 0);
    ui_form_row(g, 2, "Category", cat);
    GtkWidget *price = ui_form_entry(g, 3, "Unit price", "e.g. 150.00");
    GtkWidget *qty   = ui_form_entry(g, 4, "Quantity", "e.g. 100");
    GtkWidget *batch = ui_form_entry(g, 5, "Batch number", "e.g. B2026-11");
    GtkWidget *mfg   = ui_form_entry(g, 6, "Manufacture date", "YYYY-MM-DD");
    GtkWidget *exp   = ui_form_entry(g, 7, "Expiry date", "YYYY-MM-DD");
    GtkWidget *rx    = gtk_check_button_new_with_label("Prescription-only medicine");
    gtk_grid_attach(g, rx, 1, 8, 1, 1);
    gtk_entry_set_text(GTK_ENTRY(mfg), today);
    gtk_widget_show_all(d);

    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        Medicine m = { 0 };
        char *s_id = ui_entry_text(id), *s_name = ui_entry_text(name), *s_price = ui_entry_text(price),
             *s_qty = ui_entry_text(qty), *s_batch = ui_entry_text(batch), *s_mfg = ui_entry_text(mfg),
             *s_exp = ui_entry_text(exp);
        char *err = NULL;

        g_strlcpy(m.id, s_id, sizeof m.id);
        g_strlcpy(m.name, s_name, sizeof m.name);
        m.category = (MedCategory)gtk_combo_box_get_active(GTK_COMBO_BOX(cat));
        g_strlcpy(m.batch, s_batch, sizeof m.batch);
        g_strlcpy(m.mfg_date, s_mfg, sizeof m.mfg_date);
        g_strlcpy(m.expiry_date, s_exp, sizeof m.expiry_date);
        m.rx_only = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rx));

        if (!ui_parse_double(s_price, &m.unit_price))
            err = g_strdup("Unit price must be a number (e.g. 150.00).");
        else if (!ui_parse_int(s_qty, &m.quantity))
            err = g_strdup("Quantity must be a whole number.");
        else if (store_add_medicine(app.store, &m, &err)) {
            ui_toast(app.window, "Medicine %s (%s) saved", m.name, m.id);
            app_refresh_all();
        }

        g_free(s_id); g_free(s_name); g_free(s_price); g_free(s_qty);
        g_free(s_batch); g_free(s_mfg); g_free(s_exp);
        if (!err) break;
        ui_error(d, "%s", err);
        g_free(err);
    }
    gtk_widget_destroy(d);
}

void ui_update_stock_dialog(const char *preset_id)
{
    GtkGrid *g;
    GtkWidget *d = ui_form_dialog(app.window, "Update Stock", "_Update", &g);
    GtkWidget *id    = ui_form_entry(g, 0, "Medicine ID", NULL);
    GtkWidget *qty   = ui_form_entry(g, 1, "Quantity to add", "e.g. 50");
    GtkWidget *batch = ui_form_entry(g, 2, "New batch number", NULL);
    GtkWidget *exp   = ui_form_entry(g, 3, "New expiry date", "YYYY-MM-DD");
    if (preset_id) {
        gtk_entry_set_text(GTK_ENTRY(id), preset_id);
        gtk_widget_grab_focus(qty);
    }
    gtk_widget_show_all(d);

    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        char *s_id = ui_entry_text(id), *s_qty = ui_entry_text(qty),
             *s_batch = ui_entry_text(batch), *s_exp = ui_entry_text(exp);
        char *err = NULL;
        int n = 0;

        if (!ui_parse_int(s_qty, &n))
            err = g_strdup("Quantity to add must be a whole number.");
        else if (store_update_stock(app.store, s_id, n, s_batch, s_exp, &err)) {
            Medicine *m = store_find_medicine(app.store, s_id);
            ui_toast(app.window, "Stock updated: %s now has %d unit(s)", m->name, m->quantity);
            app_refresh_all();
        }
        g_free(s_id); g_free(s_qty); g_free(s_batch); g_free(s_exp);
        if (!err) break;
        ui_error(d, "%s", err);
        g_free(err);
    }
    gtk_widget_destroy(d);
}

/* Shows stock + expiry, then the low-stock / expiry warnings if relevant. */
static void show_stock_and_expiry(GtkWidget *parent, const Medicine *m)
{
    int days = medicine_days_to_expiry(m);
    char *msg = g_strdup_printf("%s (%s)\n\nCurrent stock: %d unit(s)\nBatch: %s\nExpiry date: %s (%s%d day(s)%s)",
                                m->name, m->id, m->quantity, m->batch, m->expiry_date,
                                days < 0 ? "expired " : "", ABS(days), days < 0 ? " ago" : " left");
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(parent), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                                          GTK_BUTTONS_CLOSE, "Stock and Expiry");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", msg);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    g_free(msg);

    if (medicine_is_low_stock(m))
        ui_warning(parent, "Low stock",
                   "%s has only %d unit(s) left, below the re-order level of %d. Please re-order.",
                   m->name, m->quantity, LOW_STOCK_LEVEL);
    if (medicine_is_expired(m))
        ui_warning(parent, "Expired",
                   "%s expired on %s and cannot be sold. Remove it from stock.", m->name, m->expiry_date);
    else if (medicine_is_expiring_soon(m))
        ui_warning(parent, "Expiry warning",
                   "%s expires on %s, in %d day(s).", m->name, m->expiry_date, days);
}

void ui_check_stock_dialog(const char *preset_id)
{
    GtkGrid *g;
    GtkWidget *d = ui_form_dialog(app.window, "Check Stock & Expiry", "_Check", &g);
    GtkWidget *id = ui_form_entry(g, 0, "Medicine ID", NULL);
    if (preset_id) gtk_entry_set_text(GTK_ENTRY(id), preset_id);
    gtk_widget_show_all(d);

    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        char *s_id = ui_entry_text(id);
        Medicine *m = store_find_medicine(app.store, s_id);
        if (!m) {
            ui_error(d, "No medicine with ID '%s'.", s_id);
            g_free(s_id);
            continue;
        }
        g_free(s_id);
        gtk_widget_hide(d);
        show_stock_and_expiry(app.window, m);
        break;
    }
    gtk_widget_destroy(d);
}

void ui_stock_value_dialog(void)
{
    StoreStats st;
    store_stats(app.store, &st);
    char *text = g_strdup_printf(
        "Total stock value (non-expired) : %12.2f\n"
        "Expired stock value (loss)      : %12.2f\n"
        "------------------------------------------------\n"
        "Total inventory at cost         : %12.2f\n\n"
        "%d medicine(s), %ld unit(s); %d expired.",
        st.stock_value, st.expired_loss, st.stock_value + st.expired_loss,
        st.total_medicines, st.total_quantity, st.expired);
    ui_text_popup(app.window, "Stock Value", text);
    g_free(text);
}

gboolean ui_remove_medicine_confirmed(GtkWidget *parent, const char *id)
{
    Medicine *m = store_find_medicine(app.store, id);
    char *err = NULL;
    if (!m) { ui_error(parent, "No medicine with ID '%s'.", id); return FALSE; }

    char name[NAME_LEN], mid[ID_LEN];
    g_strlcpy(name, m->name, sizeof name);
    g_strlcpy(mid, m->id, sizeof mid);
    if (!ui_confirm(parent, "Remove medicine?",
                    "Remove %s (%s) with %d unit(s) in stock? This cannot be undone.",
                    m->name, m->id, m->quantity))
        return FALSE;
    if (!store_remove_medicine(app.store, mid, &err)) {
        ui_error(parent, "%s", err);
        g_free(err);
        return FALSE;
    }
    app_refresh_all();
    ui_toast(app.window, "Medicine %s (%s) removed", name, mid);
    return TRUE;
}

void ui_remove_medicine_dialog(const char *preset_id)
{
    GtkGrid *g;
    GtkWidget *d = ui_form_dialog(app.window, "Remove Medicine", "_Remove", &g);
    GtkWidget *id = ui_form_entry(g, 0, "Medicine ID", NULL);
    if (preset_id) gtk_entry_set_text(GTK_ENTRY(id), preset_id);
    gtk_widget_show_all(d);

    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        char *s_id = ui_entry_text(id);
        if (!store_find_medicine(app.store, s_id)) {
            ui_error(d, "No medicine with ID '%s'.", s_id);
            g_free(s_id);
            continue;
        }
        gtk_widget_hide(d);
        ui_remove_medicine_confirmed(app.window, s_id);
        g_free(s_id);
        break;
    }
    gtk_widget_destroy(d);
}

/* One-click removal of all expired medicines (button on the medicines table). */
void ui_remove_expired(void)
{
    char *err = NULL;
    int n = store_remove_expired(app.store, &err);
    app_refresh_all();
    if (n < 0) {
        ui_error(app.window, "%s", err);
        g_free(err);
    } else {
        ui_toast(app.window, "%d expired medicine(s) removed", n);
    }
}
