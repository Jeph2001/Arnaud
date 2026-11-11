/*
 * models.h - Domain "classes" of the Pharmacy Management System.
 *
 * C has no classes, so each entity is a struct plus a set of functions that
 * take a pointer to it as their first argument (the C equivalent of methods).
 */
#ifndef MODELS_H
#define MODELS_H

#include <glib.h>

#define ID_LEN      32
#define NAME_LEN    96
#define BATCH_LEN   48
#define DATE_LEN    11   /* "YYYY-MM-DD" + NUL          */
#define STAMP_LEN   20   /* "YYYY-MM-DD HH:MM:SS" + NUL */
#define USER_LEN    64

#define LOW_STOCK_LEVEL      10   /* re-order level              */
#define EXPIRY_WARNING_DAYS  30   /* "expires soon" window        */
#define DISCOUNT_MIN          5.0
#define DISCOUNT_MAX         15.0

/* ------------------------------------------------------------------ */
/* Category                                                            */
/* ------------------------------------------------------------------ */
typedef enum {
    CAT_TABLET = 0,
    CAT_SYRUP,
    CAT_INJECTION,
    CAT_OINTMENT,
    CAT_COUNT
} MedCategory;

const char  *category_to_string(MedCategory c);
MedCategory  category_from_string(const char *s);   /* CAT_COUNT if unknown */

/* ------------------------------------------------------------------ */
/* Medicine                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    char        id[ID_LEN];
    char        name[NAME_LEN];
    MedCategory category;
    double      unit_price;
    int         quantity;
    char        batch[BATCH_LEN];
    char        mfg_date[DATE_LEN];
    char        expiry_date[DATE_LEN];
    gboolean    rx_only;            /* prescription-only medicine */
} Medicine;

Medicine *medicine_new(void);
Medicine *medicine_copy(const Medicine *m);
void      medicine_free(gpointer m);

int       medicine_days_to_expiry(const Medicine *m);  /* negative = expired */
gboolean  medicine_is_expired(const Medicine *m);
gboolean  medicine_is_expiring_soon(const Medicine *m); /* 0..30 days left    */
gboolean  medicine_is_low_stock(const Medicine *m);
double    medicine_stock_value(const Medicine *m);
char     *medicine_status_text(const Medicine *m);      /* g_free() result   */

/* ------------------------------------------------------------------ */
/* Supplier                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    char       id[ID_LEN];
    char       name[NAME_LEN];
    char       phone[32];
    GPtrArray *medicine_ids;        /* array of gchar* (owned)     */
} Supplier;

Supplier *supplier_new(void);
void      supplier_free(gpointer s);
gboolean  supplier_supplies(const Supplier *s, const char *medicine_id);
char     *supplier_medicines_text(const Supplier *s); /* "M1, M2" g_free() */

/* ------------------------------------------------------------------ */
/* Sale                                                                */
/* ------------------------------------------------------------------ */
typedef struct {
    gint64 id;
    char   timestamp[STAMP_LEN];
    char   medicine_id[ID_LEN];
    char   medicine_name[NAME_LEN];
    int    quantity;
    double unit_price;
    double total;                   /* quantity x unit price       */
    double discount_pct;            /* 0 when no discount          */
    char   discount_reason[32];
    double final_total;             /* total after discount        */
    char   customer[NAME_LEN];
    char   prescription_no[BATCH_LEN];
    char   user[USER_LEN];
} Sale;

Sale *sale_new(void);
void  sale_free(gpointer s);
char *sale_receipt_text(const Sale *s);                 /* g_free() result   */

/* ------------------------------------------------------------------ */
/* Transaction (history entry)                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    gint64 id;
    char   timestamp[STAMP_LEN];
    char   user[USER_LEN];
    char   action[24];              /* ADD, SELL, UPDATE, REMOVE, DISCOUNT, SUPPLIER+... */
    char   medicine_id[ID_LEN];     /* may be empty                 */
    char   details[256];
} Transaction;

Transaction *transaction_new(void);
void         transaction_free(gpointer t);

/* ------------------------------------------------------------------ */
/* Date helpers                                                        */
/* ------------------------------------------------------------------ */
gboolean date_parse(const char *s, GDate *out);  /* strict YYYY-MM-DD   */
int      date_days_from_today(const char *s);    /* G_MININT if invalid */
void     date_today_str(char out[DATE_LEN]);
void     timestamp_now(char out[STAMP_LEN]);

#endif /* MODELS_H */
