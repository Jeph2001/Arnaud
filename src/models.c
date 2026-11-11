/*
 * models.c - "Methods" of the domain classes and date helpers.
 */
#include "models.h"

#include <stdio.h>
#include <string.h>

static const char *CATEGORY_NAMES[CAT_COUNT] = {
    "Tablet", "Syrup", "Injection", "Ointment"
};

const char *category_to_string(MedCategory c)
{
    return (c >= 0 && c < CAT_COUNT) ? CATEGORY_NAMES[c] : "Unknown";
}

MedCategory category_from_string(const char *s)
{
    for (int i = 0; i < CAT_COUNT; i++)
        if (s && g_ascii_strcasecmp(s, CATEGORY_NAMES[i]) == 0)
            return (MedCategory)i;
    return CAT_COUNT;
}

/* ---------------------------- dates ------------------------------- */

gboolean date_parse(const char *s, GDate *out)
{
    int y, m, d;
    char extra;
    if (!s || strlen(s) != 10)
        return FALSE;
    if (sscanf(s, "%4d-%2d-%2d%c", &y, &m, &d, &extra) != 3)
        return FALSE;
    if (!g_date_valid_dmy((GDateDay)d, (GDateMonth)m, (GDateYear)y))
        return FALSE;
    g_date_clear(out, 1);
    g_date_set_dmy(out, (GDateDay)d, (GDateMonth)m, (GDateYear)y);
    return TRUE;
}

int date_days_from_today(const char *s)
{
    GDate d, today;
    if (!date_parse(s, &d))
        return G_MININT;
    g_date_clear(&today, 1);
    g_date_set_time_t(&today, time(NULL));
    return g_date_days_between(&today, &d);
}

void date_today_str(char out[DATE_LEN])
{
    GDateTime *now = g_date_time_new_now_local();
    char *s = g_date_time_format(now, "%Y-%m-%d");
    g_strlcpy(out, s, DATE_LEN);
    g_free(s);
    g_date_time_unref(now);
}

void timestamp_now(char out[STAMP_LEN])
{
    GDateTime *now = g_date_time_new_now_local();
    char *s = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");
    g_strlcpy(out, s, STAMP_LEN);
    g_free(s);
    g_date_time_unref(now);
}

/* ---------------------------- Medicine ---------------------------- */

Medicine *medicine_new(void)            { return g_new0(Medicine, 1); }
Medicine *medicine_copy(const Medicine *m) { return g_memdup2(m, sizeof *m); }
void      medicine_free(gpointer m)     { g_free(m); }

int medicine_days_to_expiry(const Medicine *m)
{
    return date_days_from_today(m->expiry_date);
}

/* A medicine is usable up to and including its expiry date. */
gboolean medicine_is_expired(const Medicine *m)
{
    return medicine_days_to_expiry(m) < 0;
}

gboolean medicine_is_expiring_soon(const Medicine *m)
{
    int d = medicine_days_to_expiry(m);
    return d >= 0 && d <= EXPIRY_WARNING_DAYS;
}

gboolean medicine_is_low_stock(const Medicine *m)
{
    return m->quantity < LOW_STOCK_LEVEL;
}

double medicine_stock_value(const Medicine *m)
{
    return m->quantity * m->unit_price;
}

char *medicine_status_text(const Medicine *m)
{
    GString *s = g_string_new(NULL);
    int days = medicine_days_to_expiry(m);

    if (days < 0)
        g_string_append_printf(s, "EXPIRED %d day(s) ago", -days);
    else if (days <= EXPIRY_WARNING_DAYS)
        g_string_append_printf(s, "Expires in %d day(s)", days);

    if (medicine_is_low_stock(m))
        g_string_append_printf(s, "%sLow stock", s->len ? ", " : "");

    if (s->len == 0)
        g_string_append(s, "OK");
    return g_string_free(s, FALSE);
}

/* ---------------------------- Supplier ---------------------------- */

Supplier *supplier_new(void)
{
    Supplier *s = g_new0(Supplier, 1);
    s->medicine_ids = g_ptr_array_new_with_free_func(g_free);
    return s;
}

void supplier_free(gpointer p)
{
    Supplier *s = p;
    if (!s) return;
    g_ptr_array_unref(s->medicine_ids);
    g_free(s);
}

gboolean supplier_supplies(const Supplier *s, const char *medicine_id)
{
    for (guint i = 0; i < s->medicine_ids->len; i++)
        if (g_strcmp0(g_ptr_array_index(s->medicine_ids, i), medicine_id) == 0)
            return TRUE;
    return FALSE;
}

char *supplier_medicines_text(const Supplier *s)
{
    GString *str = g_string_new(NULL);
    for (guint i = 0; i < s->medicine_ids->len; i++)
        g_string_append_printf(str, "%s%s", i ? ", " : "",
                               (char *)g_ptr_array_index(s->medicine_ids, i));
    return g_string_free(str, FALSE);
}

/* ------------------------------ Sale ------------------------------ */

Sale *sale_new(void)       { return g_new0(Sale, 1); }
void  sale_free(gpointer s) { g_free(s); }

char *sale_receipt_text(const Sale *s)
{
    GString *r = g_string_new(NULL);
    g_string_append(r,   "        PHARMACY RECEIPT\n");
    g_string_append(r,   "----------------------------------\n");
    g_string_append_printf(r, "Receipt #   : %" G_GINT64_FORMAT "\n", s->id);
    g_string_append_printf(r, "Date        : %s\n", s->timestamp);
    g_string_append_printf(r, "Customer    : %s\n", s->customer);
    if (s->prescription_no[0])
        g_string_append_printf(r, "Prescription: %s\n", s->prescription_no);
    g_string_append(r,   "----------------------------------\n");
    g_string_append_printf(r, "%s (%s)\n", s->medicine_name, s->medicine_id);
    g_string_append_printf(r, "  %d x %.2f  = %.2f\n", s->quantity, s->unit_price, s->total);
    if (s->discount_pct > 0)
        g_string_append_printf(r, "Discount    : %.1f%% (%s)  -%.2f\n",
                               s->discount_pct, s->discount_reason,
                               s->total - s->final_total);
    g_string_append(r,   "----------------------------------\n");
    g_string_append_printf(r, "TOTAL       : %.2f\n", s->final_total);
    g_string_append_printf(r, "Served by   : %s\n", s->user);
    return g_string_free(r, FALSE);
}

/* --------------------------- Transaction -------------------------- */

Transaction *transaction_new(void)          { return g_new0(Transaction, 1); }
void         transaction_free(gpointer t)   { g_free(t); }
