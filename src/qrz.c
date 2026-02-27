#include "qrz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>

static char qrz_user[64];
static char qrz_pass[64];
static char session_key[64];

/* Dynamic buffer for curl response */
typedef struct {
    char  *data;
    size_t size;
    size_t cap;
} Buffer;

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    Buffer *buf = (Buffer *)userdata;
    if (size != 0 && nmemb > (size_t)-1 / size) return 0;
    size_t total = size * nmemb;
    if (total > (size_t)-1 - buf->size - 1) return 0;
    if (buf->size + total + 1 > buf->cap) {
        size_t newcap = (buf->cap + total + 1) * 2;
        char *tmp = realloc(buf->data, newcap);
        if (!tmp) return 0;
        buf->data = tmp;
        buf->cap = newcap;
    }
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

static void buf_init(Buffer *buf)
{
    buf->data = malloc(4096);
    buf->size = 0;
    buf->cap = 4096;
    if (buf->data) buf->data[0] = '\0';
}

static void buf_free(Buffer *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->cap = 0;
}

/* Simple XML tag extractor: find <tag>...</tag> and copy content to out. */
static int xml_extract(const char *xml, const char *tag, char *out, int out_sz)
{
    char open[128], close[128];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *start = strstr(xml, open);
    if (!start) return -1;
    start += strlen(open);
    const char *end = strstr(start, close);
    if (!end) return -1;
    int len = (int)(end - start);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

static int http_get(const char *url, Buffer *buf)
{
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    buf_init(buf);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        buf_free(buf);
        return -1;
    }
    return 0;
}

static int qrz_login(char *err_buf, int err_sz)
{
    CURL *enc = curl_easy_init();
    if (!enc) {
        if (err_buf) snprintf(err_buf, err_sz, "CURL INIT FAILED");
        return -1;
    }
    char *esc_user = curl_easy_escape(enc, qrz_user, 0);
    char *esc_pass = curl_easy_escape(enc, qrz_pass, 0);
    char url[512];
    snprintf(url, sizeof(url),
             "https://xmldata.qrz.com/xml/current/?username=%s;password=%s;agent=azmap1.0",
             esc_user ? esc_user : "", esc_pass ? esc_pass : "");
    curl_free(esc_user);
    curl_free(esc_pass);
    curl_easy_cleanup(enc);

    Buffer buf;
    if (http_get(url, &buf) != 0) {
        if (err_buf) snprintf(err_buf, err_sz, "HTTP REQUEST FAILED");
        return -1;
    }

    /* Check for error first */
    char errmsg[256];
    if (xml_extract(buf.data, "Error", errmsg, sizeof(errmsg)) == 0) {
        if (err_buf) snprintf(err_buf, err_sz, "%.63s", errmsg);
        buf_free(&buf);
        return -1;
    }

    /* Extract session key */
    char key[64];
    if (xml_extract(buf.data, "Key", key, sizeof(key)) != 0) {
        if (err_buf) snprintf(err_buf, err_sz, "NO SESSION KEY IN RESPONSE");
        buf_free(&buf);
        return -1;
    }

    strncpy(session_key, key, sizeof(session_key) - 1);
    session_key[sizeof(session_key) - 1] = '\0';
    buf_free(&buf);
    return 0;
}

int qrz_init(const char *username, const char *password)
{
    strncpy(qrz_user, username, sizeof(qrz_user) - 1);
    qrz_user[sizeof(qrz_user) - 1] = '\0';
    strncpy(qrz_pass, password, sizeof(qrz_pass) - 1);
    qrz_pass[sizeof(qrz_pass) - 1] = '\0';
    session_key[0] = '\0';
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return 0;
}

