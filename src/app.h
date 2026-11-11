/*
 * app.h - Shared UI state and the entry points of each UI module.
 */
#ifndef APP_H
#define APP_H

#include <gtk/gtk.h>
#include "store.h"

#define PHARMACY_GREEN "#00843D"

typedef struct {
    Store        *store;
    GtkWidget    *window;

    /* Medicines tab */
    GtkListStore *med_list;
    GtkTreeModel *med_filter;
    GtkWidget    *med_view;
    GtkWidget    *med_search;
    GtkWidget    *remove_expired_btn;

    /* Sales tab */
    GtkListStore *sales_list;
    GtkWidget    *sales_view;

    /* Suppliers tab */
    GtkListStore *sup_list;
    GtkWidget    *sup_view;

    /* History tab */
    GtkListStore *hist_list;
    GtkTreeModel *hist_filter;
    GtkWidget    *hist_view;
    GtkWidget    *hist_med_entry;
    GtkWidget    *hist_date_entry;

    GtkWidget    *summary_label;
    GList        *details_windows;   /* open DetailsWindow* */
} App;

extern App app;

/* ui_common.c - notifications and form helpers */
void       ui_install_css(void);
GtkWidget *ui_toast_host(GtkWidget *window, GtkWidget *content);
void       ui_toast(GtkWidget *near, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
void       ui_error(GtkWidget *parent, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
void       ui_warning(GtkWidget *parent, const char *title, const char *fmt, ...) G_GNUC_PRINTF(3, 4);
void       ui_text_popup(GtkWidget *parent, const char *title, const char *text);
gboolean   ui_confirm(GtkWidget *parent, const char *title, const char *fmt, ...) G_GNUC_PRINTF(3, 4);
GtkWidget *ui_form_dialog(GtkWidget *parent, const char *title, const char *ok_label, GtkGrid **grid);
GtkWidget *ui_form_entry(GtkGrid *grid, int row, const char *label, const char *placeholder);
void       ui_form_row(GtkGrid *grid, int row, const char *label, GtkWidget *w);
char      *ui_entry_text(GtkWidget *entry);          /* stripped copy, g_free() */
gboolean   ui_parse_int(const char *s, int *out);
gboolean   ui_parse_double(const char *s, double *out);
GtkWidget *ui_button(const char *label, const char *css_class, GCallback cb, gpointer data);

/* ui_main.c */
GtkWidget *ui_main_window_new(GtkApplication *gapp);
void       app_refresh_all(void);
const char *ui_selected_medicine_id(void);           /* static buffer or NULL */

/* ui_medicine.c - medicine dialogs */
void ui_add_medicine_dialog(void);
void ui_update_stock_dialog(const char *preset_id);
void ui_check_stock_dialog(const char *preset_id);
void ui_stock_value_dialog(void);
void ui_remove_medicine_dialog(const char *preset_id);
gboolean ui_remove_medicine_confirmed(GtkWidget *parent, const char *id);
void ui_remove_expired(void);

/* ui_sales.c */
gboolean ui_sell_dialog(GtkWidget *parent, const char *preset_id);
void     ui_discount_dialog(gint64 sale_id);
void     ui_show_receipt(GtkWidget *parent, const Sale *sale);

/* ui_details.c */
void ui_details_window_open(const char *medicine_id);
void ui_details_refresh_all(void);

/* ui_suppliers.c */
void ui_add_supplier_dialog(void);
void ui_remove_supplier(const char *id);

/* ui_report.c */
void ui_report_window(void);

#endif /* APP_H */
