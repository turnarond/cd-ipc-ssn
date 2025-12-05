/*
 * Application: light_server
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ipc_server.h"
#include "vsi/ipc_platform.h"

#include <errno.h>

#define SERVER_DEFAULT_PORT 3001

static ipc_server_t *server;

/*
* /light Callback
*/
static void command_light (ipc_server_t *server, cli_id_t cid,
                           ipc_header_t *ipc_hdr, ipc_url_ref_t *url,
                           ipc_payload_ref_t *payload, void *arg)
{
    ipc_payload_ref_t send;
    uint16_t seqno = ipc_get_seqno(ipc_hdr);

    send.data      = "command light";
    send.length  = 13;
    printf("in command_light\r\n");
    ipc_server_response(server, cid, 0, seqno, &send);
}

int main (int argc, char **argv)
{
    struct sockaddr_in addr;
    uint16_t server_port = SERVER_DEFAULT_PORT;

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
    server = ipc_server_create("ipc-light_server");
    if (!server) {
        fprintf(stderr, "Can not create IPC server!\n");
        return  (-1);
    }

    /*
    * Add /light listener
    */
    ipc_url_ref_t url;
    url.url     = "/light";
    url.url_len = strlen(url.url);
    ipc_server_add_method(server, &url, command_light, NULL);

    /*
    * Start server
    */
    if (!ipc_server_start(server)) {
        fprintf(stderr, "Can not start IPC server! errno is %d\n", errno);
        ipc_server_destroy(server);
        return  (-1);
    }

    while (1) {
        ipc_server_poll(server, 10000);
    }

    return (0);
}
