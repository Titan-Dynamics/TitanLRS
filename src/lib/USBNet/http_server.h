#pragma once

#if defined(PLATFORM_STM32)

#include <stddef.h>
#include <stdint.h>

/*
 * A small HTTP/1.1 server on the lwIP raw TCP API, for the config API on the USB network link.
 *
 * - One request per connection (`Connection: close`): simple, and browsers cope fine.
 * - Requests are buffered whole (headers + Content-Length body) before the handler runs, and the
 *   handler's response is buffered whole and streamed out. Buffers come from the lwIP heap, which
 *   lives in AXI SRAM.
 * - A handler may defer its response (long-poll); the connection then stays open until
 *   HttpServer_Respond() is called with the handle, or the client goes away.
 * - Every response carries permissive CORS headers and the Private Network Access opt-in: the
 *   caller is a page on another (public, HTTPS) origin reaching a private address.
 */

#define HTTP_MAX_BODY 24576

struct HttpRequest
{
    const char *method;  // "GET", "POST", ...
    const char *path;    // "/config"
    const char *query;   // "export&x=1", or "" when absent
    const uint8_t *body; // NUL-terminated for convenience; may contain binary data
    size_t bodyLen;
};

/** Identifies a deferred response. Invalid once answered or once the client has gone. */
struct HttpDeferred
{
    void *conn;
    uint32_t generation;
};

class HttpResponse
{
public:
    /** Send a complete response. `body` is copied. */
    void send(int status, const char *contentType, const void *body, size_t len);
    void send(int status, const char *contentType, const char *text);

    /**
     * Allocate a body of `len` bytes to fill in place, then call send(status, contentType) —
     * avoids a second copy for large generated documents. Returns nullptr if out of memory.
     */
    char *allocBody(size_t len);
    void send(int status, const char *contentType);

    /** Hold the connection open and answer later with HttpServer_Respond(). */
    HttpDeferred defer();

    bool answered() const { return m_answered; }

private:
    friend void httpDispatch(void *conn, const HttpRequest &req);
    explicit HttpResponse(void *conn) : m_conn(conn) {}
    void *m_conn;
    char *m_body = nullptr;
    size_t m_bodyLen = 0;
    bool m_answered = false;
};

typedef void (*HttpHandler)(const HttpRequest &req, HttpResponse &res);

/** Start listening on port 80. `handler` gets every request except CORS preflights. */
void HttpServer_Init(HttpHandler handler);

/** Answer a deferred request. Returns false if the client has already gone. */
bool HttpServer_Respond(const HttpDeferred &handle, int status, const char *contentType, const void *body, size_t len);

/** True while the deferred request is still waiting for its answer. */
bool HttpServer_Pending(const HttpDeferred &handle);

/** Value of `key` in a query string ("a=1&b" -> "1" / ""), or nullptr when absent. Copies into `out`. */
const char *Http_QueryParam(const char *query, const char *key, char *out, size_t outLen);

#endif
