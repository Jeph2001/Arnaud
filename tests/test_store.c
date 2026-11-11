/*
 * test_store.c - Headless tests of the business rules (no GUI needed).
 * Run with:  make test
 */
#include "../src/store.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void date_offset(int days, char out[DATE_LEN])
{
    GDate d;
    g_date_clear(&d, 1);
    g_date_set_time_t(&d, time(NULL));
    if (days >= 0) g_date_add_days(&d, days); else g_date_subtract_days(&d, -days);
    g_date_strftime(out, DATE_LEN, "%Y-%m-%d", &d);
}

static Medicine make(const char *id, int qty, double price, int expiry_in_days)
{
    Medicine m = { 0 };
    g_strlcpy(m.id, id, sizeof m.id);
    g_strlcpy(m.name, id, sizeof m.name);
    g_strlcpy(m.batch, "B1", sizeof m.batch);
    m.category = CAT_TABLET;
    m.quantity = qty;
    m.unit_price = price;
    date_offset(-100, m.mfg_date);
    date_offset(expiry_in_days, m.expiry_date);
    return m;
}

/* Expired medicines cannot be added through the API, so age one directly. */
static void force_expire(Store *s, const char *id)
{
    Medicine *m = store_find_medicine(s, id);
    char sql[256];
    date_offset(-5, m->expiry_date);
    g_snprintf(sql, sizeof sql, "UPDATE medicines SET expiry_date='%s' WHERE id='%s';", m->expiry_date, id);
    sqlite3_exec(s->db, sql, NULL, NULL, NULL);
}

