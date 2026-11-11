/*
 * database.h - SQLite persistence layer (repository).
 *
 * Only this module talks SQL. Everything else works with the model structs.
 */
#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>
#include "models.h"

sqlite3 *db_open(const char *path, char **err);   /* creates file + schema */
void     db_close(sqlite3 *db);

gboolean db_begin(sqlite3 *db);
gboolean db_commit(sqlite3 *db);
void     db_rollback(sqlite3 *db);

/* Loaders append freshly allocated objects to the given arrays. */
gboolean db_load_medicines(sqlite3 *db, GPtrArray *out);
gboolean db_load_suppliers(sqlite3 *db, GPtrArray *out);
gboolean db_load_sales(sqlite3 *db, GPtrArray *out);
gboolean db_load_history(sqlite3 *db, GPtrArray *out);

gboolean db_insert_medicine(sqlite3 *db, const Medicine *m);
gboolean db_update_medicine(sqlite3 *db, const Medicine *m);
gboolean db_delete_medicine(sqlite3 *db, const char *id);

gboolean db_insert_supplier(sqlite3 *db, const Supplier *s);
gboolean db_delete_supplier(sqlite3 *db, const char *id);

gboolean db_insert_sale(sqlite3 *db, Sale *s);        /* fills s->id */
gboolean db_update_sale_discount(sqlite3 *db, const Sale *s);

gboolean db_insert_transaction(sqlite3 *db, Transaction *t); /* fills t->id */

const char *db_last_error(sqlite3 *db);

#endif /* DATABASE_H */
