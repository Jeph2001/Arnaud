/*
 * database.c - SQLite implementation of the persistence layer.
 */
#include "database.h"

#include <string.h>

static const char *SCHEMA =
    "PRAGMA foreign_keys = ON;"
    "CREATE TABLE IF NOT EXISTS medicines ("
    "  id          TEXT PRIMARY KEY,"
    "  name        TEXT NOT NULL,"
    "  category    TEXT NOT NULL,"
    "  unit_price  REAL NOT NULL CHECK (unit_price > 0),"
    "  quantity    INTEGER NOT NULL CHECK (quantity >= 0),"
    "  batch       TEXT NOT NULL,"
    "  mfg_date    TEXT NOT NULL,"
    "  expiry_date TEXT NOT NULL,"
    "  rx_only     INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS suppliers ("
    "  id    TEXT PRIMARY KEY,"
    "  name  TEXT NOT NULL,"
    "  phone TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS supplier_medicines ("
    "  supplier_id TEXT NOT NULL REFERENCES suppliers(id) ON DELETE CASCADE,"
    "  medicine_id TEXT NOT NULL REFERENCES medicines(id) ON DELETE CASCADE,"
    "  PRIMARY KEY (supplier_id, medicine_id));"
    "CREATE TABLE IF NOT EXISTS sales ("
    "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  timestamp       TEXT NOT NULL,"
    "  medicine_id     TEXT NOT NULL,"   /* no FK: sales outlive medicines */
    "  medicine_name   TEXT NOT NULL,"
    "  quantity        INTEGER NOT NULL,"
    "  unit_price      REAL NOT NULL,"
    "  total           REAL NOT NULL,"
    "  discount_pct    REAL NOT NULL DEFAULT 0,"
    "  discount_reason TEXT NOT NULL DEFAULT '',"
    "  final_total     REAL NOT NULL,"
    "  customer        TEXT NOT NULL,"
    "  prescription_no TEXT NOT NULL DEFAULT '',"
    "  user            TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS transactions ("
    "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  timestamp   TEXT NOT NULL,"
    "  user        TEXT NOT NULL,"
    "  action      TEXT NOT NULL,"
    "  medicine_id TEXT NOT NULL DEFAULT '',"
    "  details     TEXT NOT NULL DEFAULT '');";

/* Copy a (possibly NULL) text column into a fixed buffer. */
static void col_text(sqlite3_stmt *st, int col, char *dst, gsize len)
{
    const unsigned char *t = sqlite3_column_text(st, col);
    g_strlcpy(dst, t ? (const char *)t : "", len);
}

#define COPY(st, col, field) col_text((st), (col), (field), sizeof(field))

static gboolean exec(sqlite3 *db, const char *sql)
{
    char *msg = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &msg) != SQLITE_OK) {
        g_warning("SQL error: %s", msg ? msg : "?");
        sqlite3_free(msg);
        return FALSE;
    }
    return TRUE;
}

/* Run a prepared statement that returns no rows, then finalize it. */
static gboolean step_done(sqlite3_stmt *st)
{
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}

static sqlite3_stmt *prepare(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        g_warning("prepare failed: %s", sqlite3_errmsg(db));
        return NULL;
    }
    return st;
}

/* ------------------------------------------------------------------ */

sqlite3 *db_open(const char *path, char **err)
{
    sqlite3 *db = NULL;
    /* sqlite3_open_v2 with CREATE makes the file if it does not exist yet,
     * so a first run (or a deleted database) never crashes the program. */
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        *err = g_strdup_printf("Cannot open database '%s': %s", path,
                               db ? sqlite3_errmsg(db) : "out of memory");
        sqlite3_close(db);
        return NULL;
    }
    char *msg = NULL;
    if (sqlite3_exec(db, SCHEMA, NULL, NULL, &msg) != SQLITE_OK) {
        *err = g_strdup_printf("Cannot initialise database '%s': %s", path, msg);
        sqlite3_free(msg);
        sqlite3_close(db);
        return NULL;
    }
    return db;
}

void db_close(sqlite3 *db)          { if (db) sqlite3_close(db); }
gboolean db_begin(sqlite3 *db)      { return exec(db, "BEGIN IMMEDIATE;"); }
gboolean db_commit(sqlite3 *db)     { return exec(db, "COMMIT;"); }
void db_rollback(sqlite3 *db)       { exec(db, "ROLLBACK;"); }
const char *db_last_error(sqlite3 *db) { return sqlite3_errmsg(db); }

/* ----------------------------- loaders ---------------------------- */

