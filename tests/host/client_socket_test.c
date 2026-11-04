/*
 * FR-8: keepalive configuration of accepted client sockets.
 *
 * client_socket.c is built with setsockopt and getsockopt renamed to the
 * wrappers below. They call the real Linux functions on a real TCP socket,
 * unless a test asks one call to fail or to read back another value.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "client_socket.h"

static int pass = 0, fail = 0;
#define CHECK(c, ...) do { if (c) { pass++; printf("  ok   "); } else { fail++; printf("  FAIL "); } \
                           printf(__VA_ARGS__); printf("\n"); } while (0)

/* ---- log capture */
static int info_logs, warn_logs, error_logs;
static char last_info[256], last_error[256];

void test_log(char level, const char *tag, const char *format, ...)
{
    char line[256];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    fprintf(stderr, "%c %s: %s\n", level, tag, line);

    if (level == 'I') { info_logs++; snprintf(last_info, sizeof(last_info), "%s", line); }
    if (level == 'W') { warn_logs++; }
    if (level == 'E') { error_logs++; snprintf(last_error, sizeof(last_error), "%s", line); }
}

/* ---- socket option wrappers */
typedef struct { int level, option; } option_id_t;
static option_id_t fail_set, fail_get, fake_get;
static int fake_value;
static int set_calls;

static int matches(option_id_t id, int level, int option)
{
    return id.level == level && id.option == option;
}

int test_setsockopt(int fd, int level, int option, const void *value, socklen_t length)
{
    set_calls++;
    if (matches(fail_set, level, option)) { errno = ENOPROTOOPT; return -1; }
    return setsockopt(fd, level, option, value, length);
}

int test_getsockopt(int fd, int level, int option, void *value, socklen_t *length)
{
    if (matches(fail_get, level, option)) { errno = EBADF; return -1; }
    if (matches(fake_get, level, option)) { *(int *)value = fake_value; *length = sizeof(int); return 0; }
    return getsockopt(fd, level, option, value, length);
}

/* ---- helpers */
static const option_id_t NONE = {-1, -1};

static void reset(void)
{
    fail_set = fail_get = fake_get = NONE;
    fake_value = 0;
    set_calls = info_logs = warn_logs = error_logs = 0;
    last_info[0] = last_error[0] = '\0';
}

/* An accepted TCP socket, like the one the server configures. */
static int accepted_socket(int *client)
{
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t length = sizeof(address);
    bind(listener, (struct sockaddr *)&address, sizeof(address));
    listen(listener, 1);
    getsockname(listener, (struct sockaddr *)&address, &length);
    *client = socket(AF_INET, SOCK_STREAM, 0);
    connect(*client, (struct sockaddr *)&address, sizeof(address));
    int server = accept(listener, NULL, NULL);
    close(listener);
    return server;
}

static int read_option(int fd, int level, int option)
{
    int value = -1;
    socklen_t length = sizeof(value);
    getsockopt(fd, level, option, &value, &length);
    return value;
}

static void close_pair(int server, int client)
{
    close(server);
    close(client);
}

