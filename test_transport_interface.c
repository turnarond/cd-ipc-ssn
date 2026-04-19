/*
 * Test transport interface integration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "cd_ipc_client.h"
#include "cd_ipc_server.h"
#include "transports/ssn_transport.h"

#define TEST_SERVER_NAME "/tmp/test_ipc_server"
#define TEST_SERVER_URL "unix:///tmp/test_ipc_server"

/* Test callback functions */
static void test_message_handler(ipc_client_t *client, ipc_url_ref_t *url, ipc_data_ref_t *data, void *arg)
{
    printf("Client received message: url=%.*s, data=%.*s\n", 
           (int)url->url_len, url->url, 
           (int)data->length, (char*)data->data);
}

// Test connect handler for client auto (not used in this test)
// static void test_connect_handler(void *arg, ipc_client_auto_t *cliauto, bool connect)
// {
//     printf("Client auto connect status: %s\n", connect ? "connected" : "disconnected");
// }

static void test_server_message_handler(ipc_server_t *server, cli_id_t id, ipc_url_ref_t *url, ipc_data_ref_t *data, void *arg)
{
    printf("Server received message from client %d: url=%.*s, data=%.*s\n", 
           id, (int)url->url_len, url->url, 
           (int)data->length, (char*)data->data);
}

static void test_server_connect_handler(ipc_server_t *server, cli_id_t id, bool connect, void *arg)
{
    printf("Server client %d %s\n", id, connect ? "connected" : "disconnected");
}

static void test_rpc_handler(ipc_server_t *server, cli_id_t id, ipc_header_t *ipc_hdr, 
                            ipc_url_ref_t *url, ipc_data_ref_t *data, void *arg)
{
    printf("Server received RPC request from client %d: url=%.*s, data=%.*s\n", 
           id, (int)url->url_len, url->url, 
           (int)data->length, (char*)data->data);
    
    // Send response
    ipc_data_ref_t response;
    response.data = "RPC response"; 
    response.length = strlen("RPC response");
    ipc_server_response(server, id, 0, ipc_get_seqno(ipc_hdr), &response);
}

static void test_rpc_reply_handler(ipc_client_t *client, ipc_header_t *ipc_hdr, ipc_data_ref_t *data, void *arg)
{
    if (data) {
        printf("Client received RPC reply: data=%.*s\n", 
               (int)data->length, (char*)data->data);
    } else {
        printf("Client received RPC timeout\n");
    }
}

static void test_result_handler(ipc_client_t *client, bool success, void *arg)
{
    printf("Client operation result: %s\n", success ? "success" : "failure");
}

/* Server thread function */
static void *server_thread(void *arg)
{
    ipc_server_t *server = (ipc_server_t *)arg;
    printf("Server thread started\n");
    ipc_server_run(server);
    printf("Server thread exited\n");
    return NULL;
}

int main()
{
    int ret;
    ipc_server_t *server;
    ipc_client_t *client;
    struct timespec timeout = {1, 0};
    
    printf("=== Testing transport interface integration ===\n");
    
    // Clean up previous test socket
    unlink(TEST_SERVER_NAME);
    
    /* Test 1: Server creation and start */
    printf("\nTest 1: Server creation and start\n");
    server = ipc_server_create(TEST_SERVER_NAME);
    if (!server) {
        printf("Failed to create server\n");
        return 1;
    }
    
    // Set server callbacks
    ipc_server_set_message_handler(server, test_server_message_handler, NULL);
    ipc_server_set_connect_handler(server, test_server_connect_handler, NULL);
    
    // Add RPC method
    ipc_url_ref_t rpc_url = {"/test/rpc", strlen("/test/rpc")};
    ipc_server_add_method(server, &rpc_url, test_rpc_handler, NULL);
    
    if (!ipc_server_start(server)) {
        printf("Failed to start server\n");
        ipc_server_destroy(server);
        return 1;
    }
    printf("Server started successfully\n");
    
    // Start server thread
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, server_thread, server) != 0) {
        printf("Failed to create server thread\n");
        ipc_server_destroy(server);
        return 1;
    }
    
    // Wait a bit for server to be ready
    usleep(100000); // 100ms
    
    /* Test 2: Client creation and connection */
    printf("\nTest 2: Client creation and connection\n");
    client = ipc_client_create(test_message_handler, NULL);
    if (!client) {
        printf("Failed to create client\n");
        ipc_server_destroy(server);
        return 1;
    }
    
    if (!ipc_client_connect(client, TEST_SERVER_URL, &timeout)) {
        printf("Failed to connect client\n");
        ipc_client_close(client);
        ipc_server_destroy(server);
        return 1;
    }
    printf("Client connected successfully\n");
    
    /* Test 3: Send message from client to server */
    printf("\nTest 3: Send message from client to server\n");
    ipc_url_ref_t msg_url = {"/test/message", strlen("/test/message")};
    ipc_data_ref_t msg_data = {"Hello from client", strlen("Hello from client")};
    ret = ipc_client_message(client, &msg_url, &msg_data);
    if (ret != 0) {
        printf("Failed to send message\n");
    } else {
        printf("Message sent successfully\n");
    }
    
    /* Test 4: RPC call */
    printf("\nTest 4: RPC call\n");
    ipc_data_ref_t rpc_data = {"RPC request", strlen("RPC request")};
    ret = ipc_client_call(client, &rpc_url, &rpc_data, test_rpc_reply_handler, NULL, 1000);
    if (ret != 0) {
        printf("Failed to send RPC request\n");
    } else {
        printf("RPC request sent successfully\n");
    }
    
    /* Test 5: Subscribe to URL */
    printf("\nTest 5: Subscribe to URL\n");
    ipc_url_ref_t sub_url = {"/test/subscribe", strlen("/test/subscribe")};
    ret = ipc_client_subscribe(client, &sub_url, test_result_handler, NULL, 1000);
    if (!ret) {
        printf("Failed to subscribe\n");
    } else {
        printf("Subscribe successful\n");
    }
    
    /* Test 6: Publish from server */
    printf("\nTest 6: Publish from server\n");
    ipc_data_ref_t pub_data = {"Published message", strlen("Published message")};
    ret = ipc_server_publish(server, &sub_url, &pub_data);
    if (!ret) {
        printf("Failed to publish\n");
    } else {
        printf("Publish successful\n");
    }
    
    /* Test 7: Unsubscribe from URL */
    printf("\nTest 7: Unsubscribe from URL\n");
    ret = ipc_client_unsubscribe(client, &sub_url, test_result_handler, NULL, 1000);
    if (!ret) {
        printf("Failed to unsubscribe\n");
    } else {
        printf("Unsubscribe successful\n");
    }
    
    /* Test 8: Client disconnect */
    printf("\nTest 8: Client disconnect\n");
    if (!ipc_client_disconnect(client)) {
        printf("Failed to disconnect client\n");
    } else {
        printf("Client disconnected successfully\n");
    }
    
    /* Clean up */
    printf("\nCleaning up...\n");
    ipc_client_close(client);
    
    // Wait for server thread to exit
    pthread_cancel(thread_id);
    pthread_join(thread_id, NULL);
    
    // Clean up server
    ipc_server_destroy(server);
    
    printf("\n=== All tests completed ===\n");
    
    return 0;
}
