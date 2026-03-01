/*
 * Comprehensive IPC test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "ipc_server.h"
#include "ipc_client.h"
#include "ipc_error.h"

#define SERVER_NAME "ipc-test-server"
#define TEST_TIMEOUT 5000

static ipc_server_t *server;
static ipc_client_t *client;
static pthread_t server_thread;
static volatile bool server_running = false;

/* Test 1: Basic RPC functionality */
static void test_rpc_handler(ipc_server_t *server, cli_id_t cid, 
                           ipc_header_t *ipc_hdr, ipc_url_ref_t *url, 
                           ipc_data_ref_t *data, void *arg)
{
    ipc_data_ref_t send;
    uint16_t seqno = ipc_get_seqno(ipc_hdr);
    
    printf("Test 1: RPC handler called with URL: %.*s\n", (int)url->url_len, url->url);
    
    if (data && data->length > 0) {
        printf("Test 1: Received data: %.*s\n", (int)data->length, (char*)data->data);
    }
    
    send.data = "RPC response";
    send.length = strlen((char*)send.data);
    ipc_server_response(server, cid, IPC_ERR_SUCCESS, seqno, &send);
}

/* Test 2: Publish/Subscribe functionality */
static void test_publish_handler(ipc_client_t *client, ipc_url_ref_t *url, 
                               ipc_data_ref_t *data, void *arg)
{
    printf("Test 2: Publish received: URL=%.*s, Data=%.*s\n", 
           (int)url->url_len, url->url, 
           (int)data->length, (char*)data->data);
}

/* Test 3: Error handling */
static void test_error_handler(ipc_server_t *server, cli_id_t cid, 
                             ipc_header_t *ipc_hdr, ipc_url_ref_t *url, 
                             ipc_data_ref_t *data, void *arg)
{
    ipc_data_ref_t send;
    uint16_t seqno = ipc_get_seqno(ipc_hdr);
    
    send.data = "Error response";
    send.length = strlen((char*)send.data);
    ipc_server_response(server, cid, IPC_ERR_INVALID_ARGS, seqno, &send);
}

/* Test 4: Large message handling */
static void test_large_message_handler(ipc_server_t *server, cli_id_t cid, 
                                     ipc_header_t *ipc_hdr, ipc_url_ref_t *url, 
                                     ipc_data_ref_t *data, void *arg)
{
    ipc_data_ref_t send;
    uint16_t seqno = ipc_get_seqno(ipc_hdr);
    
    printf("Test 4: Large message received, length: %zu\n", data->length);
    
    // Echo back the large message
    send.data = data->data;
    send.length = data->length;
    ipc_server_response(server, cid, IPC_ERR_SUCCESS, seqno, &send);
}

/* Test 5: Timeout handling */
static void test_timeout_handler(ipc_client_t *client, ipc_header_t *ipc_hdr, 
                               ipc_data_ref_t *data, void *arg)
{
    if (ipc_hdr) {
        printf("Test 5: Timeout test - unexpected response received\n");
    } else {
        printf("Test 5: Timeout test - expected timeout occurred\n");
    }
}

/* Server thread function */
static void *server_thread_func(void *arg)
{
    printf("Starting server thread\n");
    
    // Create server
    server = ipc_server_create(SERVER_NAME);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return NULL;
    }
    
    // Add RPC handlers
    ipc_url_ref_t url1, url2, url3, url4;
    
    url1.url = "/test/rpc";
    url1.url_len = strlen(url1.url);
    ipc_server_add_method(server, &url1, test_rpc_handler, NULL);
    
    url2.url = "/test/error";
    url2.url_len = strlen(url2.url);
    ipc_server_add_method(server, &url2, test_error_handler, NULL);
    
    url3.url = "/test/large";
    url3.url_len = strlen(url3.url);
    ipc_server_add_method(server, &url3, test_large_message_handler, NULL);
    
    // Start server
    if (!ipc_server_start(server)) {
        fprintf(stderr, "Failed to start server\n");
        ipc_server_destroy(server);
        return NULL;
    }
    
    server_running = true;
    
    // Server loop
    while (server_running) {
        ipc_server_poll(server, 1000);
    }
    
    // Cleanup
    ipc_server_destroy(server);
    printf("Server thread exited\n");
    return NULL;
}

/* Test 1: Basic RPC */
static void test_basic_rpc(void)
{
    printf("\n=== Test 1: Basic RPC ===\n");
    
    ipc_url_ref_t url;
    ipc_data_ref_t data;
    
    url.url = "/test/rpc";
    url.url_len = strlen(url.url);
    
    data.data = "Hello from client";
    data.length = strlen((char*)data.data);
    
    int ret = ipc_client_call(client, &url, &data, test_timeout_handler, NULL, TEST_TIMEOUT);
    if (ret < 0) {
        fprintf(stderr, "RPC call failed\n");
    }
    
    // Wait for response
    sleep(1);
}

