/*
 * store.h - Service layer: in-memory lists + business rules.
 *
 * The UI calls these functions only. Each mutating call validates its input,
 * writes to SQLite inside a transaction (together with a history entry), and
 * updates the in-memory lists only if the database write succeeded.
 *
 * Every function that can fail returns FALSE/NULL and sets *err to a
 * human-readable message (free it with g_free()).
 */
#ifndef STORE_H
#define STORE_H

#include <sqlite3.h>
#include "models.h"

typedef struct {
    sqlite3   *db;
    char       db_path[512];
    char       user[USER_LEN];      /* operator performing the actions */
    GPtrArray *medicines;           /* Medicine*    */
    GPtrArray *suppliers;           /* Supplier*    */
    GPtrArray *sales;               /* Sale*        */
    GPtrArray *history;             /* Transaction* (chronological) */
} Store;

typedef struct {
    int    total_medicines;
    long   total_quantity;
    double stock_value;             /* non-expired stock  */
    double expired_loss;            /* expired stock      */
    double sales_total;             /* all-time, after discounts */
    double sales_today_total;
    int    sales_today_count;
    int    sales_count;
    int    low_stock;
    int    expiring_soon;
    int    expired;
} StoreStats;

Store    *store_open(const char *db_path, char **err);
void      store_close(Store *s);
void      store_set_user(Store *s, const char *user);

Medicine *store_find_medicine(Store *s, const char *id);
Supplier *store_find_supplier(Store *s, const char *id);
Sale     *store_find_sale(Store *s, gint64 id);

gboolean  store_add_medicine(Store *s, const Medicine *m, char **err);
gboolean  store_update_stock(Store *s, const char *id, int add_qty,
                             const char *new_batch, const char *new_expiry, char **err);
Sale     *store_sell(Store *s, const char *id, int qty, const char *customer,
                     const char *prescription_no, char **err);
gboolean  store_apply_discount(Store *s, gint64 sale_id, double pct,
                               const char *reason, char **err);
gboolean  store_remove_medicine(Store *s, const char *id, char **err);
int       store_remove_expired(Store *s, char **err);   /* returns count, -1 on error */

gboolean  store_add_supplier(Store *s, const Supplier *sup, char **err);
gboolean  store_remove_supplier(Store *s, const char *id, char **err);

void      store_stats(Store *s, StoreStats *out);
char     *store_build_report(Store *s);                  /* g_free() result */

extern const char *DISCOUNT_REASONS[];                   /* NULL-terminated */

#endif /* STORE_H */
