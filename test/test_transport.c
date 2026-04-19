/*
 * Transport Layer Unit Tests
 */

#include "transports/ssn_transport.h"
#include "util/ssn_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_BUFFER_SIZE 1024
#define TEST_TIMEOUT_MS 1000

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg) \
    do { \
        if (cond) { \
            g_tests_passed++; \
            printf("[PASS] %s\n", msg); \
        } else { \
            g_tests_failed++; \
            printf("[FAIL] %s\n", msg); \
        } \
    } while (0)

static void test_address_parse_unix(void)
{
    ssn_address_t addr;
    bool result;

    result = ssn_address_parse("unix:///tmp/test.sock", &addr);
    ASSERT(result == true, "Parse Unix socket address");
    ASSERT(addr.type == SSN_TRANSPORT_UNIX, "Address type is Unix");
}

static void test_address_parse_tcp(void)
{
    ssn_address_t addr;
    bool result;

    result = ssn_address_parse("tcp://127.0.0.1:8080", &addr);
    ASSERT(result == true, "Parse TCP address");
    ASSERT(addr.type == SSN_TRANSPORT_TCP, "Address type is TCP");

    result = ssn_address_parse("tcp://localhost:8080", &addr);
    ASSERT(result == true, "Parse TCP address with hostname");
}

static void test_address_parse_udp(void)
{
    ssn_address_t addr;
    bool result;

    result = ssn_address_parse("udp://127.0.0.1:9090", &addr);
    ASSERT(result == true, "Parse UDP address");
    ASSERT(addr.type == SSN_TRANSPORT_UDP, "Address type is UDP");
}

static void test_address_to_string(void)
{
    ssn_address_t addr;
    char buffer[256];
    bool result;

    ssn_address_parse("tcp://127.0.0.1:8080", &addr);
    result = ssn_address_to_string(&addr, buffer, sizeof(buffer));
    ASSERT(result == true, "Convert address to string");
    ASSERT(strstr(buffer, "tcp://") != NULL, "String contains tcp://");
}

static void test_transport_type_string(void)
{
    const char* str;

    str = ssn_transport_type_to_string(SSN_TRANSPORT_UNIX);
    ASSERT(strcmp(str, "unix") == 0, "Unix type to string");

    str = ssn_transport_type_to_string(SSN_TRANSPORT_TCP);
    ASSERT(strcmp(str, "tcp") == 0, "TCP type to string");

    str = ssn_transport_type_to_string(SSN_TRANSPORT_UDP);
    ASSERT(strcmp(str, "udp") == 0, "UDP type to string");

    ssn_transport_type_t type = ssn_transport_type_from_string("tcp");
    ASSERT(type == SSN_TRANSPORT_TCP, "String to TCP type");
}

static void test_unix_transport_create_destroy(void)
{
    ssn_transport_config_t config = {
        .type = SSN_TRANSPORT_UNIX,
        .non_blocking = false,
        .send_timeout_ms = 5000,
        .recv_timeout_ms = 5000,
        .connect_timeout_ms = 5000,
        .enable_keepalive = false,
        .keepalive_idle_sec = 60,
        .keepalive_interval_sec = 10,
        .keepalive_count = 3,
        .enable_nagle = false,
        .send_buffer_size = 65536,
        .recv_buffer_size = 65536,
        .reuse_address = true
    };

    ssn_transport_t* transport = ssn_transport_create(SSN_TRANSPORT_UNIX, &config);
    ASSERT(transport != NULL, "Create Unix transport");
    ASSERT(transport->valid == true, "Transport is valid");

    if (transport) {
        ssn_transport_destroy(transport);
    }
}

