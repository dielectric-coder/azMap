#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "fetch.h"

typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
} Buffer;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    Buffer *buf = userdata;
    size_t total = size * nmemb;
    if (buf->len + total + 1 > buf->cap) {
        size_t newcap = (buf->cap + total + 1) * 2;
        char *p = realloc(buf->data, newcap);
        if (!p) return 0;
        buf->data = p;
        buf->cap = newcap;
    }
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

static void *fetch_thread(void *arg)
{
    FetchRequest *req = arg;
    CURL *curl = curl_easy_init();
    if (!curl) {
        pthread_mutex_lock(&req->mutex);
        req->status = -1;
        pthread_mutex_unlock(&req->mutex);
        return NULL;
    }

    Buffer buf = { .data = malloc(4096), .len = 0, .cap = 4096 };
    if (!buf.data) {
        curl_easy_cleanup(curl);
        pthread_mutex_lock(&req->mutex);
        req->status = -1;
        pthread_mutex_unlock(&req->mutex);
        return NULL;
    }
    buf.data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "azMap/1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    pthread_mutex_lock(&req->mutex);
    if (res == CURLE_OK) {
        req->response = buf.data;
        req->response_len = buf.len;
        req->status = 1;
    } else {
        free(buf.data);
        req->status = -1;
    }
    pthread_mutex_unlock(&req->mutex);
    return NULL;
}

void fetch_start(FetchRequest *req, const char *url)
{
    memset(req, 0, sizeof(*req));
    pthread_mutex_init(&req->mutex, NULL);
    req->url = strdup(url);
    req->status = 0;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&req->thread, &attr, fetch_thread, req);
    pthread_attr_destroy(&attr);
}

int fetch_check(FetchRequest *req)
{
    pthread_mutex_lock(&req->mutex);
    int s = req->status;
    pthread_mutex_unlock(&req->mutex);
    return s;
}

char *fetch_take_response(FetchRequest *req)
{
    pthread_mutex_lock(&req->mutex);
    char *r = req->response;
    req->response = NULL;
    pthread_mutex_unlock(&req->mutex);
    return r;
}

void fetch_cleanup(FetchRequest *req)
{
    free(req->url);
    req->url = NULL;
    free(req->response);
    req->response = NULL;
    pthread_mutex_destroy(&req->mutex);
}
