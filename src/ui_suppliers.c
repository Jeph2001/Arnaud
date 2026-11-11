/*
 * ui_suppliers.c - Add / remove suppliers.
 */
#include "app.h"

void ui_add_supplier_dialog(void)
{
    if (app.store->medicines->len == 0) {
        ui_error(app.window, "Add at least one medicine before adding a supplier.");
        return;
    }

    GtkGrid *g;
    GtkWidget *d = ui_form_dialog(app.window, "Add Supplier", "_Save", &g);
    GtkWidget *id    = ui_form_entry(g, 0, "Supplier ID", "e.g. SUP01");
    GtkWidget *name  = ui_form_entry(g, 1, "Name", NULL);
    GtkWidget *phone = ui_form_entry(g, 2, "Phone", "e.g. +250 788 000 000");

    /* One check box per medicine. */
    GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GPtrArray *checks = g_ptr_array_new();
    for (guint i = 0; i < app.store->medicines->len; i++) {
        Medicine *m = g_ptr_array_index(app.store->medicines, i);
        char *label = g_strdup_printf("%s - %s", m->id, m->name);
        GtkWidget *cb = gtk_check_button_new_with_label(label);
        g_object_set_data_full(G_OBJECT(cb), "medicine-id", g_strdup(m->id), g_free);
        gtk_box_pack_start(GTK_BOX(list), cb, FALSE, FALSE, 0);
        g_ptr_array_add(checks, cb);
        g_free(label);
    }
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw), 160);
    gtk_container_add(GTK_CONTAINER(sw), list);
    ui_form_row(g, 3, "Medicines supplied", sw);
    gtk_widget_show_all(d);

    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        Supplier *s = supplier_new();
        char *s_id = ui_entry_text(id), *s_name = ui_entry_text(name), *s_phone = ui_entry_text(phone);
        char *err = NULL;
        g_strlcpy(s->id, s_id, sizeof s->id);
        g_strlcpy(s->name, s_name, sizeof s->name);
        g_strlcpy(s->phone, s_phone, sizeof s->phone);
        for (guint i = 0; i < checks->len; i++)
            if (gtk_toggle_button_get_active(g_ptr_array_index(checks, i)))
                g_ptr_array_add(s->medicine_ids,
                                g_strdup(g_object_get_data(g_ptr_array_index(checks, i), "medicine-id")));

        gboolean ok = store_add_supplier(app.store, s, &err);
        if (ok) {
            app_refresh_all();
            ui_toast(app.window, "Supplier %s (%s) added", s->name, s->id);
        }
        supplier_free(s);
        g_free(s_id); g_free(s_name); g_free(s_phone);
        if (ok) break;
        ui_error(d, "%s", err);
        g_free(err);
    }
    g_ptr_array_free(checks, TRUE);
    gtk_widget_destroy(d);
}

void ui_remove_supplier(const char *id)
{
    Supplier *s = store_find_supplier(app.store, id);
    char *err = NULL;
    if (!s) return;

    char name[NAME_LEN], sid[ID_LEN];
    g_strlcpy(name, s->name, sizeof name);
    g_strlcpy(sid, s->id, sizeof sid);
    if (!ui_confirm(app.window, "Remove supplier?", "Remove supplier %s (%s)?", s->name, s->id))
        return;
    if (!store_remove_supplier(app.store, sid, &err)) {
        ui_error(app.window, "%s", err);
        g_free(err);
        return;
    }
    app_refresh_all();
    ui_toast(app.window, "Supplier %s removed", name);
}
