/*
 * ui_sales.c - Sell medicine, receipts, discounts.
 */
#include "app.h"

void ui_show_receipt(GtkWidget *parent, const Sale *sale)
{
    char *text = sale_receipt_text(sale);
    ui_text_popup(parent, "Receipt", text);
    g_free(text);
}

/* Show whether the typed medicine needs a prescription while the user types. */
static void on_sell_id_changed(GtkEntry *id, GtkWidget *hint)
{
    Medicine *m = store_find_medicine(app.store, gtk_entry_get_text(id));
    if (!m) { gtk_label_set_text(GTK_LABEL(hint), ""); return; }
    char *t = g_strdup_printf("%s • %d in stock • %.2f each%s%s", m->name, m->quantity, m->unit_price,
                              m->rx_only ? " • PRESCRIPTION REQUIRED" : "",
                              medicine_is_expired(m) ? " • EXPIRED" : "");
    gtk_label_set_text(GTK_LABEL(hint), t);
    g_free(t);
}

gboolean ui_sell_dialog(GtkWidget *parent, const char *preset_id)
{
    GtkGrid *g;
    gboolean sold = FALSE;
    GtkWidget *d = ui_form_dialog(parent, "Sell Medicine", "_Sell", &g);
    GtkWidget *id   = ui_form_entry(g, 0, "Medicine ID", NULL);
    GtkWidget *hint = gtk_label_new("");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "field-name");
    gtk_grid_attach(g, hint, 1, 1, 1, 1);
    GtkWidget *qty  = ui_form_entry(g, 2, "Quantity", "e.g. 2");
    GtkWidget *cust = ui_form_entry(g, 3, "Customer name", NULL);
    GtkWidget *rx   = ui_form_entry(g, 4, "Prescription number", "Required for prescription-only medicines");
    g_signal_connect(id, "changed", G_CALLBACK(on_sell_id_changed), hint);
    if (preset_id) {
        gtk_entry_set_text(GTK_ENTRY(id), preset_id);
        gtk_widget_grab_focus(qty);
    }
    gtk_widget_show_all(d);

    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        char *s_id = ui_entry_text(id), *s_qty = ui_entry_text(qty),
             *s_cust = ui_entry_text(cust), *s_rx = ui_entry_text(rx);
        char *err = NULL;
        Sale *sale = NULL;
        int n = 0;

        if (!ui_parse_int(s_qty, &n))
            err = g_strdup("Quantity must be a whole number.");
        else
            sale = store_sell(app.store, s_id, n, s_cust, s_rx, &err);
        g_free(s_id); g_free(s_qty); g_free(s_cust); g_free(s_rx);

        if (sale) {
            sold = TRUE;
            gtk_widget_hide(d);
            app_refresh_all();
            ui_toast(parent, "Sale #%" G_GINT64_FORMAT " recorded: %.2f", sale->id, sale->final_total);
            ui_show_receipt(parent, sale);
            break;
        }
        ui_error(d, "%s", err);
        g_free(err);
    }
    gtk_widget_destroy(d);
    return sold;
}

void ui_discount_dialog(gint64 sale_id)
{
    Sale *sale = store_find_sale(app.store, sale_id);
    if (!sale) return;

    GtkGrid *g;
    GtkWidget *d = ui_form_dialog(app.window, "Apply Discount", "_Apply", &g);
    char *info = g_strdup_printf("Sale #%" G_GINT64_FORMAT ": %d x %s for %s\nOriginal amount: %.2f",
                                 sale->id, sale->quantity, sale->medicine_name, sale->customer, sale->total);
    GtkWidget *label = gtk_label_new(info);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(g, label, 0, 0, 2, 1);
    g_free(info);

    GtkWidget *pct = ui_form_entry(g, 1, "Discount %", "5 to 15");
    GtkWidget *reason = gtk_combo_box_text_new();
    for (int i = 0; DISCOUNT_REASONS[i]; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(reason), DISCOUNT_REASONS[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(reason), 0);
    ui_form_row(g, 2, "Reason", reason);
    gtk_widget_show_all(d);

    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        char *s_pct = ui_entry_text(pct);
        char *s_reason = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(reason));
        char *err = NULL;
        double p = 0;
        gtk_widget_hide(d);

        if (!ui_parse_double(s_pct, &p)) {
            ui_error(app.window, "Discount must be a number between %.0f and %.0f. The sale was left unchanged.",
                     DISCOUNT_MIN, DISCOUNT_MAX);
        } else if (!store_apply_discount(app.store, sale_id, p, s_reason, &err)) {
            ui_error(app.window, "%s", err);
            g_free(err);
        } else {
            app_refresh_all();
            ui_toast(app.window, "Discount applied to sale #%" G_GINT64_FORMAT, sale->id);
            char *text = g_strdup_printf(
                "Sale #%" G_GINT64_FORMAT "\n\n"
                "Original amount : %10.2f\n"
                "Discount        : %9.1f%%  (%s)\n"
                "You save        : %10.2f\n"
                "------------------------------------\n"
                "Amount to pay   : %10.2f",
                sale->id, sale->total, sale->discount_pct, sale->discount_reason,
                sale->total - sale->final_total, sale->final_total);
            ui_text_popup(app.window, "Discount Applied", text);
            g_free(text);
        }
        g_free(s_pct);
        g_free(s_reason);
    }
    gtk_widget_destroy(d);
}
