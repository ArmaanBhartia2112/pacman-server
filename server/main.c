#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static Server g_server;

static void on_signal(int sig) {
    (void)sig;
    g_server.running = 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [--tcp-port PORT] [--udp-port PORT] [--tick-rate HZ]\n"
            "Defaults: tcp=7777 udp=7778 tick=25\n", prog);
}

int main(int argc, char **argv) {
    int tcp_port    = 7777;
    int udp_port    = 7778;
    int tick_rate   = 25;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tcp-port") == 0 && i+1 < argc)
            tcp_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--udp-port") == 0 && i+1 < argc)
            udp_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--tick-rate") == 0 && i+1 < argc)
            tick_rate = atoi(argv[++i]);
        else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]); return 0;
        }
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    if (server_init(&g_server, tcp_port, udp_port, tick_rate) < 0) {
        fprintf(stderr, "Server init failed\n");
        return 1;
    }

    server_run(&g_server);
    server_destroy(&g_server);
    return 0;
}
