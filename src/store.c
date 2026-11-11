/*
 * store.c - Business rules of the pharmacy.
 */
#include "store.h"
#include "database.h"

#include <stdarg.h>
#include <string.h>

const char *DISCOUNT_REASONS[] = { "Senior citizen", "Insurance", "Bulk purchase", NULL };

static gboolean fail(char **err, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
static gboolean fail(char **err, const char *fmt, ...)
{
    if (err) {
        va_list ap;
        va_start(ap, fmt);
        *err = g_strdup_vprintf(fmt, ap);
        va_end(ap);
    }
    return FALSE;
}

/* Is `s` empty or only whitespace? */
static gboolean blank(const char *s)
{
    if (!s) return TRUE;
    while (*s) if (!g_ascii_isspace(*s++)) return FALSE;
    return TRUE;
}

/* Write a history entry inside the current DB transaction; the in-memory copy
 * is returned so the caller can append it after COMMIT succeeds. */
static Transaction *log_tx(Store *s, const char *action, const char *med_id,
                           const char *fmt, ...) G_GNUC_PRINTF(4, 5);
static Transaction *log_tx(Store *s, const char *action, const char *med_id,
                           const char *fmt, ...)
{
    Transaction *t = transaction_new();
    va_list ap;
    timestamp_now(t->timestamp);
    g_strlcpy(t->user, s->user, sizeof t->user);
    g_strlcpy(t->action, action, sizeof t->action);
    g_strlcpy(t->medicine_id, med_id ? med_id : "", sizeof t->medicine_id);
    va_start(ap, fmt);
    g_vsnprintf(t->details, sizeof t->details, fmt, ap);
    va_end(ap);
    if (!db_insert_transaction(s->db, t)) {
        transaction_free(t);
        return NULL;
    }
    return t;
}

/* Commit, or roll back and report. On success the history entry is kept. */
static gboolean finish(Store *s, gboolean ok, Transaction *t, char **err)
{
    if (ok && t && db_commit(s->db)) {
        g_ptr_array_add(s->history, t);
        return TRUE;
    }
    const char *msg = db_last_error(s->db);
    db_rollback(s->db);
    if (t) transaction_free(t);
    return fail(err, "Database error: %s", msg);
}

/* ------------------------------------------------------------------ */

Store *store_open(const char *db_path, char **err)
{
    sqlite3 *db = db_open(db_path, err);
    if (!db) return NULL;

    Store *s = g_new0(Store, 1);
    s->db = db;
    g_strlcpy(s->db_path, db_path, sizeof s->db_path);
    g_strlcpy(s->user, "unknown", sizeof s->user);
    s->medicines = g_ptr_array_new_with_free_func(medicine_free);
    s->suppliers = g_ptr_array_new_with_free_func(supplier_free);
    s->sales     = g_ptr_array_new_with_free_func(sale_free);
    s->history   = g_ptr_array_new_with_free_func(transaction_free);

    if (!db_load_medicines(db, s->medicines) || !db_load_suppliers(db, s->suppliers) ||
        !db_load_sales(db, s->sales) || !db_load_history(db, s->history)) {
        *err = g_strdup_printf("Cannot read data from '%s': %s", db_path, db_last_error(db));
        store_close(s);
        return NULL;
    }
    return s;
}

void store_close(Store *s)
{
    if (!s) return;
    db_close(s->db);
    g_ptr_array_unref(s->medicines);
    g_ptr_array_unref(s->suppliers);
    g_ptr_array_unref(s->sales);
    g_ptr_array_unref(s->history);
    g_free(s);
}

void store_set_user(Store *s, const char *user)
{
    g_strlcpy(s->user, user, sizeof s->user);
}

Medicine *store_find_medicine(Store *s, const char *id)
{
    for (guint i = 0; id && i < s->medicines->len; i++) {
        Medicine *m = g_ptr_array_index(s->medicines, i);
        if (g_ascii_strcasecmp(m->id, id) == 0) return m;
    }
    return NULL;
}

Supplier *store_find_supplier(Store *s, const char *id)
{
    for (guint i = 0; id && i < s->suppliers->len; i++) {
        Supplier *sup = g_ptr_array_index(s->suppliers, i);
        if (g_ascii_strcasecmp(sup->id, id) == 0) return sup;
    }
    return NULL;
}

Sale *store_find_sale(Store *s, gint64 id)
{
    for (guint i = 0; i < s->sales->len; i++) {
        Sale *sale = g_ptr_array_index(s->sales, i);
        if (sale->id == id) return sale;
    }
    return NULL;
}

/* --------------------------- medicines ---------------------------- */

gboolean store_add_medicine(Store *s, const Medicine *m, char **err)
{
    GDate mfg, exp;

    if (blank(m->id))                       return fail(err, "Medicine ID is required.");
    if (store_find_medicine(s, m->id))      return fail(err, "Medicine ID '%s' already exists. IDs must be unique.", m->id);
    if (blank(m->name))                     return fail(err, "Medicine name is required.");
    if (m->category >= CAT_COUNT)           return fail(err, "Choose a category.");
    if (m->unit_price <= 0)                 return fail(err, "Unit price must be greater than 0.");
    if (m->quantity <= 0)                   return fail(err, "Quantity must be greater than 0.");
    if (blank(m->batch))                    return fail(err, "Batch number is required.");
    if (!date_parse(m->mfg_date, &mfg))     return fail(err, "Manufacture date must be a valid date (YYYY-MM-DD).");
    if (!date_parse(m->expiry_date, &exp))  return fail(err, "Expiry date must be a valid date (YYYY-MM-DD).");
    if (g_date_compare(&exp, &mfg) <= 0)    return fail(err, "Expiry date must be later than the manufacture date.");
    if (date_days_from_today(m->mfg_date) > 0) return fail(err, "Manufacture date cannot be in the future.");
    if (date_days_from_today(m->expiry_date) <= 0) return fail(err, "Expiry date must be later than today.");

    db_begin(s->db);
    gboolean ok = db_insert_medicine(s->db, m);
    Transaction *t = ok ? log_tx(s, "ADD", m->id, "Added %s (%s), qty %d @ %.2f, batch %s, expires %s%s",
                                 m->name, category_to_string(m->category), m->quantity, m->unit_price,
                                 m->batch, m->expiry_date, m->rx_only ? ", prescription only" : "")
                        : NULL;
    if (!finish(s, ok, t, err)) return FALSE;
    g_ptr_array_add(s->medicines, medicine_copy(m));
    return TRUE;
}

gboolean store_update_stock(Store *s, const char *id, int add_qty,
                            const char *new_batch, const char *new_expiry, char **err)
{
    GDate exp;
    Medicine *m = store_find_medicine(s, id);

    if (!m)                                 return fail(err, "No medicine with ID '%s'.", id ? id : "");
    if (add_qty <= 0)                       return fail(err, "Quantity to add must be greater than 0.");
    if (blank(new_batch))                   return fail(err, "New batch number is required.");
    if (!date_parse(new_expiry, &exp))      return fail(err, "New expiry date must be a valid date (YYYY-MM-DD).");
    if (date_days_from_today(new_expiry) < 0) return fail(err, "New expiry date cannot be in the past.");
    if ((gint64)m->quantity + add_qty > G_MAXINT) return fail(err, "Quantity is too large.");

    Medicine updated = *m;
    updated.quantity += add_qty;
    g_strlcpy(updated.batch, new_batch, sizeof updated.batch);
    g_strlcpy(updated.expiry_date, new_expiry, sizeof updated.expiry_date);

    db_begin(s->db);
    gboolean ok = db_update_medicine(s->db, &updated);
    Transaction *t = ok ? log_tx(s, "UPDATE", m->id, "Added %d unit(s) (now %d), batch %s -> %s, expiry %s -> %s",
                                 add_qty, updated.quantity, m->batch, new_batch, m->expiry_date, new_expiry)
                        : NULL;
    if (!finish(s, ok, t, err)) return FALSE;
    *m = updated;
    return TRUE;
}

Sale *store_sell(Store *s, const char *id, int qty, const char *customer,
                 const char *prescription_no, char **err)
{
    Medicine *m = store_find_medicine(s, id);

    if (!m)                         { fail(err, "No medicine with ID '%s'.", id ? id : ""); return NULL; }
    if (medicine_is_expired(m))     { fail(err, "Expired medicine cannot be sold."); return NULL; }
    if (qty <= 0)                   { fail(err, "Quantity to sell must be greater than 0."); return NULL; }
    if (qty > m->quantity)          { fail(err, "Insufficient stock: only %d unit(s) of %s available.", m->quantity, m->name); return NULL; }
    if (blank(customer))            { fail(err, "Customer name is required."); return NULL; }
    if (m->rx_only && blank(prescription_no)) {
        fail(err, "Prescription required: %s is a prescription-only medicine.", m->name);
        return NULL;
    }

    Sale *sale = sale_new();
    timestamp_now(sale->timestamp);
    g_strlcpy(sale->medicine_id, m->id, sizeof sale->medicine_id);
    g_strlcpy(sale->medicine_name, m->name, sizeof sale->medicine_name);
    sale->quantity    = qty;
    sale->unit_price  = m->unit_price;
    sale->total       = qty * m->unit_price;
    sale->final_total = sale->total;
    g_strlcpy(sale->customer, customer, sizeof sale->customer);
    g_strlcpy(sale->prescription_no, prescription_no ? prescription_no : "", sizeof sale->prescription_no);
    g_strlcpy(sale->user, s->user, sizeof sale->user);

    Medicine updated = *m;
    updated.quantity -= qty;

    db_begin(s->db);
    gboolean ok = db_update_medicine(s->db, &updated) && db_insert_sale(s->db, sale);
    Transaction *t = ok ? log_tx(s, "SELL", m->id, "Sale #%" G_GINT64_FORMAT ": %d x %s to %s = %.2f%s%s",
                                 sale->id, qty, m->name, sale->customer, sale->total,
                                 sale->prescription_no[0] ? ", Rx " : "", sale->prescription_no)
                        : NULL;
    if (!finish(s, ok, t, err)) {
        sale_free(sale);
        return NULL;
    }
    *m = updated;
    g_ptr_array_add(s->sales, sale);
    return sale;
}

gboolean store_apply_discount(Store *s, gint64 sale_id, double pct, const char *reason, char **err)
{
    Sale *sale = store_find_sale(s, sale_id);
    gboolean reason_ok = FALSE;

    for (int i = 0; DISCOUNT_REASONS[i]; i++)
        if (g_strcmp0(reason, DISCOUNT_REASONS[i]) == 0) reason_ok = TRUE;

    if (!sale)        return fail(err, "Select a sale first.");
    if (pct < DISCOUNT_MIN || pct > DISCOUNT_MAX)
        return fail(err, "Discount must be between %.0f%% and %.0f%%. The sale was left unchanged.",
                    DISCOUNT_MIN, DISCOUNT_MAX);
    if (!reason_ok)   return fail(err, "Choose a discount reason. The sale was left unchanged.");

    Sale updated = *sale;
    updated.discount_pct = pct;
    g_strlcpy(updated.discount_reason, reason, sizeof updated.discount_reason);
    /* The discount is always taken from the original total, so re-applying
     * replaces the previous discount instead of stacking it. */
    updated.final_total = sale->total * (1.0 - pct / 100.0);

    db_begin(s->db);
    gboolean ok = db_update_sale_discount(s->db, &updated);
    Transaction *t = ok ? log_tx(s, "DISCOUNT", sale->medicine_id,
                                 "Sale #%" G_GINT64_FORMAT ": %.1f%% (%s), %.2f -> %.2f",
                                 sale->id, pct, reason, sale->total, updated.final_total)
                        : NULL;
    if (!finish(s, ok, t, err)) return FALSE;
    *sale = updated;
    return TRUE;
}

/* Remove a medicine from memory, including supplier links (the DB cascades). */
static void forget_medicine(Store *s, Medicine *m)
{
    for (guint i = 0; i < s->suppliers->len; i++) {
        Supplier *sup = g_ptr_array_index(s->suppliers, i);
        for (guint j = sup->medicine_ids->len; j-- > 0;)
            if (g_strcmp0(g_ptr_array_index(sup->medicine_ids, j), m->id) == 0)
                g_ptr_array_remove_index(sup->medicine_ids, j);
    }
    g_ptr_array_remove(s->medicines, m);   /* frees m */
}

gboolean store_remove_medicine(Store *s, const char *id, char **err)
{
    Medicine *m = store_find_medicine(s, id);
    if (!m) return fail(err, "No medicine with ID '%s'.", id ? id : "");

    db_begin(s->db);
    gboolean ok = db_delete_medicine(s->db, m->id);
    Transaction *t = ok ? log_tx(s, "REMOVE", m->id, "Removed %s (qty %d, batch %s, expiry %s)",
                                 m->name, m->quantity, m->batch, m->expiry_date)
                        : NULL;
    if (!finish(s, ok, t, err)) return FALSE;
    forget_medicine(s, m);
    return TRUE;
}

int store_remove_expired(Store *s, char **err)
{
    int count = 0;
    for (guint i = s->medicines->len; i-- > 0;) {
        Medicine *m = g_ptr_array_index(s->medicines, i);
        if (!medicine_is_expired(m)) continue;
        char id[ID_LEN];
        g_strlcpy(id, m->id, sizeof id);
        if (!store_remove_medicine(s, id, err)) return -1;
        count++;
    }
    return count;
}

/* --------------------------- suppliers ---------------------------- */

gboolean store_add_supplier(Store *s, const Supplier *sup, char **err)
{
    if (blank(sup->id))                   return fail(err, "Supplier ID is required.");
    if (store_find_supplier(s, sup->id))  return fail(err, "Supplier ID '%s' already exists.", sup->id);
    if (blank(sup->name))                 return fail(err, "Supplier name is required.");
    if (blank(sup->phone))                return fail(err, "Phone number is required.");
    for (const char *p = sup->phone; *p; p++)
        if (!g_ascii_isdigit(*p) && !strchr("+-() ", *p))
            return fail(err, "Phone number may only contain digits, spaces and + - ( ).");
    if (sup->medicine_ids->len == 0)      return fail(err, "Select at least one medicine this supplier provides.");

    char *meds = supplier_medicines_text(sup);
    db_begin(s->db);
    gboolean ok = db_insert_supplier(s->db, sup);
    Transaction *t = ok ? log_tx(s, "SUPPLIER+", NULL, "Added supplier %s (%s, %s) supplying %s",
                                 sup->id, sup->name, sup->phone, meds)
                        : NULL;
    g_free(meds);
    if (!finish(s, ok, t, err)) return FALSE;

    Supplier *copy = supplier_new();
    g_strlcpy(copy->id, sup->id, sizeof copy->id);
    g_strlcpy(copy->name, sup->name, sizeof copy->name);
    g_strlcpy(copy->phone, sup->phone, sizeof copy->phone);
    for (guint i = 0; i < sup->medicine_ids->len; i++)
        g_ptr_array_add(copy->medicine_ids, g_strdup(g_ptr_array_index(sup->medicine_ids, i)));
    g_ptr_array_add(s->suppliers, copy);
    return TRUE;
}

gboolean store_remove_supplier(Store *s, const char *id, char **err)
{
    Supplier *sup = store_find_supplier(s, id);
    if (!sup) return fail(err, "No supplier with ID '%s'.", id ? id : "");

    db_begin(s->db);
    gboolean ok = db_delete_supplier(s->db, sup->id);
    Transaction *t = ok ? log_tx(s, "SUPPLIER-", NULL, "Removed supplier %s (%s)", sup->id, sup->name) : NULL;
    if (!finish(s, ok, t, err)) return FALSE;
    g_ptr_array_remove(s->suppliers, sup);
    return TRUE;
}

/* ----------------------------- reports ---------------------------- */

void store_stats(Store *s, StoreStats *o)
{
    char today[DATE_LEN];
    date_today_str(today);
    memset(o, 0, sizeof *o);

    o->total_medicines = (int)s->medicines->len;
    for (guint i = 0; i < s->medicines->len; i++) {
        Medicine *m = g_ptr_array_index(s->medicines, i);
        o->total_quantity += m->quantity;
        if (medicine_is_expired(m)) {
            o->expired_loss += medicine_stock_value(m);
            o->expired++;
        } else {
            o->stock_value += medicine_stock_value(m);
            if (medicine_is_expiring_soon(m)) o->expiring_soon++;
        }
        if (medicine_is_low_stock(m)) o->low_stock++;
    }
    o->sales_count = (int)s->sales->len;
    for (guint i = 0; i < s->sales->len; i++) {
        Sale *sale = g_ptr_array_index(s->sales, i);
        o->sales_total += sale->final_total;
        if (strncmp(sale->timestamp, today, 10) == 0) {
            o->sales_today_total += sale->final_total;
            o->sales_today_count++;
        }
    }
}

char *store_build_report(Store *s)
{
    StoreStats st;
    char stamp[STAMP_LEN];
    GString *r = g_string_new(NULL);
    store_stats(s, &st);
    timestamp_now(stamp);

    g_string_append(r, "==================================================\n");
    g_string_append(r, "        PHARMACY MANAGEMENT SYSTEM - REPORT\n");
    g_string_append(r, "==================================================\n");
    g_string_append_printf(r, "Generated : %s\nBy        : %s\n\n", stamp, s->user);

    g_string_append(r, "STOCK\n");
    g_string_append_printf(r, "  Total medicines            : %d\n", st.total_medicines);
    g_string_append_printf(r, "  Total quantity in stock    : %ld\n", st.total_quantity);
    g_string_append_printf(r, "  Stock value (non-expired)  : %.2f\n", st.stock_value);
    g_string_append_printf(r, "  Expired stock (loss)       : %.2f\n\n", st.expired_loss);

    g_string_append(r, "SALES\n");
    g_string_append_printf(r, "  Total sales amount (all)   : %.2f  (%d sale(s))\n", st.sales_total, st.sales_count);
    g_string_append_printf(r, "  Sales today                : %d  (amount %.2f)\n", st.sales_today_count, st.sales_today_total);

    /* Best seller: medicine with the most units sold (all time). */
    GHashTable *units = g_hash_table_new(g_str_hash, g_str_equal);
    const char *best_id = NULL, *best_name = NULL;
    int best_units = 0;
    for (guint i = 0; i < s->sales->len; i++) {
        Sale *sale = g_ptr_array_index(s->sales, i);
        int u = GPOINTER_TO_INT(g_hash_table_lookup(units, sale->medicine_id)) + sale->quantity;
        g_hash_table_insert(units, sale->medicine_id, GINT_TO_POINTER(u));
        if (u > best_units) { best_units = u; best_id = sale->medicine_id; best_name = sale->medicine_name; }
    }
    g_hash_table_destroy(units);
    if (best_id)
        g_string_append_printf(r, "  Best-selling medicine      : %s (%s), %d unit(s) sold\n\n", best_name, best_id, best_units);
    else
        g_string_append(r, "  Best-selling medicine      : - (no sales yet)\n\n");

    g_string_append_printf(r, "BELOW RE-ORDER LEVEL (< %d units)\n", LOW_STOCK_LEVEL);
    int n = 0;
    for (guint i = 0; i < s->medicines->len; i++) {
        Medicine *m = g_ptr_array_index(s->medicines, i);
        if (medicine_is_low_stock(m)) {
            g_string_append_printf(r, "  %-10s %-28s qty %d\n", m->id, m->name, m->quantity);
            n++;
        }
    }
    if (!n) g_string_append(r, "  (none)\n");

    g_string_append_printf(r, "\nEXPIRING WITHIN %d DAYS\n", EXPIRY_WARNING_DAYS);
    n = 0;
    for (guint i = 0; i < s->medicines->len; i++) {
        Medicine *m = g_ptr_array_index(s->medicines, i);
        if (medicine_is_expiring_soon(m)) {
            g_string_append_printf(r, "  %-10s %-28s expires %s (%d day(s))\n", m->id, m->name,
                                   m->expiry_date, medicine_days_to_expiry(m));
            n++;
        }
    }
    if (!n) g_string_append(r, "  (none)\n");

    g_string_append(r, "\nEXPIRED (cannot be sold)\n");
    n = 0;
    for (guint i = 0; i < s->medicines->len; i++) {
        Medicine *m = g_ptr_array_index(s->medicines, i);
        if (medicine_is_expired(m)) {
            g_string_append_printf(r, "  %-10s %-28s expired %s, loss %.2f\n", m->id, m->name,
                                   m->expiry_date, medicine_stock_value(m));
            n++;
        }
    }
    if (!n) g_string_append(r, "  (none)\n");
    return g_string_free(r, FALSE);
}
