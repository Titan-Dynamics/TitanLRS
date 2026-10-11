#include "http_server.h"

#if defined(PLATFORM_STM32)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lwip/mem.h"
#include "lwip/tcp.h"

#define HTTP_PORT 80
// Browsers open several connections at once, and a long-poll holds one open, so a handful.
#define HTTP_MAX_CONNS 4
#define HTTP_HEADER_MAX 1024
#define HTTP_PATH_MAX 64
#define HTTP_QUERY_MAX 128
// tcp_poll interval, in TCP coarse ticks (500 ms): one poll a second.
#define HTTP_POLL_INTERVAL 2
// A request that has not finished arriving after this many polls is abandoned.
#define HTTP_REQUEST_TIMEOUT_POLLS 10

enum ConnState : uint8_t
{
    CONN_FREE,
    CONN_READ_HEADERS,
    CONN_READ_BODY,
    CONN_DEFERRED,
    CONN_SENDING,
};

struct HttpConn
{
    struct tcp_pcb *pcb;
    ConnState state;
    uint32_t generation;
    uint8_t idlePolls;

    char hdr[HTTP_HEADER_MAX + 1];
    uint16_t hdrLen;
    char method[8];
    char path[HTTP_PATH_MAX];
    char query[HTTP_QUERY_MAX];

    uint8_t *body;
    size_t bodyLen;
    size_t bodyExpected;

    char *tx;
    size_t txLen;
    size_t txSent;
};

static HttpConn s_conns[HTTP_MAX_CONNS];
static HttpHandler s_handler = nullptr;
static struct tcp_pcb *s_listen = nullptr;
static uint32_t s_generation = 0;
// Set when closeConn() had to abort: the lwIP callback that led there must return ERR_ABRT.
static struct tcp_pcb *s_abortedPcb = nullptr;

static const char *reasonPhrase(int status)
{
    switch (status)
    {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "";
    }
}

// ---------------------------------------------------------------------------------------------
// connection lifetime
// ---------------------------------------------------------------------------------------------

static void releaseBuffers(HttpConn *c)
{
    if (c->body != nullptr)
    {
        mem_free(c->body);
        c->body = nullptr;
    }
    if (c->tx != nullptr)
    {
        mem_free(c->tx);
        c->tx = nullptr;
    }
}

static void freeConn(HttpConn *c)
{
    releaseBuffers(c);
    c->pcb = nullptr;
    c->state = CONN_FREE;
    c->generation = ++s_generation;
}

static void detach(struct tcp_pcb *pcb)
{
    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_sent(pcb, nullptr);
    tcp_err(pcb, nullptr);
    tcp_poll(pcb, nullptr, 0);
}

/** Close gracefully; returns ERR_ABRT if it had to abort instead (callers inside a callback must
 *  then return ERR_ABRT). */
static err_t closeConn(HttpConn *c)
{
    struct tcp_pcb *pcb = c->pcb;
    freeConn(c);
    if (pcb == nullptr)
    {
        return ERR_OK;
    }
    detach(pcb);
    if (tcp_close(pcb) != ERR_OK)
    {
        s_abortedPcb = pcb;
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    return ERR_OK;
}

// ---------------------------------------------------------------------------------------------
// sending
// ---------------------------------------------------------------------------------------------

static err_t sendMore(HttpConn *c)
{
    while (c->txSent < c->txLen)
    {
        const u16_t room = tcp_sndbuf(c->pcb);
        if (room == 0)
        {
            break;
        }
        size_t n = c->txLen - c->txSent;
        if (n > room)
        {
            n = room;
        }
        const u8_t more = (c->txSent + n < c->txLen) ? TCP_WRITE_FLAG_MORE : 0;
        if (tcp_write(c->pcb, c->tx + c->txSent, (u16_t)n, TCP_WRITE_FLAG_COPY | more) != ERR_OK)
        {
            break; // out of segments for now; tcp_sent / poll will call again
        }
        c->txSent += n;
    }
    tcp_output(c->pcb);

    if (c->txSent >= c->txLen)
    {
        // Everything is queued (copied): the FIN follows the data.
        return closeConn(c);
    }
    return ERR_OK;
}

static bool queueResponse(HttpConn *c, int status, const char *contentType, const void *body, size_t len)
{
    char head[400];
    const int headLen = snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: *\r\n"
        "Access-Control-Allow-Private-Network: true\r\n"
        "Access-Control-Max-Age: 600\r\n"
        "\r\n",
        status, reasonPhrase(status), contentType ? contentType : "text/plain", (unsigned)len);
    if (headLen <= 0 || (size_t)headLen >= sizeof(head))
    {
        return false;
    }

    c->tx = (char *)mem_malloc((mem_size_t)(headLen + len));
    if (c->tx == nullptr)
    {
        closeConn(c);
        return false;
    }
    memcpy(c->tx, head, headLen);
    if (len)
    {
        memcpy(c->tx + headLen, body, len);
    }
    c->txLen = headLen + len;
    c->txSent = 0;
    c->state = CONN_SENDING;
    sendMore(c);
    return true;
}

