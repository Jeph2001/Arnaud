/*
 * main.c - Program entry point.
 *
 * Start-up: open (or create) the SQLite database, ask who is operating the
 * system (recorded in the transaction history), then show the main window.
 */
#include "app.h"

#include <glib/gstdio.h>

/* Database location: $PHARMACY_DB if set, otherwise the per-user data folder
 * (%LOCALAPPDATA%\PharmacyMS on Windows, ~/.local/share/PharmacyMS on Linux). */
static char *database_path(void)
{
    const char *env = g_getenv("PHARMACY_DB");
    if (env && *env) return g_strdup(env);
    char *dir = g_build_filename(g_get_user_data_dir(), "PharmacyMS", NULL);
    g_mkdir_with_parents(dir, 0700);
    char *path = g_build_filename(dir, "pharmacy.db", NULL);
    g_free(dir);
    return path;
}

static char *ask_operator_name(void)
{
    GtkGrid *g;
    GtkWidget *d = ui_form_dialog(NULL, "Pharmacy Management System - Sign in", "_Continue", &g);
    GtkWidget *intro = gtk_label_new("Enter your name. It is recorded with every operation you perform.");
    gtk_grid_attach(g, intro, 0, 0, 2, 1);
    GtkWidget *name = ui_form_entry(g, 1, "Pharmacist", NULL);
    const char *guess = g_get_real_name();
    if (!guess || g_strcmp0(guess, "Unknown") == 0) guess = g_get_user_name();
    gtk_entry_set_text(GTK_ENTRY(name), guess ? guess : "");
    gtk_window_set_position(GTK_WINDOW(d), GTK_WIN_POS_CENTER);
    gtk_widget_show_all(d);

    char *result = NULL;
    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        result = ui_entry_text(name);
        if (*result) break;
        g_free(result);
        result = NULL;
        ui_error(d, "Please enter your name.");
    }
    gtk_widget_destroy(d);
    return result;
}

static void on_activate(GtkApplication *gapp, gpointer data)
{
    (void)data;
    if (app.window) {                       /* second launch: reuse window */
        gtk_window_present(GTK_WINDOW(app.window));
        return;
    }
    ui_install_css();

    char *err = NULL, *path = database_path();
    app.store = store_open(path, &err);
    g_free(path);
    if (!app.store) {
        ui_error(NULL, "%s", err);
        g_free(err);
        return;                             /* no window -> application exits */
    }

    /* $PHARMACY_USER skips the sign-in prompt (kiosk / scripted use). */
    const char *env_user = g_getenv("PHARMACY_USER");
    char *user = (env_user && *env_user) ? g_strdup(env_user) : ask_operator_name();
    if (!user) return;
    store_set_user(app.store, user);
    g_free(user);

    GtkWidget *win = ui_main_window_new(gapp);
    gtk_widget_show_all(win);
    ui_toast(win, "Welcome, %s", app.store->user);
}

int main(int argc, char **argv)
{
    GtkApplication *gapp = gtk_application_new("rw.pharmacy.PharmacyMS", 0);
    g_signal_connect(gapp, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(gapp), argc, argv);
    g_object_unref(gapp);
    store_close(app.store);                 /* SQLite already holds every change */
    return status;
}
