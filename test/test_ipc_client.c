#include "ipc_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef SYLIXOS
#include <sys/vproc.h>
#endif
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ipc_client.h"

static ipc_client_t *client;

static void on_command_light (struct ipc_client *client, ipc_header_t *vsoa_hdr, ipc_data_ref_t *data, void *arg)
{
    if (vsoa_hdr) {
        printf("On asynchronous RPC reply, data: %.*s\n", (int)data->length, (char*)data->data);
    } else {
        fprintf(stderr, "VSOA server /light reply timeout!\n");
    }
}

int main (int argc, char **argv)
{
    int max_fd, cnt;
    fd_set fds;
    char info[256];
    socklen_t serv_len = sizeof(struct sockaddr_in);
    struct sockaddr_in addr;
    struct timespec timeout = { 1, 0 };

#ifdef SYLIXOS
    vprocExitModeSet(getpid(), LW_VPROC_EXIT_FORCE);
#endif

    client = ipc_client_create(NULL, NULL);
    if (!client) {
        fprintf(stderr, "Can not create VSOA client!\n");
        return  (-1);
    }

    if (!ipc_client_connect(client, "ipc-light_server", NULL)) {
        ipc_client_close(client);
        fprintf(stderr, "Can not connect to VSOA server!\n");
        return  (-1);
    }

    while (1) {
        ipc_client_poll(client, 1000);

        ipc_url_ref_t url;
        url.url     = "/light";
        url.url_len = strlen(url.url);
        int ret = ipc_client_call(client, &url, NULL, on_command_light, NULL, 1000);
        if (ret < 0) {
            fprintf(stderr, "Asynchronous RPC call error (not connected to server)!\n");
        }
    }

    return  (0);
}