// ---------------------------------------------------------------------------------------------
// HttpResponse
// ---------------------------------------------------------------------------------------------

void HttpResponse::send(int status, const char *contentType, const void *body, size_t len)
{
    if (m_answered)
    {
        return;
    }
    m_answered = true;
    queueResponse((HttpConn *)m_conn, status, contentType, body, len);
    if (m_body != nullptr)
    {
        mem_free(m_body);
        m_body = nullptr;
    }
}

void HttpResponse::send(int status, const char *contentType, const char *text)
{
    send(status, contentType, text, text ? strlen(text) : 0);
}

char *HttpResponse::allocBody(size_t len)
{
    if (m_body != nullptr)
    {
        mem_free(m_body);
    }
    m_body = (char *)mem_malloc((mem_size_t)(len + 1));
    m_bodyLen = m_body ? len : 0;
    return m_body;
}

void HttpResponse::send(int status, const char *contentType)
{
    char *body = m_body;
    m_body = nullptr;
    send(status, contentType, body, m_bodyLen);
    if (body != nullptr)
    {
        mem_free(body);
    }
}

HttpDeferred HttpResponse::defer()
{
    HttpConn *c = (HttpConn *)m_conn;
    m_answered = true;
    c->state = CONN_DEFERRED;
    c->idlePolls = 0;
    return HttpDeferred{c, c->generation};
}

static HttpConn *deferredConn(const HttpDeferred &handle)
{
    HttpConn *c = (HttpConn *)handle.conn;
    if (c == nullptr || c->state != CONN_DEFERRED || c->generation != handle.generation)
    {
        return nullptr;
    }
    return c;
}

bool HttpServer_Respond(const HttpDeferred &handle, int status, const char *contentType, const void *body, size_t len)
{
    HttpConn *c = deferredConn(handle);
    return c != nullptr && queueResponse(c, status, contentType, body, len);
}

bool HttpServer_Pending(const HttpDeferred &handle)
{
    return deferredConn(handle) != nullptr;
}

// ---------------------------------------------------------------------------------------------
// request parsing
// ---------------------------------------------------------------------------------------------

void httpDispatch(void *conn, const HttpRequest &req)
{
    HttpConn *c = (HttpConn *)conn;
    HttpResponse res(c);
    if (strcmp(req.method, "OPTIONS") == 0)
    {
        // CORS / private-network preflight: the headers are on every response.
        res.send(204, "text/plain", nullptr, 0);
    }
    else if (s_handler != nullptr)
    {
        s_handler(req, res);
    }
    if (!res.answered())
    {
        res.send(500, "text/plain", "no response");
    }
}

static void requestComplete(HttpConn *c)
{
    HttpRequest req;
    req.method = c->method;
    req.path = c->path;
    req.query = c->query;
    req.body = c->body;
    req.bodyLen = c->bodyLen;
    httpDispatch(c, req);
    // The body is no longer needed whether the response went out or was deferred.
    if (c->state != CONN_FREE && c->body != nullptr)
    {
        mem_free(c->body);
        c->body = nullptr;
    }
}