gboolean db_load_medicines(sqlite3 *db, GPtrArray *out)
{
    sqlite3_stmt *st = prepare(db,
        "SELECT id,name,category,unit_price,quantity,batch,mfg_date,expiry_date,rx_only "
        "FROM medicines ORDER BY id;");
    if (!st) return FALSE;
    while (sqlite3_step(st) == SQLITE_ROW) {
        Medicine *m = medicine_new();
        char cat[32];
        COPY(st, 0, m->id);
        COPY(st, 1, m->name);
        COPY(st, 2, cat);
        m->category = category_from_string(cat);
        if (m->category == CAT_COUNT) m->category = CAT_TABLET;
        m->unit_price = sqlite3_column_double(st, 3);
        m->quantity   = sqlite3_column_int(st, 4);
        COPY(st, 5, m->batch);
        COPY(st, 6, m->mfg_date);
        COPY(st, 7, m->expiry_date);
        m->rx_only = sqlite3_column_int(st, 8) != 0;
        g_ptr_array_add(out, m);
    }
    sqlite3_finalize(st);
    return TRUE;
}

gboolean db_load_suppliers(sqlite3 *db, GPtrArray *out)
{
    sqlite3_stmt *st = prepare(db, "SELECT id,name,phone FROM suppliers ORDER BY id;");
    sqlite3_stmt *links = prepare(db,
        "SELECT medicine_id FROM supplier_medicines WHERE supplier_id=? ORDER BY medicine_id;");
    if (!st || !links) {
        sqlite3_finalize(st);
        sqlite3_finalize(links);
        return FALSE;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        Supplier *s = supplier_new();
        COPY(st, 0, s->id);
        COPY(st, 1, s->name);
        COPY(st, 2, s->phone);
        sqlite3_bind_text(links, 1, s->id, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(links) == SQLITE_ROW)
            g_ptr_array_add(s->medicine_ids,
                            g_strdup((const char *)sqlite3_column_text(links, 0)));
        sqlite3_reset(links);
        g_ptr_array_add(out, s);
    }
    sqlite3_finalize(links);
    sqlite3_finalize(st);
    return TRUE;
}

gboolean db_load_sales(sqlite3 *db, GPtrArray *out)
{
    sqlite3_stmt *st = prepare(db,
        "SELECT id,timestamp,medicine_id,medicine_name,quantity,unit_price,total,"
        "discount_pct,discount_reason,final_total,customer,prescription_no,user "
        "FROM sales ORDER BY id;");
    if (!st) return FALSE;
    while (sqlite3_step(st) == SQLITE_ROW) {
        Sale *s = sale_new();
        s->id = sqlite3_column_int64(st, 0);
        COPY(st, 1, s->timestamp);
        COPY(st, 2, s->medicine_id);
        COPY(st, 3, s->medicine_name);
        s->quantity     = sqlite3_column_int(st, 4);
        s->unit_price   = sqlite3_column_double(st, 5);
        s->total        = sqlite3_column_double(st, 6);
        s->discount_pct = sqlite3_column_double(st, 7);
        COPY(st, 8, s->discount_reason);
        s->final_total  = sqlite3_column_double(st, 9);
        COPY(st, 10, s->customer);
        COPY(st, 11, s->prescription_no);
        COPY(st, 12, s->user);
        g_ptr_array_add(out, s);
    }
    sqlite3_finalize(st);
    return TRUE;
}

gboolean db_load_history(sqlite3 *db, GPtrArray *out)
{
    sqlite3_stmt *st = prepare(db,
        "SELECT id,timestamp,user,action,medicine_id,details FROM transactions ORDER BY id;");
    if (!st) return FALSE;
    while (sqlite3_step(st) == SQLITE_ROW) {
        Transaction *t = transaction_new();
        t->id = sqlite3_column_int64(st, 0);
        COPY(st, 1, t->timestamp);
        COPY(st, 2, t->user);
        COPY(st, 3, t->action);
        COPY(st, 4, t->medicine_id);
        COPY(st, 5, t->details);
        g_ptr_array_add(out, t);
    }
    sqlite3_finalize(st);
    return TRUE;
}

/* ---------------------------- medicines --------------------------- */

