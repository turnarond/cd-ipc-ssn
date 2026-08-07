/*
 * node_pubsub.c - Node publish/subscribe example
 *
 * This example demonstrates how to use publish/subscribe functionality between nodes in the node abstraction layer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "node/ssn_node.h"
#include "util/ssn_log.h"

#define PUBLISHER_ADDRESS "tcp://127.0.0.1:8892"

static int g_news_messages_received = 0;
static int g_weather_messages_received = 0;

static volatile int g_server_running = 0;

/**
 * @brief Background server poller
 *
 * Runs ssn_node_poll in a thread so the publisher node can accept
 * connections and handle the subscriber handshake while clients connect.
 */
static void *server_poll_thread(void *arg)
{
    ssn_node_t *node = (ssn_node_t *)arg;
    while (g_server_running) {
        ssn_node_poll(node, 100);
    }
    return NULL;
}

/**
 * @brief Subscriber1 message handler (subscribed to /news)
 * 
 * @param client IPC client instance
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void subscriber1_message_handler(ssn_client_t *client, ssn_url_ref_t *url, 
                                      ssn_data_ref_t *data, void *arg)
{
    (void)client;
    (void)arg;

    LOG_INFO("Subscriber1 received message on %.*s: %.*s",
             (int)url->url_len, url->url, (int)data->length, (const char*)data->data);

    g_news_messages_received++;
}

/**
 * @brief Subscriber2 message handler (subscribed to /weather)
 * 
 * @param client IPC client instance
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void subscriber2_message_handler(ssn_client_t *client, ssn_url_ref_t *url, 
                                      ssn_data_ref_t *data, void *arg)
{
    (void)client;
    (void)arg;

    LOG_INFO("Subscriber2 received message on %.*s: %.*s",
             (int)url->url_len, url->url, (int)data->length, (const char*)data->data);

    g_weather_messages_received++;
}

/**
 * @brief Test node publish/subscribe
 */
