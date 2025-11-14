/*
 * Application: light_server
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ipc_server.h"
#include "ipc_platform.h"

#define IPC_AUTO_PORT_ENV  "VSOA_AUTO_PORT="
#define SERVER_DEFAULT_PORT 3001

static ipc_server_t *server;

/*
* /light Callback
*/
static void command_light (void *arg, ipc_server_t *server, ipc_cli_id_t cid,
                           ipc_header_t *ipc_hdr, ipc_url_t *url,
                           ipc_payload_t *payload)
{
    ipc_payload_t send;
    uint16_t seqno = ipc_parser_get_seqno(ipc_hdr);

    send.data      = NULL;
    send.data_len  = 0;
    printf("in command_light\r\n");
    ipc_server_cli_reply(server, cid, 0, seqno, &send);
}

int main (int argc, char **argv)
{
    struct sockaddr_in addr;
    uint16_t server_port = SERVER_DEFAULT_PORT;

    char *autoPort = getenv(IPC_AUTO_PORT_ENV);
    if (autoPort != NULL) {
        fprintf(stdout, "ser port is %s .\n", autoPort);

        server_port = atoi(autoPort);
        if (server_port == 0) {
            server_port = SERVER_DEFAULT_PORT;
        }
    }

    bzero(&addr, sizeof(struct sockaddr_in));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(server_port);
    addr.sin_addr.s_addr = INADDR_ANY;

#ifdef IPC_HAS_SIN_LEN
    addr.sin_len = sizeof(struct sockaddr_in);
#endif

    /*
    * Initialize server
    */
    server = ipc_server_create("light_server");
    if (!server) {
        fprintf(stderr, "Can not create IPC server!\n");
        return  (-1);
    }

    /*
    * Add /light listener
    */
    ipc_url_t url;
    url.url     = "/light";
    url.url_len = strlen(url.url);
    ipc_server_add_listener(server, &url, command_light, NULL);

    /*
    * Start server
    */
    if (!ipc_server_start(server, "./ipc-light_server")) {
        fprintf(stderr, "Can not start IPC server! errno is %d\n", errno);
        ipc_server_close(server);
        return  (-1);
    }

    int cnt, max_fd;
    fd_set fds;
    struct timespec timeout = { 1, 0 };
    while (1) {
        FD_ZERO(&fds);
        max_fd = ipc_server_fds(server, &fds);

        cnt = pselect(max_fd + 1, &fds, NULL, NULL, &timeout, NULL);
        if (cnt > 0) {
            ipc_server_input_fds(server, &fds);
        }
    }

    return (0);
}