int main(void)
{
    char *err = NULL;
    char *path = g_build_filename(g_get_tmp_dir(), "pharmacy-test.db", NULL);
    g_remove(path);

    /* Missing database file must be created, not crash. */
    Store *s = store_open(path, &err);
    CHECK(s != NULL);
    store_set_user(s, "tester");

    /* ---- add medicine validation ---- */
    Medicine m = make("M1", 100, 10.0, 365);
    CHECK(store_add_medicine(s, &m, &err));
    CHECK(!store_add_medicine(s, &m, &err) && strstr(err, "unique")); g_clear_pointer(&err, g_free);
    Medicine bad = make("M2", 0, 10.0, 365);
    CHECK(!store_add_medicine(s, &bad, &err)); g_clear_pointer(&err, g_free);   /* qty 0 */
    bad = make("M2", 5, 0, 365);
    CHECK(!store_add_medicine(s, &bad, &err)); g_clear_pointer(&err, g_free);   /* price 0 */
    bad = make("M2", 5, 1, 0);
    CHECK(!store_add_medicine(s, &bad, &err)); g_clear_pointer(&err, g_free);   /* expiry = today */
    bad = make("M2", 5, 1, 365);
    date_offset(400, bad.mfg_date);
    CHECK(!store_add_medicine(s, &bad, &err)); g_clear_pointer(&err, g_free);   /* expiry < mfg */
    bad = make("M2", 5, 1, 365);
    g_strlcpy(bad.expiry_date, "2026-02-30", sizeof bad.expiry_date);
    CHECK(!store_add_medicine(s, &bad, &err)); g_clear_pointer(&err, g_free);   /* invalid date */

    Medicine rx = make("RX1", 5, 20.0, 20);   /* low stock + expiring soon */
    rx.rx_only = TRUE;
    CHECK(store_add_medicine(s, &rx, &err));
    Medicine *prx = store_find_medicine(s, "RX1");
    CHECK(medicine_is_low_stock(prx) && medicine_is_expiring_soon(prx) && !medicine_is_expired(prx));

    /* ---- selling ---- */
    CHECK(store_sell(s, "M1", 101, "Ann", "", &err) == NULL && strstr(err, "Insufficient stock")); g_clear_pointer(&err, g_free);
    CHECK(store_sell(s, "RX1", 1, "Ann", "", &err) == NULL && strstr(err, "Prescription required")); g_clear_pointer(&err, g_free);
    CHECK(store_sell(s, "M1", 0, "Ann", "", &err) == NULL); g_clear_pointer(&err, g_free);
    CHECK(store_sell(s, "M1", 1, "", "", &err) == NULL); g_clear_pointer(&err, g_free);
    Sale *sale = store_sell(s, "M1", 3, "Ann", "", &err);
    CHECK(sale && sale->total == 30.0 && store_find_medicine(s, "M1")->quantity == 97);
    CHECK(store_sell(s, "RX1", 2, "Bob", "RX-778", &err) != NULL);

    /* ---- discount ---- */
    gint64 sid = sale->id;
    CHECK(!store_apply_discount(s, sid, 20, "Insurance", &err)); g_clear_pointer(&err, g_free);
    CHECK(!store_apply_discount(s, sid, 4.9, "Insurance", &err)); g_clear_pointer(&err, g_free);
    CHECK(store_find_sale(s, sid)->final_total == 30.0);                     /* unchanged */
    CHECK(!store_apply_discount(s, sid, 10, "Because", &err)); g_clear_pointer(&err, g_free);
    CHECK(store_apply_discount(s, sid, 10, "Senior citizen", &err));
    CHECK(store_find_sale(s, sid)->final_total == 27.0);
    CHECK(store_apply_discount(s, sid, 5, "Insurance", &err));               /* replaces, no stacking */
    CHECK(store_find_sale(s, sid)->final_total == 28.5);

    /* ---- update stock ---- */
    char past[DATE_LEN], future[DATE_LEN];
    date_offset(-1, past);
    date_offset(500, future);
    CHECK(!store_update_stock(s, "M1", 0, "B2", future, &err)); g_clear_pointer(&err, g_free);
    CHECK(!store_update_stock(s, "M1", 5, "B2", past, &err)); g_clear_pointer(&err, g_free);
    CHECK(store_update_stock(s, "M1", 3, "B2", future, &err));
    CHECK(store_find_medicine(s, "M1")->quantity == 100);
    CHECK(strcmp(store_find_medicine(s, "M1")->batch, "B2") == 0);

    /* ---- suppliers ---- */
    Supplier *sup = supplier_new();
    g_strlcpy(sup->id, "S1", sizeof sup->id);
    g_strlcpy(sup->name, "MediSupply", sizeof sup->name);
    g_strlcpy(sup->phone, "abc", sizeof sup->phone);
    g_ptr_array_add(sup->medicine_ids, g_strdup("M1"));
    g_ptr_array_add(sup->medicine_ids, g_strdup("RX1"));
    CHECK(!store_add_supplier(s, sup, &err)); g_clear_pointer(&err, g_free); /* bad phone */
    g_strlcpy(sup->phone, "+250 788 123 456", sizeof sup->phone);
    CHECK(store_add_supplier(s, sup, &err));
    supplier_free(sup);

    /* ---- expired handling ---- */
    Medicine e = make("EXP", 4, 50.0, 100);
    CHECK(store_add_medicine(s, &e, &err));
    force_expire(s, "EXP");
    CHECK(store_sell(s, "EXP", 1, "Ann", "", &err) == NULL && strstr(err, "Expired")); g_clear_pointer(&err, g_free);
    StoreStats st;
    store_stats(s, &st);
    CHECK(st.expired == 1 && st.expired_loss == 200.0);
    CHECK(st.stock_value == 100 * 10.0 + 3 * 20.0);
    CHECK(st.sales_today_count == 2);

    char *report = store_build_report(s);
    CHECK(strstr(report, "Best-selling medicine      : M1") != NULL);
    g_free(report);

    CHECK(store_remove_expired(s, &err) == 1);
    CHECK(store_find_medicine(s, "EXP") == NULL);

    /* ---- remove medicine also drops supplier link ---- */
    CHECK(store_remove_medicine(s, "RX1", &err));
    CHECK(store_find_supplier(s, "S1")->medicine_ids->len == 1);

    guint history = s->history->len;
    store_close(s);

    /* ---- persistence: everything is back after re-opening ---- */
    s = store_open(path, &err);
    CHECK(s != NULL);
    CHECK(s->medicines->len == 1 && store_find_medicine(s, "M1")->quantity == 100);
    CHECK(s->sales->len == 2 && store_find_sale(s, sid)->final_total == 28.5);
    CHECK(s->suppliers->len == 1 && store_find_supplier(s, "S1")->medicine_ids->len == 1);
    CHECK(s->history->len == history);
    CHECK(store_remove_supplier(s, "S1", &err));
    store_close(s);
    g_remove(path);
    g_free(path);

    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("All store tests passed (%u history entries recorded).\n", history);
    return 0;
}