static bool test_node_pubsub(void)
{
    LOG_INFO("Test: Node publish/subscribe");

    // Create publisher node
    ssn_node_config_t publisher_config = {
        .node_type = "publisher",
        .node_name = "Publisher",
        .listen_address = "127.0.0.1",
        .listen_port = 8892,
        .capabilities = SSN_NODE_CAP_SERVER | SSN_NODE_CAP_PUBSUB
    };

    ssn_node_t *publisher_node = ssn_node_create(&publisher_config);
    if (!publisher_node) {
        LOG_ERROR("Failed to create publisher node");
        return false;
    }

    LOG_INFO("Publisher node created: id=%s, type=%s, name=%s", 
             publisher_node->node_id, publisher_node->node_type, publisher_node->node_name);

    // Start publisher node
    if (!ssn_node_start(publisher_node)) {
        LOG_ERROR("Failed to start publisher node");
        ssn_node_destroy(publisher_node);
        return false;
    }

    LOG_INFO("Publisher node started");

    // Start background server poller (the server is poll-driven)
    pthread_t server_tid;
    g_server_running = 1;
    if (pthread_create(&server_tid, NULL, server_poll_thread, publisher_node) != 0) {
        LOG_ERROR("Failed to create server poll thread");
        ssn_node_destroy(publisher_node);
        return false;
    }

    // Create subscriber1 node (subscribes to /news)
    ssn_node_config_t subscriber1_config = {
        .node_type = "subscriber",
        .node_name = "Subscriber1",
        .capabilities = SSN_NODE_CAP_CLIENT | SSN_NODE_CAP_PUBSUB
    };

    ssn_node_t *subscriber1_node = ssn_node_create(&subscriber1_config);
    if (!subscriber1_node) {
        LOG_ERROR("Failed to create subscriber1 node");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(publisher_node);
        return false;
    }

    LOG_INFO("Subscriber1 node created: id=%s, type=%s, name=%s", 
             subscriber1_node->node_id, subscriber1_node->node_type, subscriber1_node->node_name);

    // Start subscriber1 node
    if (!ssn_node_start(subscriber1_node)) {
        LOG_ERROR("Failed to start subscriber1 node");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(publisher_node);
        ssn_node_destroy(subscriber1_node);
        return false;
    }

    LOG_INFO("Subscriber1 node started");

    // Create subscriber2 node (subscribes to /weather)
    ssn_node_config_t subscriber2_config = {
        .node_type = "subscriber",
        .node_name = "Subscriber2",
        .capabilities = SSN_NODE_CAP_CLIENT | SSN_NODE_CAP_PUBSUB
    };

    ssn_node_t *subscriber2_node = ssn_node_create(&subscriber2_config);
    if (!subscriber2_node) {
        LOG_ERROR("Failed to create subscriber2 node");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(publisher_node);
        ssn_node_destroy(subscriber1_node);
        return false;
    }

    LOG_INFO("Subscriber2 node created: id=%s, type=%s, name=%s", 
             subscriber2_node->node_id, subscriber2_node->node_type, subscriber2_node->node_name);

    // Start subscriber2 node
    if (!ssn_node_start(subscriber2_node)) {
        LOG_ERROR("Failed to start subscriber2 node");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(publisher_node);
        ssn_node_destroy(subscriber1_node);
        ssn_node_destroy(subscriber2_node);
        return false;
    }

    LOG_INFO("Subscriber2 node started");

    // 消息回调在 subscribe 中传入（订阅即注册 per-URL 消息处理），无需另行设置

    // Subscribe to topics
    ssn_url_ref_t news_topic = {
        .url = "/news",
        .url_len = 5
    };

    ssn_url_ref_t weather_topic = {
        .url = "/weather",
        .url_len = 8
    };

    if (!ssn_node_subscribe(subscriber1_node, PUBLISHER_ADDRESS, &news_topic,
                            subscriber1_message_handler, NULL, 5000)) {
        LOG_ERROR("Subscriber1 failed to subscribe to /news");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(publisher_node);
        ssn_node_destroy(subscriber1_node);
        ssn_node_destroy(subscriber2_node);
        return false;
    }
    LOG_INFO("Subscriber1 subscribed to /news");

    if (!ssn_node_subscribe(subscriber2_node, PUBLISHER_ADDRESS, &weather_topic,
                            subscriber2_message_handler, NULL, 5000)) {
        LOG_ERROR("Subscriber2 failed to subscribe to /weather");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(publisher_node);
        ssn_node_destroy(subscriber1_node);
        ssn_node_destroy(subscriber2_node);
        return false;
    }
    LOG_INFO("Subscriber2 subscribed to /weather");

    // Wait for subscriptions to be established
    sleep(2);

    // Publish news message
    LOG_INFO("Publisher publishing to /news: Breaking news! Server is online");
    ssn_data_ref_t news_data = {
        .data = "Breaking news! Server is online",
        .length = 31
    };

    if (!ssn_node_publish(publisher_node, &news_topic, &news_data)) {
        LOG_ERROR("Failed to publish news message");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(publisher_node);
        ssn_node_destroy(subscriber1_node);
        ssn_node_destroy(subscriber2_node);
        return false;
    }

    // Wait for message to be delivered
    sleep(1);

    // Publish weather message
    LOG_INFO("Publisher publishing to /weather: Today's weather is sunny");
    ssn_data_ref_t weather_data = {
        .data = "Today's weather is sunny",
        .length = 24
    };

    if (!ssn_node_publish(publisher_node, &weather_topic, &weather_data)) {
        LOG_ERROR("Failed to publish weather message");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(publisher_node);
        ssn_node_destroy(subscriber1_node);
        ssn_node_destroy(subscriber2_node);
        return false;
    }

    // Wait for messages to be delivered
    LOG_INFO("Waiting for messages to be delivered...");
    int timeout = 5;
    while (timeout > 0 && (g_news_messages_received == 0 || g_weather_messages_received == 0)) {
        ssn_node_poll(publisher_node, 100);
        ssn_node_poll(subscriber1_node, 100);
        ssn_node_poll(subscriber2_node, 100);
        sleep(1);
        timeout--;
    }

    // Check if messages were received
    if (g_news_messages_received == 0) {
        LOG_ERROR("Subscriber1 did not receive news message");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(publisher_node);
        ssn_node_destroy(subscriber1_node);
        ssn_node_destroy(subscriber2_node);
        return false;
    }

    if (g_weather_messages_received == 0) {
        LOG_ERROR("Subscriber2 did not receive weather message");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(publisher_node);
        ssn_node_destroy(subscriber1_node);
        ssn_node_destroy(subscriber2_node);
        return false;
    }

    // Unsubscribe from topics
    ssn_node_unsubscribe(subscriber1_node, &news_topic, 5000);
    ssn_node_unsubscribe(subscriber2_node, &weather_topic, 5000);

    // Stop background server poller
    g_server_running = 0;
    pthread_join(server_tid, NULL);

    // Stop nodes
    ssn_node_stop(publisher_node);
    ssn_node_stop(subscriber1_node);
    ssn_node_stop(subscriber2_node);

    // Destroy nodes
    ssn_node_destroy(publisher_node);
    ssn_node_destroy(subscriber1_node);
    ssn_node_destroy(subscriber2_node);

    LOG_INFO("Publish/subscribe test completed successfully");
    return true;
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Node publish/subscribe example started");

    // Run test
    bool test = test_node_pubsub();

    if (test) {
        LOG_INFO("\nAll publish/subscribe tests completed successfully!");
        return 0;
    } else {
        LOG_ERROR("\nPublish/subscribe test failed!");
        return 1;
    }
}
