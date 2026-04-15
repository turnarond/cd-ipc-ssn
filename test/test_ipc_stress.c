#include "ipc_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include "ipc_client.h"

#define THREAD_COUNT 50
#define TEST_ITERATIONS 1000

static ipc_client_t *clients[THREAD_COUNT];
static pthread_mutex_t stats_mutex;
static int total_success = 0;
static int total_failure = 0;
static long long total_time = 0;

static void on_command_stress(struct ipc_client *client, ipc_header_t *ipc_hdr, ipc_data_ref_t *data, void *arg)
{
    // 简单的回调处理
}

static void *thread_func(void *arg)
{
    long thread_id = (long)arg;
    ipc_client_t *client = clients[thread_id];
    int i;
    int thread_success = 0;
    int thread_failure = 0;
    long long thread_time = 0;

    // 测试连接
    if (!ipc_client_connect(client, "ipc-stress_server", NULL)) {
        pthread_mutex_lock(&stats_mutex);
        total_failure++;
        pthread_mutex_unlock(&stats_mutex);
        return NULL;
    }

    // 测试各种操作
    for (i = 0; i < TEST_ITERATIONS; i++) {
        struct timespec start, end;
        long long elapsed;

        // 测试RPC调用
        ipc_url_ref_t url;
        url.url = "/stress";
        url.url_len = strlen(url.url);
        
        ipc_data_ref_t data;
        char msg[64];
        sprintf(msg, "Stress test from thread %d, iteration %d", (int)thread_id, i);
        data.data = msg;
        data.length = strlen(msg);

        // 记录开始时间
        clock_gettime(CLOCK_MONOTONIC, &start);

        int ret = ipc_client_call(client, &url, &data, on_command_stress, NULL, 500);
        if (ret < 0) {
            thread_failure++;
        } else {
            thread_success++;
        }

        // 记录结束时间
        clock_gettime(CLOCK_MONOTONIC, &end);
        elapsed = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
        thread_time += elapsed;

        // 测试消息发送
        ret = ipc_client_message(client, &url, &data);
        if (ret < 0) {
            thread_failure++;
        } else {
            thread_success++;
        }

        // 测试轮询
        ipc_client_poll(client, 1);
    }

    // 测试断开连接
    if (!ipc_client_disconnect(client)) {
        thread_failure++;
    } else {
        thread_success++;
    }

    // 更新统计信息
    pthread_mutex_lock(&stats_mutex);
    total_success += thread_success;
    total_failure += thread_failure;
    total_time += thread_time;
    pthread_mutex_unlock(&stats_mutex);

    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t threads[THREAD_COUNT];
    long i;
    struct timespec start, end;
    long long total_elapsed;

    // 初始化统计互斥锁
    pthread_mutex_init(&stats_mutex, NULL);

    printf("Starting stress test with %d threads, %d iterations each...\n", THREAD_COUNT, TEST_ITERATIONS);

    // 记录总开始时间
    clock_gettime(CLOCK_MONOTONIC, &start);

    // 创建多个客户端
    for (i = 0; i < THREAD_COUNT; i++) {
        clients[i] = ipc_client_create(NULL, NULL);
        if (!clients[i]) {
            fprintf(stderr, "Can not create client %ld!\n", i);
            return -1;
        }
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
    }

    // 记录总结束时间
    clock_gettime(CLOCK_MONOTONIC, &end);
    total_elapsed = (end.tv_sec - start.tv_sec) * 1000LL + (end.tv_nsec - start.tv_nsec) / 1000000LL;

    // 关闭所有客户端
    for (i = 0; i < THREAD_COUNT; i++) {
        ipc_client_close(clients[i]);
    }

    // 销毁互斥锁
    pthread_mutex_destroy(&stats_mutex);

    // 计算统计信息
    int total_operations = total_success + total_failure;
    double success_rate = (double)total_success / total_operations * 100.0;
    double avg_time_per_operation = (double)total_time / total_operations / 1000.0; // 微秒

    // 输出统计信息
    printf("\nStress test completed!\n");
    printf("Total operations: %d\n", total_operations);
    printf("Success: %d (%.2f%%)\n", total_success, success_rate);
    printf("Failure: %d (%.2f%%)\n", total_failure, 100.0 - success_rate);
    printf("Total time: %lld ms\n", total_elapsed);
    printf("Average time per operation: %.2f us\n", avg_time_per_operation);

    return 0;
}