/** Parses the request line and headers. Returns an HTTP status on failure, 0 on success. */
static int parseHeaders(HttpConn *c, size_t headerEnd)
{
    c->hdr[headerEnd] = '\0';
    char *line = c->hdr;
    char *eol = strstr(line, "\r\n");
    if (eol == nullptr)
    {
        return 400;
    }
    *eol = '\0';

    // METHOD SP target SP version
    char *sp1 = strchr(line, ' ');
    char *sp2 = sp1 ? strchr(sp1 + 1, ' ') : nullptr;
    if (sp1 == nullptr || sp2 == nullptr || (size_t)(sp1 - line) >= sizeof(c->method))
    {
        return 400;
    }
    *sp1 = '\0';
    *sp2 = '\0';
    strcpy(c->method, line);

    char *target = sp1 + 1;
    char *q = strchr(target, '?');
    if (q != nullptr)
    {
        *q++ = '\0';
    }
    if (strlen(target) >= sizeof(c->path) || (q && strlen(q) >= sizeof(c->query)))
    {
        return 414;
    }
    strcpy(c->path, target);
    strcpy(c->query, q ? q : "");

    c->bodyExpected = 0;
    for (line = eol + 2; *line; line = eol + 2)
    {
        eol = strstr(line, "\r\n");
        if (eol == nullptr)
        {
            break;
        }
        *eol = '\0';
        if (strncasecmp(line, "Content-Length:", 15) == 0)
        {
            c->bodyExpected = strtoul(line + 15, nullptr, 10);
        }
    }
    if (c->bodyExpected > HTTP_MAX_BODY)
    {
        return 413;
    }
    return 0;
}

static void failRequest(HttpConn *c, int status)
{
    queueResponse(c, status, "text/plain", reasonPhrase(status), strlen(reasonPhrase(status)));
}

/** Feeds received bytes into the request. */
static void consume(HttpConn *c, const uint8_t *data, size_t len)
{
    if (c->state == CONN_READ_HEADERS)
    {
        const size_t room = HTTP_HEADER_MAX - c->hdrLen;
        const size_t take = len < room ? len : room;
        memcpy(c->hdr + c->hdrLen, data, take);
        c->hdrLen += take;
        c->hdr[c->hdrLen] = '\0';

        char *end = strstr(c->hdr, "\r\n\r\n");
        if (end == nullptr)
        {
            if (c->hdrLen >= HTTP_HEADER_MAX)
            {
                failRequest(c, 431);
            }
            return;
        }

        const size_t headerEnd = (end - c->hdr) + 2; // keep the last header's CRLF
        const size_t bodyStart = (end - c->hdr) + 4;
        // Body bytes that arrived along with the headers, from this chunk and earlier ones.
        const size_t extraInHdr = c->hdrLen - bodyStart;
        const uint8_t *extra = (const uint8_t *)c->hdr + bodyStart;
        const size_t extraInData = len - take;

        const int status = parseHeaders(c, headerEnd);
        if (status != 0)
        {
            failRequest(c, status);
            return;
        }

        c->body = (uint8_t *)mem_malloc((mem_size_t)(c->bodyExpected + 1));
        if (c->body == nullptr)
        {
            failRequest(c, 503);
            return;
        }
        c->bodyLen = 0;
        c->state = CONN_READ_BODY;
        consume(c, extra, extraInHdr);
        if (c->state == CONN_READ_BODY && extraInData)
        {
            consume(c, data + take, extraInData);
        }
        if (c->state == CONN_READ_BODY && c->bodyLen >= c->bodyExpected)
        {
            c->body[c->bodyLen] = '\0';
            requestComplete(c);
        }
        return;
    }

    if (c->state == CONN_READ_BODY)
    {
        size_t take = c->bodyExpected - c->bodyLen;
        if (take > len)
        {
            take = len;
        }
        memcpy(c->body + c->bodyLen, data, take);
        c->bodyLen += take;
    }
    // Bytes after a complete request (or while deferred / sending) are ignored. Completion of a
    // body is checked by the caller once the whole pbuf chain is in.
}

// ---------------------------------------------------------------------------------------------
// lwIP callbacks
// ---------------------------------------------------------------------------------------------

static err_t onRecv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    HttpConn *c = (HttpConn *)arg;
    if (c == nullptr)
    {
        if (p)
        {
            pbuf_free(p);
        }
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    if (p == nullptr || err != ERR_OK)
    {
        // The client closed its side. A deferred request has nobody left to answer.
        if (p)
        {
            pbuf_free(p);
        }
        if (c->state == CONN_SENDING)
        {
            return ERR_OK; // finish sending; the close follows
        }
        return closeConn(c);
    }

    c->idlePolls = 0;
    s_abortedPcb = nullptr;
    // Acknowledge the data up front: everything received is consumed (or deliberately ignored),
    // and tcp_close() answers with a RST rather than a FIN while any received data is still
    // unacknowledged — which is exactly when the response below closes the connection.
    tcp_recved(pcb, p->tot_len);
    for (struct pbuf *q = p; q != nullptr && c->state != CONN_FREE; q = q->next)
    {
        consume(c, (const uint8_t *)q->payload, q->len);
    }
    if (c->state == CONN_READ_BODY && c->bodyLen >= c->bodyExpected)
    {
        c->body[c->bodyLen] = '\0';
        requestComplete(c);
    }
    pbuf_free(p);
    return s_abortedPcb == pcb ? ERR_ABRT : ERR_OK;
}