static void test_tcp_transport_create_destroy(void)
{
    ssn_transport_config_t config = {
        .type = SSN_TRANSPORT_TCP,
        .non_blocking = true,
        .send_timeout_ms = 5000,
        .recv_timeout_ms = 5000,
        .connect_timeout_ms = 5000,
        .enable_keepalive = true,
        .keepalive_idle_sec = 60,
        .keepalive_interval_sec = 10,
        .keepalive_count = 3,
        .enable_nagle = false,
        .send_buffer_size = 65536,
        .recv_buffer_size = 65536,
        .reuse_address = true
    };

    ssn_transport_t* transport = ssn_transport_create(SSN_TRANSPORT_TCP, &config);
    ASSERT(transport != NULL, "Create TCP transport");
    ASSERT(transport->valid == true, "Transport is valid");

    if (transport) {
        ssn_transport_destroy(transport);
    }
}

static void test_udp_transport_create_destroy(void)
{
    ssn_transport_config_t config = {
        .type = SSN_TRANSPORT_UDP,
        .non_blocking = false,
        .send_timeout_ms = 5000,
        .recv_timeout_ms = 5000,
        .connect_timeout_ms = 5000,
        .enable_keepalive = false,
        .keepalive_idle_sec = 60,
        .keepalive_interval_sec = 10,
        .keepalive_count = 3,
        .enable_nagle = false,
        .send_buffer_size = 65536,
        .recv_buffer_size = 65536,
        .reuse_address = true
    };

    ssn_transport_t* transport = ssn_transport_create(SSN_TRANSPORT_UDP, &config);
    ASSERT(transport != NULL, "Create UDP transport");
    ASSERT(transport->valid == true, "Transport is valid");

    if (transport) {
        ssn_transport_destroy(transport);
    }
}

static void test_unix_server_listen(void)
{
    ssn_transport_config_t config = {
        .type = SSN_TRANSPORT_UNIX,
        .non_blocking = false,
        .reuse_address = true
    };

    ssn_transport_t* transport = ssn_transport_create(SSN_TRANSPORT_UNIX, &config);
    ASSERT(transport != NULL, "Create Unix transport for server");

    if (transport) {
        ssn_address_t addr;
        ssn_address_parse("unix:///tmp/test_server.sock", &addr);

        ssn_transport_bind(transport, &addr);
        bool result = ssn_transport_listen(transport, 10);
        ASSERT(result == true, "Unix server listen");

        if (result) {
            ssn_transport_stats_t stats;
            result = ssn_transport_get_stats(transport, &stats);
            ASSERT(result == true, "Get transport stats");
        }

        ssn_transport_destroy(transport);
        unlink("/tmp/test_server.sock");
    }
}

static void test_unix_client_server(void)
{
    ssn_transport_config_t server_config = {
        .type = SSN_TRANSPORT_UNIX,
        .non_blocking = false
    };

    ssn_transport_t* server = ssn_transport_create(SSN_TRANSPORT_UNIX, &server_config);
    ASSERT(server != NULL, "Create Unix server transport");

    if (server) {
        ssn_address_t server_addr;
        ssn_address_parse("unix:///tmp/test_pipe.sock", &server_addr);

        ssn_transport_bind(server, &server_addr);
        bool result = ssn_transport_listen(server, 5);
        ASSERT(result == true, "Server listen");

        if (result) {
            ssn_transport_config_t client_config = {
                .type = SSN_TRANSPORT_UNIX,
                .non_blocking = false
            };

            ssn_transport_t* client = ssn_transport_create(SSN_TRANSPORT_UNIX, &client_config);
            ASSERT(client != NULL, "Create Unix client transport");

            if (client) {
                result = ssn_transport_connect(client, &server_addr, 1000);
                ASSERT(result == true, "Client connect to server");

                if (result) {
                    char send_buffer[TEST_BUFFER_SIZE] = "Hello, Unix Socket!";
                    char recv_buffer[TEST_BUFFER_SIZE];

                    ssn_address_t client_addr;
                    ssn_transport_t* accepted = ssn_transport_accept(server,
                                                                    &client_addr,
                                                                    1000);
                    ASSERT(accepted != NULL, "Server accept client");

                    if (accepted) {
                        usleep(10000);

                        int sent = ssn_transport_send(client, send_buffer,
                                                     strlen(send_buffer));
                        ASSERT(sent > 0, "Client send data");

                        int received = ssn_transport_recv(accepted,
                                                         recv_buffer,
                                                         sizeof(recv_buffer),
                                                         1000);
                        ASSERT(received > 0, "Server receive data");
                        ASSERT(received == (int)strlen(send_buffer),
                               "Data length matches");
                        recv_buffer[received] = '\0';
                        ASSERT(strncmp(send_buffer, recv_buffer,
                                     strlen(send_buffer)) == 0,
                               "Data content matches");

                        ssn_transport_destroy(accepted);
                    }

                    ssn_transport_disconnect(client);
                }

                ssn_transport_destroy(client);
            }
        }

        ssn_transport_destroy(server);
        unlink("/tmp/test_pipe.sock");
    }
}

