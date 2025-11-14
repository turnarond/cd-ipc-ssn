#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif
#include <errno.h>
#ifdef SYLIXOS
#include <sys/vproc.h>
#endif
#include "ipc_cliauto.h"

#ifdef _MSC_VER
#define SLEEP(ms) Sleep(ms)
#else
#define SLEEP(ms) usleep((ms) * 1000)
#endif

/* My server password */
#define MY_SERVER_PASSWD "123456"

/* My client */
static ipc_client_t *client;

/* My client auto */
static ipc_client_auto_t *cliauto;

/* If client is connected */
static bool connected = false;

static void onconnect (void *arg, ipc_client_auto_t *cliauto, bool connect)
{
    printf("On connect, connect: %s\n", 
            (connect == true) ? "connected" : "disconnected");
    connected = connect;
}

static void on_command_light (void *arg, struct ipc_client *client, ipc_header_t *ipc_hdr, ipc_payload_t *payload)
{
    if (ipc_hdr) {
        printf("On asynchronous RPC reply, payload: %.*s\n", (int)payload->data_len, (char*)payload->data);
    } else {
        fprintf(stderr, "IPC server /light reply timeout!\n");
    }
}

/*
 * main function
 */
int main (int argc, char **argv)
{
    bool ret;
    ipc_url_t url;

#ifdef SYLIXOS
    vprocExitModeSet(getpid(), LW_VPROC_EXIT_FORCE);
#endif

    /*
     * Create client auto robot
     */
    cliauto = ipc_client_auto_create(NULL, NULL);
    client  = ipc_client_auto_handle(cliauto);

    if (!ipc_client_auto_setup(cliauto, onconnect, NULL)) {
        ipc_client_auto_delete(cliauto);
        fprintf(stderr, "Cannot register connect callback: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    /*
     * Client auto robot start
     * The robot will automatically connect to the specified server and maintain the connection.
     * At this time, the developer only needs to focus on the business.
     */
    ipc_client_auto_start(cliauto, "./ipc-light_server", 
                           NULL, 0, 1000, 1000, 1000);

    while (true) {
        /*
         * Wait for connection established
         */
        if (!connected) {
            fprintf(stderr, "Cannot connected to server now!\n");
            SLEEP(1);
            continue;
        }

        /*
         * Developers can still use asynchronous RPC calls, 
         * and the server response callback function will be called in the client auto robot event loop thread context.
         */
        url.url     = "/light";
        url.url_len = strlen(url.url);
        ret = ipc_client_call(client, &url, NULL, on_command_light, NULL, NULL);
        if (!ret) {
            fprintf(stderr, "Asynchronous RPC call error (not connected to server)!\n");
        }

        SLEEP(1000);
    }
}