int qrz_lookup(const char *callsign, QRZResult *result, char *err_buf, int err_sz)
{
    memset(result, 0, sizeof(*result));

    /* Login if no session key */
    if (session_key[0] == '\0') {
        if (qrz_login(err_buf, err_sz) != 0)
            return -1;
    }

    /* Build lookup URL */
    char call_upper[32];
    int ci = 0;
    for (int i = 0; callsign[i] && ci < 31; i++)
        call_upper[ci++] = (char)toupper((unsigned char)callsign[i]);
    call_upper[ci] = '\0';

    CURL *enc = curl_easy_init();
    if (!enc) {
        if (err_buf) snprintf(err_buf, err_sz, "CURL INIT FAILED");
        return -1;
    }
    char *esc_call = curl_easy_escape(enc, call_upper, 0);
    char url[512];
    snprintf(url, sizeof(url),
             "https://xmldata.qrz.com/xml/current/?s=%s;callsign=%s",
             session_key, esc_call ? esc_call : "");
    curl_free(esc_call);
    curl_easy_cleanup(enc);

    Buffer buf;
    if (http_get(url, &buf) != 0) {
        if (err_buf) snprintf(err_buf, err_sz, "HTTP REQUEST FAILED");
        return -1;
    }

    /* Check for session timeout / error — retry once */
    char errmsg[256];
    if (xml_extract(buf.data, "Error", errmsg, sizeof(errmsg)) == 0) {
        if (strstr(errmsg, "Session Timeout") || strstr(errmsg, "Invalid session key") ||
            strstr(errmsg, "session")) {
            buf_free(&buf);
            session_key[0] = '\0';
            if (qrz_login(err_buf, err_sz) != 0)
                return -1;
            /* Retry lookup */
            snprintf(url, sizeof(url),
                     "https://xmldata.qrz.com/xml/current/?s=%s;callsign=%s",
                     session_key, call_upper);
            if (http_get(url, &buf) != 0) {
                if (err_buf) snprintf(err_buf, err_sz, "HTTP REQUEST FAILED");
                return -1;
            }
            if (xml_extract(buf.data, "Error", errmsg, sizeof(errmsg)) == 0) {
                if (err_buf) snprintf(err_buf, err_sz, "%.63s", errmsg);
                buf_free(&buf);
                return -1;
            }
        } else {
            if (err_buf) snprintf(err_buf, err_sz, "%.63s", errmsg);
            buf_free(&buf);
            return -1;
        }
    }

    /* Extract fields */
    char lat_s[32], lon_s[32], fname[64], name_s[64];
    char addr2[64], country[64], grid[16], call_s[32];

    lat_s[0] = lon_s[0] = fname[0] = name_s[0] = '\0';
    addr2[0] = country[0] = grid[0] = call_s[0] = '\0';

    xml_extract(buf.data, "lat", lat_s, sizeof(lat_s));
    xml_extract(buf.data, "lon", lon_s, sizeof(lon_s));
    xml_extract(buf.data, "fname", fname, sizeof(fname));
    xml_extract(buf.data, "name", name_s, sizeof(name_s));
    xml_extract(buf.data, "addr2", addr2, sizeof(addr2));
    xml_extract(buf.data, "country", country, sizeof(country));
    xml_extract(buf.data, "grid", grid, sizeof(grid));
    xml_extract(buf.data, "call", call_s, sizeof(call_s));

    buf_free(&buf);

    /* Populate result */
    if (call_s[0])
        strncpy(result->call, call_s, sizeof(result->call) - 1);
    else
        strncpy(result->call, call_upper, sizeof(result->call) - 1);

    /* Compose name: "FNAME NAME" */
    if (fname[0] && name_s[0])
        snprintf(result->name, sizeof(result->name), "%s %s", fname, name_s);
    else if (fname[0])
        strncpy(result->name, fname, sizeof(result->name) - 1);
    else if (name_s[0])
        strncpy(result->name, name_s, sizeof(result->name) - 1);

    /* Compose location: "ADDR2, COUNTRY" */
    if (addr2[0] && country[0])
        snprintf(result->location, sizeof(result->location), "%.60s, %.60s", addr2, country);
    else if (country[0])
        strncpy(result->location, country, sizeof(result->location) - 1);
    else if (addr2[0])
        strncpy(result->location, addr2, sizeof(result->location) - 1);

    strncpy(result->grid, grid, sizeof(result->grid) - 1);

    if (lat_s[0] && lon_s[0]) {
        result->lat = atof(lat_s);
        result->lon = atof(lon_s);
        result->valid = 1;
    }

    return 0;
}

void qrz_cleanup(void)
{
    curl_global_cleanup();
}
