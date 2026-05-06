#include "ssn_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "ssn_client.h"

#define THREAD_COUNT 10
#define TEST_ITERATIONS 100

static ssn_client_t *clients[THREAD_COUNT];
static pthread_mutex_t print_mutex;

static void on_command_test(ssn_client_t *client, ssn_header_t *ssn_hdr, ssn_data_ref_t *data, void *arg)
{
    if (ssn_hdr) {
        pthread_mutex_lock(&print_mutex);
        printf("Thread %d: On RPC reply, data: %.*s\n", (int)(long)arg, (int)data->length, (char*)data->data);
        pthread_mutex_unlock(&print_mutex);
    } else {
        pthread_mutex_lock(&print_mutex);
        printf("Thread %d: RPC reply timeout!\n", (int)(long)arg);
        pthread_mutex_unlock(&print_mutex);
    }
}

static void *thread_func(void *arg)
{
    long thread_id = (long)arg;
    ssn_client_t *client = clients[thread_id];
    int i;

    pthread_mutex_lock(&print_mutex);
    printf("Thread %d: Started\n", (int)thread_id);
    pthread_mutex_unlock(&print_mutex);

    // 测试连接
    if (!ssn_client_connect(client, "unix:///tmp/ipc-test_server", NULL)) {
        pthread_mutex_lock(&print_mutex);
        printf("Thread %d: Can not connect to server!\n", (int)thread_id);
        pthread_mutex_unlock(&print_mutex);
        return NULL;
    }

    pthread_mutex_lock(&print_mutex);
    printf("Thread %d: Connected to server\n", (int)thread_id);
    pthread_mutex_unlock(&print_mutex);

    // 测试各种操作
    for (i = 0; i < TEST_ITERATIONS; i++) {
        // 测试RPC调用
        ssn_url_ref_t url;
        url.url = "/test";
        url.url_len = strlen(url.url);
        
        ssn_data_ref_t data;
        char msg[64];
        sprintf(msg, "Hello from thread %d, iteration %d", (int)thread_id, i);
        data.data = msg;
        data.length = strlen(msg);

        int ret = ssn_client_call(client, &url, &data, on_command_test, (void *)thread_id, 1000);
        if (ret < 0) {
            pthread_mutex_lock(&print_mutex);
            printf("Thread %d: RPC call error!\n", (int)thread_id);
            pthread_mutex_unlock(&print_mutex);
        }

        // 测试消息发送
        ret = ssn_client_message(client, &url, &data);
        if (ret < 0) {
            pthread_mutex_lock(&print_mutex);
            printf("Thread %d: Message send error!\n", (int)thread_id);
            pthread_mutex_unlock(&print_mutex);
        }

        // 测试轮询
        ssn_client_poll(client, 10);

        // 测试设置发送超时
        ssn_client_send_timeout(client, 1000);

        // 测试设置消息处理回调
        ssn_client_set_on_message(client, NULL, NULL);

        // 短暂休眠，避免过于密集的操作
        usleep(1000);
    }

    // 测试断开连接
    if (!ssn_client_disconnect(client)) {
        pthread_mutex_lock(&print_mutex);
        printf("Thread %d: Disconnect error!\n", (int)thread_id);
        pthread_mutex_unlock(&print_mutex);
    }

    pthread_mutex_lock(&print_mutex);
    printf("Thread %d: Disconnected\n", (int)thread_id);
    pthread_mutex_unlock(&print_mutex);

    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t threads[THREAD_COUNT];
    long i;

    // 初始化打印互斥锁
    pthread_mutex_init(&print_mutex, NULL);

    // 创建多个客户端
    for (i = 0; i < THREAD_COUNT; i++) {
        clients[i] = ssn_client_create(NULL, NULL);
        if (!clients[i]) {
            fprintf(stderr, "Can not create client %ld!\n", i);
            return -1;
        }
        printf("Created client %ld\n", i);
    }

    // 创建多个线程
    for (i = 0; i < THREAD_COUNT; i++) {
        if (pthread_create(&threads[i], NULL, thread_func, (void *)i) != 0) {
            fprintf(stderr, "Can not create thread %ld!\n", i);
            return -1;
        }
    }

    // 等待所有线程完成
    for (i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
        printf("Thread %ld joined\n", i);
    }

    // 关闭所有客户端
    for (i = 0; i < THREAD_COUNT; i++) {
        ssn_client_close(clients[i]);
        printf("Closed client %ld\n", i);
    }

    // 销毁互斥锁
    pthread_mutex_destroy(&print_mutex);

    printf("All threads completed\n");
    return 0;
}
