/*
 * ui_report.c - Summary report window with export to a text file.
 */
#include "app.h"

#include <errno.h>
#include <string.h>

static void on_export(GtkWidget *window)
{
    const char *text = g_object_get_data(G_OBJECT(window), "report");
    GtkWidget *chooser = gtk_file_chooser_dialog_new("Export report", GTK_WINDOW(window),
                                                     GTK_FILE_CHOOSER_ACTION_SAVE,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_Export", GTK_RESPONSE_ACCEPT, NULL);
    char today[DATE_LEN], *suggested;
    date_today_str(today);
    suggested = g_strdup_printf("pharmacy-report-%s.txt", today);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), suggested);
    g_free(suggested);

    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        GError *error = NULL;
        gtk_widget_hide(chooser);
        if (g_file_set_contents(path, text, -1, &error)) {
            ui_toast(window, "Report exported to %s", path);
        } else {
            ui_error(window, "Could not export the report: %s", error->message);
            g_error_free(error);
        }
        g_free(path);
    }
    gtk_widget_destroy(chooser);
}

void ui_report_window(void)
{
    char *report = store_build_report(app.store);

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Pharmacy Report");
    gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(app.window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), 720, 640);
    g_object_set_data_full(G_OBJECT(win), "report", report, g_free);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    g_object_set(box, "margin", 12, NULL);

    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), report, -1);
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    gtk_box_pack_start(GTK_BOX(box), sw, TRUE, TRUE, 0);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(buttons), ui_button("_Export to text file...", "primary", G_CALLBACK(on_export), win), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(buttons), ui_button("_Close", NULL, G_CALLBACK(gtk_widget_destroy), win), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(win), ui_toast_host(win, box));
    gtk_widget_show_all(win);
}