/* Test 2: Publish/Subscribe */
static void test_publish_subscribe(void)
{
    printf("\n=== Test 2: Publish/Subscribe ===\n");
    
    // Subscribe to topic
    ipc_url_ref_t url;
    url.url = "/test/publish";
    url.url_len = strlen(url.url);
    
    bool ret = ipc_client_subscribe(client, &url, NULL, NULL, TEST_TIMEOUT);
    if (!ret) {
        fprintf(stderr, "Subscribe failed\n");
        return;
    }
    
    // Server publishes message
    ipc_data_ref_t publish_data;
    publish_data.data = "Published message";
    publish_data.length = strlen((char*)publish_data.data);
    
    ipc_server_publish(server, &url, &publish_data);
    
    // Wait for publish
    sleep(1);
    
    // Unsubscribe
    ret = ipc_client_unsubscribe(client, &url, NULL, NULL, TEST_TIMEOUT);
    if (!ret) {
        fprintf(stderr, "Unsubscribe failed\n");
    }
}

/* Test 3: Error handling */
static void test_error_handling(void)
{
    printf("\n=== Test 3: Error handling ===\n");
    
    ipc_url_ref_t url;
    url.url = "/test/error";
    url.url_len = strlen(url.url);
    
    int ret = ipc_client_call(client, &url, NULL, test_timeout_handler, NULL, TEST_TIMEOUT);
    if (ret < 0) {
        fprintf(stderr, "RPC call failed\n");
    }
    
    // Wait for response
    sleep(1);
}

/* Test 4: Large message */
static void test_large_message(void)
{
    printf("\n=== Test 4: Large message ===\n");
    
    ipc_url_ref_t url;
    ipc_data_ref_t data;
    
    url.url = "/test/large";
    url.url_len = strlen(url.url);
    
    // Create a large message
    char *large_data = malloc(10000);
    if (!large_data) {
        fprintf(stderr, "Failed to allocate large data\n");
        return;
    }
    memset(large_data, 'A', 9999);
    large_data[9999] = '\0';
    
    data.data = large_data;
    data.length = strlen(large_data);
    
    int ret = ipc_client_call(client, &url, &data, test_timeout_handler, NULL, TEST_TIMEOUT);
    if (ret < 0) {
        fprintf(stderr, "Large message RPC call failed\n");
    }
    
    // Wait for response
    sleep(1);
    
    free(large_data);
}

/* Test 5: Timeout handling */
static void test_timeout(void)
{
    printf("\n=== Test 5: Timeout handling ===\n");
    
    // Use a non-existent URL to trigger timeout
    ipc_url_ref_t url;
    url.url = "/test/non-existent";
    url.url_len = strlen(url.url);
    
    int ret = ipc_client_call(client, &url, NULL, test_timeout_handler, NULL, 500); // Short timeout
    if (ret < 0) {
        fprintf(stderr, "RPC call failed\n");
    }
    
    // Wait for timeout
    sleep(1);
}

/* Test 6: Invalid URL */
static void test_invalid_url(void)
{
    printf("\n=== Test 6: Invalid URL ===\n");
    
    // Use an invalid URL (missing leading slash)
    ipc_url_ref_t url;
    url.url = "test/invalid";
    url.url_len = strlen(url.url);
    
    int ret = ipc_client_call(client, &url, NULL, test_timeout_handler, NULL, TEST_TIMEOUT);
    if (ret < 0) {
        printf("Test 6: Invalid URL test - expected failure occurred\n");
    }
    
    sleep(1);
}

int main(int argc, char **argv)
{
    printf("=== Comprehensive IPC Test ===\n");
    
    // Start server thread
    if (pthread_create(&server_thread, NULL, server_thread_func, NULL) != 0) {
        fprintf(stderr, "Failed to create server thread\n");
        return -1;
    }
    
    // Wait for server to start
    sleep(1);
    
    // Create client
    client = ipc_client_create(test_publish_handler, NULL);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        server_running = false;
        pthread_join(server_thread, NULL);
        return -1;
    }
    
    // Connect to server
    if (!ipc_client_connect(client, SERVER_NAME, NULL)) {
        fprintf(stderr, "Failed to connect to server\n");
        ipc_client_close(client);
        server_running = false;
        pthread_join(server_thread, NULL);
        return -1;
    }
    
    printf("Connected to server\n");
    
    // Run tests
    test_basic_rpc();
    test_publish_subscribe();
    test_error_handling();
    test_large_message();
    test_timeout();
    test_invalid_url();
    
    // Cleanup
    ipc_client_close(client);
    server_running = false;
    pthread_join(server_thread, NULL);
    
    printf("\n=== All tests completed ===\n");
    return 0;
}
