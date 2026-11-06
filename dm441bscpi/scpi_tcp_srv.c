#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "dm441bscpi.h"

/* Raw lwIP is polled on Core0. One client, bounded input/output queues. */
static struct {
    struct tcp_pcb *client;
    char line[128], output[1024];
    unsigned used, queued;
    bool discard;
} srv;
static void flush(struct tcp_pcb *pcb) {
    unsigned n = srv.queued;
    if (n > tcp_sndbuf(pcb)) n = tcp_sndbuf(pcb);
    if (n && tcp_write(pcb, srv.output, n, TCP_WRITE_FLAG_COPY) == ERR_OK) {
        memmove(srv.output, srv.output+n, srv.queued-n);
        srv.queued -= n;
        tcp_output(pcb);
    }
}
static bool reply(struct tcp_pcb *pcb, const char *text) {
    flush(pcb);
    size_t n = strlen(text);
    if (n > sizeof(srv.output)-srv.queued) return false;
    memcpy(srv.output+srv.queued, text, n);
    srv.queued += n;
    return true;
}
static bool execute(struct tcp_pcb *pcb) {
    char *s = srv.line;
    while (isspace((unsigned char)*s)) ++s;
    char *end = s+strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = 0;
    if (*s == ':') ++s;
    if (!*s) return true;
    if (!strcasecmp(s, "*IDN?")) return reply(pcb, "EZ DM-441B\n");
    if (!strcasecmp(s, "STAT?") || !strcasecmp(s, "STATUS?") ||
        !strcasecmp(s, "READ?") || !strcasecmp(s, "FETCH?") || !strcasecmp(s, "FETC?")) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%" PRIu64 "\n", dm_status());
        return reply(pcb, buffer);
    }
    if (!strcasecmp(s, "RAW?")) {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%" PRIu32 "\n", dm_raw());
        return reply(pcb, buffer);
    }
    if (!strcasecmp(s, "*OPC?")) return reply(pcb, "1\n");
    return reply(pcb, "-113,\"Undefined header\"\n");
}
static void detach(struct tcp_pcb *pcb) {
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_poll(pcb, NULL, 0);
    tcp_err(pcb, NULL);
    memset(&srv, 0, sizeof(srv));
}
/* Core0 calls this outside lwIP callbacks when the network is lost. */
void scpi_disconnect(void) {
    if (!srv.client) return;
    struct tcp_pcb *pcb = srv.client;
    detach(pcb);
    tcp_abort(pcb);
}
static err_t receive(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t error) {
    (void)arg;
    if (!p) {
        detach(pcb);
        if (tcp_close(pcb) != ERR_OK) { tcp_abort(pcb); return ERR_ABRT; }
        return ERR_OK;
    }
    if (error != ERR_OK) { pbuf_free(p); return error; }
    bool ok = true;
    for (struct pbuf *part = p; part && ok; part = part->next) {
        const unsigned char *bytes = part->payload;
        for (unsigned i = 0; i < part->len && ok; ++i) {
            unsigned char ch = bytes[i];
            if (ch == '\r' || ch == '\n' || ch == ';') {
                if (srv.discard) ok = reply(pcb, "-223,\"Invalid or oversized command\"\n");
                else if (srv.used) { srv.line[srv.used] = 0; ok = execute(pcb); }
                srv.used = 0;
                srv.discard = false;
            } else if (!srv.discard) {
                if (!ch || srv.used == sizeof(srv.line)-1) srv.discard = true;
                else srv.line[srv.used++] = ch;
            }
        }
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    if (!ok) { detach(pcb); tcp_abort(pcb); return ERR_ABRT; }
    flush(pcb);
    return ERR_OK;
}
static void failed(void *arg, err_t error) {
    (void)arg; (void)error;
    memset(&srv, 0, sizeof(srv));
}
static err_t sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)arg; (void)len; flush(pcb); return ERR_OK;
}
static err_t poll_client(void *arg, struct tcp_pcb *pcb) {
    (void)arg; flush(pcb); return ERR_OK;
}
static err_t accept_client(void *arg, struct tcp_pcb *pcb, err_t error) {
    (void)arg;
    if (error != ERR_OK) return error;
    if (srv.client) { tcp_abort(pcb); return ERR_ABRT; }
    srv.client = pcb;
    tcp_recv(pcb, receive);
    tcp_err(pcb, failed);
    tcp_sent(pcb, sent);
    tcp_poll(pcb, poll_client, 2);
    return ERR_OK;
}
bool scpi_start(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) return false;
    if (tcp_bind(pcb, IP_ANY_TYPE, 5025) != ERR_OK) { tcp_close(pcb); return false; }
    struct tcp_pcb *listener = tcp_listen_with_backlog(pcb, 1);
    if (!listener) { tcp_close(pcb); return false; }
    tcp_accept(listener, accept_client);
    return true;
}