static void test_tcp_client_server(void)
{
    ssn_transport_config_t server_config = {
        .type = SSN_TRANSPORT_TCP,
        .non_blocking = false,
        .reuse_address = true
    };

    ssn_transport_t* server = ssn_transport_create(SSN_TRANSPORT_TCP, &server_config);
    ASSERT(server != NULL, "Create TCP server transport");

    if (server) {
        ssn_address_t server_addr;
        ssn_address_parse("tcp://127.0.0.1:9999", &server_addr);

        ssn_transport_bind(server, &server_addr);
        bool result = ssn_transport_listen(server, 5);
        ASSERT(result == true, "TCP server listen");

        if (result) {
            ssn_transport_config_t client_config = {
                .type = SSN_TRANSPORT_TCP,
                .non_blocking = false
            };

            ssn_transport_t* client = ssn_transport_create(SSN_TRANSPORT_TCP, &client_config);
            ASSERT(client != NULL, "Create TCP client transport");

            if (client) {
                result = ssn_transport_connect(client, &server_addr, 1000);
                ASSERT(result == true, "TCP client connect to server");

                if (result) {
                    char send_buffer[TEST_BUFFER_SIZE] = "Hello, TCP!";
                    char recv_buffer[TEST_BUFFER_SIZE];

                    ssn_address_t client_addr;
                    ssn_transport_t* accepted = ssn_transport_accept(server,
                                                                    &client_addr,
                                                                    1000);
                    ASSERT(accepted != NULL, "TCP server accept client");

                    if (accepted) {
                        usleep(10000);

                        int sent = ssn_transport_send(client, send_buffer,
                                                     strlen(send_buffer));
                        ASSERT(sent > 0, "TCP client send data");

                        int received = ssn_transport_recv(accepted,
                                                         recv_buffer,
                                                         sizeof(recv_buffer),
                                                         1000);
                        ASSERT(received > 0, "TCP server receive data");
                        ASSERT(received == (int)strlen(send_buffer),
                               "TCP data length matches");
                        recv_buffer[received] = '\0';
                        ASSERT(strncmp(send_buffer, recv_buffer,
                                     strlen(send_buffer)) == 0,
                               "TCP data content matches");

                        ssn_transport_destroy(accepted);
                    }

                    ssn_transport_disconnect(client);
                }

                ssn_transport_destroy(client);
            }
        }

        ssn_transport_destroy(server);
    }
}

