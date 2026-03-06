/* fetch.h — Threaded non-blocking HTTP GET via libcurl + pthread.
 *
 * Each FetchRequest spawns a detached thread that performs an HTTP GET.
 * The main loop polls fetch_check() each frame; when done, it takes
 * ownership of the response string.  Used by all overlay data sources
 * (MUF, Sporadic E, Aurora, DRAP, Kp/Bz) for async refresh. */

#ifndef FETCH_H
#define FETCH_H

#include <pthread.h>
#include <stddef.h>

typedef struct {
    char           *url;
    char           *response;      /* malloc'd response body (caller frees) */
    size_t          response_len;
    int             status;        /* 0=pending, 1=done, -1=error */
    pthread_t       thread;
    pthread_mutex_t mutex;
} FetchRequest;

/* Start an async HTTP GET in a background thread. */
void fetch_start(FetchRequest *req, const char *url);

/* Non-blocking check: returns status (0=pending, 1=done, -1=error). */
int  fetch_check(FetchRequest *req);

/* Take ownership of the response string (caller must free). Returns NULL on error. */
char *fetch_take_response(FetchRequest *req);

/* Cleanup request resources (does NOT join thread — it is detached). */
void fetch_cleanup(FetchRequest *req);

#endif