static const struct { int level, option; const char *name; } REQUIRED[] = {
    {SOL_SOCKET, SO_KEEPALIVE, "SO_KEEPALIVE"},
    {IPPROTO_TCP, TCP_KEEPIDLE, "TCP_KEEPIDLE"},
    {IPPROTO_TCP, TCP_KEEPINTVL, "TCP_KEEPINTVL"},
    {IPPROTO_TCP, TCP_KEEPCNT, "TCP_KEEPCNT"},
};

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("K1 keepalive applied on a real TCP socket\n");
    {
        reset();
        int client, server = accepted_socket(&client);
        esp_err_t err = client_socket_configure(server);
        CHECK(err == ESP_OK, "configuration succeeds");
        CHECK(read_option(server, SOL_SOCKET, SO_KEEPALIVE) == 1, "SO_KEEPALIVE is on");
        CHECK(read_option(server, IPPROTO_TCP, TCP_KEEPIDLE) == CLIENT_KEEPALIVE_IDLE_S, "idle is %d s", read_option(server, IPPROTO_TCP, TCP_KEEPIDLE));
        CHECK(read_option(server, IPPROTO_TCP, TCP_KEEPINTVL) == CLIENT_KEEPALIVE_INTERVAL_S, "interval is %d s", read_option(server, IPPROTO_TCP, TCP_KEEPINTVL));
        CHECK(read_option(server, IPPROTO_TCP, TCP_KEEPCNT) == CLIENT_KEEPALIVE_PROBES, "probes are %d", read_option(server, IPPROTO_TCP, TCP_KEEPCNT));
        CHECK(read_option(server, IPPROTO_TCP, TCP_NODELAY) == 1, "TCP_NODELAY is on");
        CHECK(info_logs == 1 && strcmp(last_info, "TCP keepalive: idle 5 s, interval 2 s, 3 probes") == 0,
              "effective values logged once: \"%s\"", last_info);
        CHECK(error_logs == 0 && warn_logs == 0, "no warning or error");
        CHECK(CLIENT_KEEPALIVE_IDLE_S + CLIENT_KEEPALIVE_INTERVAL_S * CLIENT_KEEPALIVE_PROBES == 11,
              "a silent client is dropped after %d s, within the 30 s FR-8 limit",
              CLIENT_KEEPALIVE_IDLE_S + CLIENT_KEEPALIVE_INTERVAL_S * CLIENT_KEEPALIVE_PROBES);
        close_pair(server, client);
    }

    printf("K2 a keepalive option that cannot be set rejects the client\n");
    for (size_t i = 0; i < sizeof(REQUIRED) / sizeof(REQUIRED[0]); i++) {
        reset();
        fail_set = (option_id_t){REQUIRED[i].level, REQUIRED[i].option};
        int client, server = accepted_socket(&client);
        esp_err_t err = client_socket_configure(server);
        CHECK(err == ESP_FAIL && error_logs == 1 && strstr(last_error, REQUIRED[i].name) && info_logs == 0,
              "%s fails: rejected, \"%s\"", REQUIRED[i].name, last_error);
        close_pair(server, client);
    }

    printf("K3 a keepalive option that cannot be read back rejects the client\n");
    for (size_t i = 0; i < sizeof(REQUIRED) / sizeof(REQUIRED[0]); i++) {
        reset();
        fail_get = (option_id_t){REQUIRED[i].level, REQUIRED[i].option};
        int client, server = accepted_socket(&client);
        esp_err_t err = client_socket_configure(server);
        CHECK(err == ESP_FAIL && error_logs == 1 && strstr(last_error, "read back") && info_logs == 0,
              "%s unreadable: rejected, \"%s\"", REQUIRED[i].name, last_error);
        close_pair(server, client);
    }

    printf("K4 a keepalive value that reads back different rejects the client\n");
    {
        const struct { int level, option, value; const char *name; } WRONG[] = {
            {SOL_SOCKET, SO_KEEPALIVE, 0, "SO_KEEPALIVE off"},
            {IPPROTO_TCP, TCP_KEEPIDLE, 7200, "lwIP default idle of 7200 s"},
            {IPPROTO_TCP, TCP_KEEPINTVL, 75, "interval of 75 s"},
            {IPPROTO_TCP, TCP_KEEPCNT, 9, "9 probes"},
        };
        for (size_t i = 0; i < sizeof(WRONG) / sizeof(WRONG[0]); i++) {
            reset();
            fake_get = (option_id_t){WRONG[i].level, WRONG[i].option};
            fake_value = WRONG[i].value;
            int client, server = accepted_socket(&client);
            esp_err_t err = client_socket_configure(server);
            CHECK(err == ESP_FAIL && error_logs == 1 && strstr(last_error, "reads back") && info_logs == 0,
                  "%s: rejected, \"%s\"", WRONG[i].name, last_error);
            close_pair(server, client);
        }
    }

    printf("K5 lwIP reads SO_KEEPALIVE back as its flag bit\n");
    {
        reset();
        fake_get = (option_id_t){SOL_SOCKET, SO_KEEPALIVE};
        fake_value = 0x08; /* SOF_KEEPALIVE */
        int client, server = accepted_socket(&client);
        CHECK(client_socket_configure(server) == ESP_OK && error_logs == 0, "0x08 is accepted as on");
        close_pair(server, client);
    }

    printf("K6 TCP_NODELAY failure keeps the previous behavior: warning only\n");
    {
        reset();
        fail_set = (option_id_t){IPPROTO_TCP, TCP_NODELAY};
        int client, server = accepted_socket(&client);
        esp_err_t err = client_socket_configure(server);
        CHECK(err == ESP_OK && warn_logs == 1 && error_logs == 0 && info_logs == 1, "client kept, one warning");
        CHECK(read_option(server, IPPROTO_TCP, TCP_KEEPIDLE) == CLIENT_KEEPALIVE_IDLE_S, "keepalive still applied");
        close_pair(server, client);
    }

    printf("K7 the first failure stops the configuration\n");
    {
        reset();
        fail_set = (option_id_t){SOL_SOCKET, SO_KEEPALIVE};
        int client, server = accepted_socket(&client);
        client_socket_configure(server);
        CHECK(set_calls == 2, "only TCP_NODELAY and SO_KEEPALIVE were attempted (%d calls)", set_calls);
        close_pair(server, client);
    }

    printf("\nRESULT pass=%d fail=%d\n", pass, fail);
    return fail ? 1 : 0;
}