static void test_udp_client_server(void)
{
    ssn_transport_config_t server_config = {
        .type = SSN_TRANSPORT_UDP,
        .non_blocking = false,
        .reuse_address = true
    };

    ssn_transport_t* server = ssn_transport_create(SSN_TRANSPORT_UDP, &server_config);
    ASSERT(server != NULL, "Create UDP server transport");

    if (server) {
        ssn_address_t server_addr;
        ssn_address_parse("udp://127.0.0.1:9998", &server_addr);

        bool result = ssn_transport_bind(server, &server_addr);
        ASSERT(result == true, "UDP server bind");

        // UDP服务器需要调用listen来实际绑定地址
        result = ssn_transport_listen(server, 5);
        ASSERT(result == true, "UDP server listen");

        if (result) {
            ssn_transport_config_t client_config = {
                .type = SSN_TRANSPORT_UDP,
                .non_blocking = false
            };

            ssn_transport_t* client = ssn_transport_create(SSN_TRANSPORT_UDP, &client_config);
            ASSERT(client != NULL, "Create UDP client transport");

            if (client) {
                char send_buffer[TEST_BUFFER_SIZE] = "Hello, UDP!";
                char recv_buffer[TEST_BUFFER_SIZE];

                // UDP需要先连接
                result = ssn_transport_connect(client, &server_addr, 1000);
                ASSERT(result == true, "UDP client connect");

                if (result) {
                    int sent = ssn_transport_send(client, send_buffer,
                                                strlen(send_buffer));
                    ASSERT(sent > 0, "UDP client send data");

                    // 给服务器一点时间处理
                    usleep(10000);

                    int received = ssn_transport_recv(server,
                                                    recv_buffer,
                                                    sizeof(recv_buffer),
                                                    1000);
                    ASSERT(received > 0, "UDP server receive data");
                    ASSERT(received == (int)strlen(send_buffer),
                           "UDP data length matches");
                    recv_buffer[received] = '\0';
                    ASSERT(strncmp(send_buffer, recv_buffer,
                                 strlen(send_buffer)) == 0,
                           "UDP data content matches");

                    ssn_transport_disconnect(client);
                }

                ssn_transport_destroy(client);
            }
        }

        ssn_transport_destroy(server);
    }
}

static void test_transport_factory(void)
{
    // 测试传输类型转换
    const char* unix_str = ssn_transport_type_to_string(SSN_TRANSPORT_UNIX);
    ASSERT(unix_str != NULL, "Unix type to string");

    const char* tcp_str = ssn_transport_type_to_string(SSN_TRANSPORT_TCP);
    ASSERT(tcp_str != NULL, "TCP type to string");

    const char* udp_str = ssn_transport_type_to_string(SSN_TRANSPORT_UDP);
    ASSERT(udp_str != NULL, "UDP type to string");

    // 测试字符串转传输类型
    ssn_transport_type_t unix_type = ssn_transport_type_from_string("unix");
    ASSERT(unix_type == SSN_TRANSPORT_UNIX, "String to Unix type");

    ssn_transport_type_t tcp_type = ssn_transport_type_from_string("tcp");
    ASSERT(tcp_type == SSN_TRANSPORT_TCP, "String to TCP type");

    ssn_transport_type_t udp_type = ssn_transport_type_from_string("udp");
    ASSERT(udp_type == SSN_TRANSPORT_UDP, "String to UDP type");
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    printf("========================================\n");
    printf("SSN Transport Layer Unit Tests\n");
    printf("========================================\n\n");

    printf("Running address parsing tests...\n");
    test_address_parse_unix();
    test_address_parse_tcp();
    test_address_parse_udp();
    test_address_to_string();
    test_transport_type_string();

    printf("\nRunning transport creation tests...\n");
    test_unix_transport_create_destroy();
    test_tcp_transport_create_destroy();
    test_udp_transport_create_destroy();

    printf("\nRunning Unix socket tests...\n");
    test_unix_server_listen();
    test_unix_client_server();

    printf("\nRunning TCP tests...\n");
    test_tcp_client_server();

    printf("\nRunning UDP tests...\n");
    test_udp_client_server();

    printf("\nRunning factory tests...\n");
    test_transport_factory();

    printf("\n========================================\n");
    printf("Test Results: %d passed, %d failed\n",
           g_tests_passed, g_tests_failed);
    printf("========================================\n");

    return g_tests_failed > 0 ? 1 : 0;
}