static void bind_medicine(sqlite3_stmt *st, const Medicine *m)
{
    sqlite3_bind_text  (st, 1, m->id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 2, m->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 3, category_to_string(m->category), -1, SQLITE_STATIC);
    sqlite3_bind_double(st, 4, m->unit_price);
    sqlite3_bind_int   (st, 5, m->quantity);
    sqlite3_bind_text  (st, 6, m->batch, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 7, m->mfg_date, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 8, m->expiry_date, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (st, 9, m->rx_only ? 1 : 0);
}

gboolean db_insert_medicine(sqlite3 *db, const Medicine *m)
{
    sqlite3_stmt *st = prepare(db,
        "INSERT INTO medicines (id,name,category,unit_price,quantity,batch,mfg_date,expiry_date,rx_only)"
        " VALUES (?,?,?,?,?,?,?,?,?);");
    if (!st) return FALSE;
    bind_medicine(st, m);
    return step_done(st);
}

gboolean db_update_medicine(sqlite3 *db, const Medicine *m)
{
    sqlite3_stmt *st = prepare(db,
        "UPDATE medicines SET name=?2,category=?3,unit_price=?4,quantity=?5,batch=?6,"
        "mfg_date=?7,expiry_date=?8,rx_only=?9 WHERE id=?1;");
    if (!st) return FALSE;
    bind_medicine(st, m);
    return step_done(st);
}

gboolean db_delete_medicine(sqlite3 *db, const char *id)
{
    sqlite3_stmt *st = prepare(db, "DELETE FROM medicines WHERE id=?;");
    if (!st) return FALSE;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    return step_done(st);
}

/* ---------------------------- suppliers --------------------------- */

gboolean db_insert_supplier(sqlite3 *db, const Supplier *s)
{
    sqlite3_stmt *st = prepare(db, "INSERT INTO suppliers (id,name,phone) VALUES (?,?,?);");
    if (!st) return FALSE;
    sqlite3_bind_text(st, 1, s->id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, s->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, s->phone, -1, SQLITE_TRANSIENT);
    if (!step_done(st)) return FALSE;

    for (guint i = 0; i < s->medicine_ids->len; i++) {
        st = prepare(db, "INSERT INTO supplier_medicines (supplier_id,medicine_id) VALUES (?,?);");
        if (!st) return FALSE;
        sqlite3_bind_text(st, 1, s->id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, g_ptr_array_index(s->medicine_ids, i), -1, SQLITE_TRANSIENT);
        if (!step_done(st)) return FALSE;
    }
    return TRUE;
}

gboolean db_delete_supplier(sqlite3 *db, const char *id)
{
    sqlite3_stmt *st = prepare(db, "DELETE FROM suppliers WHERE id=?;");
    if (!st) return FALSE;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    return step_done(st);
}

/* ------------------------------ sales ----------------------------- */

gboolean db_insert_sale(sqlite3 *db, Sale *s)
{
    sqlite3_stmt *st = prepare(db,
        "INSERT INTO sales (timestamp,medicine_id,medicine_name,quantity,unit_price,total,"
        "discount_pct,discount_reason,final_total,customer,prescription_no,user)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?);");
    if (!st) return FALSE;
    sqlite3_bind_text  (st, 1, s->timestamp, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 2, s->medicine_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 3, s->medicine_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (st, 4, s->quantity);
    sqlite3_bind_double(st, 5, s->unit_price);
    sqlite3_bind_double(st, 6, s->total);
    sqlite3_bind_double(st, 7, s->discount_pct);
    sqlite3_bind_text  (st, 8, s->discount_reason, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 9, s->final_total);
    sqlite3_bind_text  (st, 10, s->customer, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 11, s->prescription_no, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (st, 12, s->user, -1, SQLITE_TRANSIENT);
    if (!step_done(st)) return FALSE;
    s->id = sqlite3_last_insert_rowid(db);
    return TRUE;
}

gboolean db_update_sale_discount(sqlite3 *db, const Sale *s)
{
    sqlite3_stmt *st = prepare(db,
        "UPDATE sales SET discount_pct=?, discount_reason=?, final_total=? WHERE id=?;");
    if (!st) return FALSE;
    sqlite3_bind_double(st, 1, s->discount_pct);
    sqlite3_bind_text  (st, 2, s->discount_reason, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 3, s->final_total);
    sqlite3_bind_int64 (st, 4, s->id);
    return step_done(st);
}

/* --------------------------- transactions ------------------------- */

gboolean db_insert_transaction(sqlite3 *db, Transaction *t)
{
    sqlite3_stmt *st = prepare(db,
        "INSERT INTO transactions (timestamp,user,action,medicine_id,details) VALUES (?,?,?,?,?);");
    if (!st) return FALSE;
    sqlite3_bind_text(st, 1, t->timestamp, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, t->user, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, t->action, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, t->medicine_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, t->details, -1, SQLITE_TRANSIENT);
    if (!step_done(st)) return FALSE;
    t->id = sqlite3_last_insert_rowid(db);
    return TRUE;
}