static err_t onSent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)pcb;
    (void)len;
    HttpConn *c = (HttpConn *)arg;
    if (c != nullptr && c->state == CONN_SENDING)
    {
        return sendMore(c);
    }
    return ERR_OK;
}

static void onError(void *arg, err_t err)
{
    (void)err;
    HttpConn *c = (HttpConn *)arg;
    if (c != nullptr)
    {
        // lwIP has already freed the pcb.
        c->pcb = nullptr;
        freeConn(c);
    }
}

static err_t onPoll(void *arg, struct tcp_pcb *pcb)
{
    HttpConn *c = (HttpConn *)arg;
    if (c == nullptr)
    {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    switch (c->state)
    {
    case CONN_SENDING:
        return sendMore(c);
    case CONN_READ_HEADERS:
    case CONN_READ_BODY:
        if (++c->idlePolls >= HTTP_REQUEST_TIMEOUT_POLLS)
        {
            return closeConn(c);
        }
        break;
    default:
        // Deferred requests are timed out by whoever deferred them.
        break;
    }
    return ERR_OK;
}

static err_t onAccept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || pcb == nullptr)
    {
        return ERR_VAL;
    }
    HttpConn *c = nullptr;
    for (auto &conn : s_conns)
    {
        if (conn.state == CONN_FREE)
        {
            c = &conn;
            break;
        }
    }
    if (c == nullptr)
    {
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    c->pcb = pcb;
    c->state = CONN_READ_HEADERS;
    c->idlePolls = 0;
    c->hdrLen = 0;
    c->body = nullptr;
    c->bodyLen = 0;
    c->bodyExpected = 0;
    c->tx = nullptr;
    c->txLen = 0;
    c->txSent = 0;

    tcp_arg(pcb, c);
    tcp_recv(pcb, onRecv);
    tcp_sent(pcb, onSent);
    tcp_err(pcb, onError);
    tcp_poll(pcb, onPoll, HTTP_POLL_INTERVAL);
    tcp_nagle_disable(pcb);
    return ERR_OK;
}

void HttpServer_Init(HttpHandler handler)
{
    s_handler = handler;
    if (s_listen != nullptr)
    {
        return;
    }
    for (auto &c : s_conns)
    {
        c.pcb = nullptr;
        c.state = CONN_FREE;
        c.generation = 0;
        c.body = nullptr;
        c.tx = nullptr;
    }
    struct tcp_pcb *pcb = tcp_new();
    if (pcb == nullptr)
    {
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, HTTP_PORT) != ERR_OK)
    {
        tcp_abort(pcb);
        return;
    }
    s_listen = tcp_listen(pcb);
    tcp_accept(s_listen, onAccept);
}

// ---------------------------------------------------------------------------------------------

const char *Http_QueryParam(const char *query, const char *key, char *out, size_t outLen)
{
    if (query == nullptr || outLen == 0)
    {
        return nullptr;
    }
    const size_t keyLen = strlen(key);
    const char *p = query;
    while (*p)
    {
        const char *end = strchr(p, '&');
        if (end == nullptr)
        {
            end = p + strlen(p);
        }
        const char *eq = (const char *)memchr(p, '=', end - p);
        const char *nameEnd = eq ? eq : end;
        if ((size_t)(nameEnd - p) == keyLen && strncmp(p, key, keyLen) == 0)
        {
            size_t n = eq ? (size_t)(end - eq - 1) : 0;
            if (n >= outLen)
            {
                n = outLen - 1;
            }
            if (n)
            {
                memcpy(out, eq + 1, n);
            }
            out[n] = '\0';
            return out;
        }
        p = *end ? end + 1 : end;
    }
    return nullptr;
}

#endif
