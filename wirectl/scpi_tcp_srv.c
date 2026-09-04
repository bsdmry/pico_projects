#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "wirectl.h"

#define TCP_PORT 5025
#define COMMAND_SIZE 256

typedef struct {
    struct tcp_pcb *server;
    struct tcp_pcb *client;
    char command[COMMAND_SIZE];
    size_t command_length;
    enum intercore_command pending_response;
} scpi_server_t;

static scpi_server_t srv;

static err_t send_reply(struct tcp_pcb *pcb, const char *reply) {
    err_t result = tcp_write(pcb, reply, (u16_t)strlen(reply), TCP_WRITE_FLAG_COPY);
    if (result == ERR_OK) result = tcp_output(pcb);
    return result;
}

static char *trim(char *text) {
    char *end;
    while (isspace((unsigned char)*text)) ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

static bool command_is(const char *command, const char *name) {
    size_t length = strlen(name);
    return strncasecmp(command, name, length) == 0 &&
           (command[length] == '\0' || command[length] == '?' ||
            isspace((unsigned char)command[length]));
}

static bool parse_uint16(const char *argument, uint16_t *result) {
    char *end;
    unsigned long value;
    while (isspace((unsigned char)*argument)) ++argument;
    if (*argument == '\0') return false;
    value = strtoul(argument, &end, 10);
    while (isspace((unsigned char)*end)) ++end;
    if (*end != '\0' || value > 65535u) return false;
    *result = (uint16_t)value;
    return true;
}

static bool parse_lock(const char *argument, bool *result) {
    while (isspace((unsigned char)*argument)) ++argument;
    if (!strcasecmp(argument, "ON") || !strcmp(argument, "1")) { *result = true; return true; }
    if (!strcasecmp(argument, "OFF") || !strcmp(argument, "0")) { *result = false; return true; }
    return false;
}

static const char *argument_after(const char *command, const char *name) {
    const char *argument = command + strlen(name);
    while (isspace((unsigned char)*argument)) ++argument;
    return argument;
}

static void request_register(scpi_server_t *server,
                             enum intercore_command request,
                             enum intercore_command response) {
    if (server->pending_response != 0) {
        send_reply(server->client, "-410,\"Query interrupted\"\n");
        return;
    }
    server->pending_response = response;
    intercore_send(request, 0);
}

static enum intercore_command set_command(unsigned interface_number,
                                           enum intercore_command int1,
                                           enum intercore_command int2) {
    return interface_number == 1u ? int1 : int2;
}

static void execute_command(scpi_server_t *srv, struct tcp_pcb *pcb, char *line) {
    char reply[80];
    char *command = trim(line);
    if (*command == '\0') return;
    if (*command == ':') ++command;

    if (!strcasecmp(command, "*IDN?")) {
        send_reply(pcb, "DemoDev\n");
    } else if (command_is(command, "INFO:CTRL:NUMI") &&
               (*argument_after(command, "INFO:CTRL:NUMI") == '\0' || strchr(command, '?'))) {
        send_reply(pcb, "2\n");
    } else if (command_is(command, "INFO:NET:MAC") &&
               (*argument_after(command, "INFO:NET:MAC") == '\0' || strchr(command, '?'))) {
        const uint8_t *mac = netif_default ? netif_default->hwaddr : NULL;
        if (mac) snprintf(reply, sizeof(reply), "%02x%02x%02x%02x%02x%02x\n",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        else strcpy(reply, "000000000000\n");
        send_reply(pcb, reply);
    } else if (command_is(command, "INFO:NET:IP") &&
               (*argument_after(command, "INFO:NET:IP") == '\0' || strchr(command, '?'))) {
        snprintf(reply, sizeof(reply), "%s\n", netif_default
                 ? ip4addr_ntoa(netif_ip4_addr(netif_default)) : "0.0.0.0");
        send_reply(pcb, reply);
    } else {
        unsigned interface_number = 0;
        const char *suffix = NULL;
        if (!strncasecmp(command, "CTL:INT1:", 9)) { interface_number = 1; suffix = command + 9; }
        else if (!strncasecmp(command, "CTL:INT2:", 9)) { interface_number = 2; suffix = command + 9; }
        if (interface_number && command_is(suffix, "VOLT")) {
            const char *argument = argument_after(suffix, "VOLT");
            uint16_t value;
            if (*argument == '?') {
                request_register(srv,
                    set_command(interface_number, CORE1_GET_INT1_VOLTAGE_LIM, CORE1_GET_INT2_VOLTAGE_LIM),
                    set_command(interface_number, CORE0_DAT_INT1_VOLTAGE_LIM, CORE0_DAT_INT2_VOLTAGE_LIM));
            } else if (!parse_uint16(argument, &value) || value < WIRECTL_VOLTAGE_MIN || value > WIRECTL_VOLTAGE_MAX) {
                send_reply(pcb, "-222,\"Data out of range\"\n");
            } else {
                intercore_send(set_command(interface_number,
                               CORE1_SET_INT1_VOLTAGE_LIM, CORE1_SET_INT2_VOLTAGE_LIM), value);
            }
		} else if (interface_number && command_is(suffix, "CURR")) {
            const char *argument = argument_after(suffix, "CURR");
            uint16_t value;
            if (*argument == '?') {
                request_register(srv,
                    set_command(interface_number, CORE1_GET_INT1_CURRENT_LIM, CORE1_GET_INT2_CURRENT_LIM),
                    set_command(interface_number, CORE0_DAT_INT1_CURRENT_LIM, CORE0_DAT_INT2_CURRENT_LIM));
            } else if (!parse_uint16(argument, &value) || value < WIRECTL_CURRENT_MIN || value > WIRECTL_CURRENT_MAX) {
                send_reply(pcb, "-222,\"Data out of range\"\n");
            } else {
                intercore_send(set_command(interface_number,
                               CORE1_SET_INT1_CURRENT_LIM, CORE1_SET_INT2_CURRENT_LIM), value);
            }
        } else if (interface_number && command_is(suffix, "OUTP")) {
            const char *argument = argument_after(suffix, "OUTP");
            bool value;
            if (*argument == '?') {
                request_register(srv,
                    set_command(interface_number, CORE1_GET_INT1_OUTPUT, CORE1_GET_INT2_OUTPUT),
                    set_command(interface_number, CORE0_DAT_INT1_OUTPUT, CORE0_DAT_INT2_OUTPUT));
            } else if (!parse_lock(argument, &value)) {
                send_reply(pcb, "-222,\"Expected ON, OFF, 1 or 0\"\n");
            } else {
                intercore_send(set_command(interface_number,
                               CORE1_SET_INT1_OUTPUT, CORE1_SET_INT2_OUTPUT), value);
            }
        } else if (interface_number && command_is(suffix, "LOCK")) {
            const char *argument = argument_after(suffix, "LOCK");
            bool value;
            if (*argument == '?') {
                request_register(srv,
                    set_command(interface_number, CORE1_GET_INT1_LOCK, CORE1_GET_INT2_LOCK),
                    set_command(interface_number, CORE0_DAT_INT1_LOCK, CORE0_DAT_INT2_LOCK));
            } else if (!parse_lock(argument, &value)) {
                send_reply(pcb, "-222,\"Expected ON, OFF, 1 or 0\"\n");
            } else {
                intercore_send(set_command(interface_number,
                               CORE1_SET_INT1_LOCK, CORE1_SET_INT2_LOCK), value);
            }
        } else {
            send_reply(pcb, "-113,\"Undefined header\"\n");
        }
    }
}

static void client_detach(scpi_server_t *srv) {
    srv->client = NULL;
    srv->command_length = 0;
    srv->pending_response = 0;
}

static err_t scpi_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t error) {
    scpi_server_t *srv = (scpi_server_t *)arg;
    if (!p) { client_detach(srv); tcp_close(pcb); return ERR_OK; }
    if (error != ERR_OK) { pbuf_free(p); return error; }
    for (struct pbuf *part = p; part; part = part->next) {
        const char *bytes = (const char *)part->payload;
        for (u16_t i = 0; i < part->len; ++i) {
            char ch = bytes[i];
            if (ch == '\n' || ch == '\r' || ch == ';') {
                if (srv->command_length) {
                    srv->command[srv->command_length] = '\0';
                    execute_command(srv, pcb, srv->command);
                    srv->command_length = 0;
                }
            } else if (srv->command_length + 1u < sizeof(srv->command)) {
                srv->command[srv->command_length++] = ch;
            } else {
                srv->command_length = 0;
                send_reply(pcb, "-223,\"Command too long\"\n");
            }
        }
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void scpi_error(void *arg, err_t error) {
    (void)error;
    client_detach((scpi_server_t *)arg);
}

static err_t scpi_accept(void *arg, struct tcp_pcb *pcb, err_t error) {
    scpi_server_t *srv = (scpi_server_t *)arg;
    if (error != ERR_OK) return error;
    if (srv->client) { tcp_abort(pcb); return ERR_ABRT; }
    srv->client = pcb;
    srv->command_length = 0;
    tcp_arg(pcb, srv);
    tcp_recv(pcb, scpi_recv);
    tcp_err(pcb, scpi_error);
    tcp_setprio(pcb, TCP_PRIO_NORMAL);
    return ERR_OK;
}

void run_scpi_tcp_server(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb || tcp_bind(pcb, IP_ANY_TYPE, TCP_PORT) != ERR_OK) {
        printf("SCPI bind failed\n");
        if (pcb) tcp_close(pcb);
        return;
    }
    srv.server = tcp_listen_with_backlog(pcb, 1);
    if (!srv.server) { tcp_close(pcb); return; }
    tcp_arg(srv.server, &srv);
    tcp_accept(srv.server, scpi_accept);
    printf("SCPI server listening on port %u\n", TCP_PORT);
    for (;;) {
        uint32_t message;
        while (intercore_core0_try_pop(&message)) {
            enum intercore_command command = (enum intercore_command)(message >> 24);
            uint32_t value = message & 0x00ffffffu;
            cyw43_arch_lwip_begin(); // Stops background network processing
            if (srv.client && command == CORE0_ERR_INTERFACE_LOCKED) {
                send_reply(srv.client, "-221,\"Settings conflict: interface locked\"\n");
            } else if (srv.client && command == srv.pending_response) {
                char reply[32];
                bool logical = command == CORE0_DAT_INT1_LOCK || command == CORE0_DAT_INT2_LOCK ||
                               command == CORE0_DAT_INT1_OUTPUT || command == CORE0_DAT_INT2_OUTPUT;
                if (logical) { 
					snprintf(reply, sizeof(reply), "%s\n", value ? "ON" : "OFF"); 
				} else { 
					snprintf(reply, sizeof(reply), "%lu\n", (unsigned long)value); 
				}
                send_reply(srv.client, reply);
                srv.pending_response = 0;
            }
            cyw43_arch_lwip_end(); // Runs background network processing
        }
        sleep_ms(1);
    }
}
