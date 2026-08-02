# QoS服务质量框架详细设计文档

## 1. 概述

### 1.1 QoS框架目标

QoS（Quality of Service）服务质量框架旨在为SSN提供完善的服务质量保障机制，确保不同优先级、不同可靠性要求的消息能够按照预期的方式进行传输。

### 1.2 设计原则

1. **可配置性**：提供丰富的QoS配置选项
2. **可扩展性**：支持新增QoS策略
3. **透明性**：对上层应用透明，无需关心底层实现
4. **高性能**：尽量减少QoS机制带来的性能开销

## 2. QoS架构设计

### 2.1 QoS架构层次

```
┌─────────────────────────────────────────────────────────────┐
│                    QoS 策略层 (QoS Policy Layer)            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
│  │ 可靠性策略  │  │ 优先级策略  │  │  带宽策略   │        │
│  └─────────────┘  └─────────────┘  └─────────────┘        │
├─────────────────────────────────────────────────────────────┤
│                    QoS 执行层 (QoS Enforcement Layer)       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
│  │  流量整形   │  │  拥塞控制   │  │  调度队列   │        │
│  └─────────────┘  └─────────────┘  └─────────────┘        │
├─────────────────────────────────────────────────────────────┤
│                    QoS 监控层 (QoS Monitoring Layer)        │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
│  │  延迟监控   │  │  丢包监控   │  │  带宽监控   │        │
│  └─────────────┘  └─────────────┘  └─────────────┘        │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 核心组件

1. **QoS管理器**：统一管理所有QoS策略
2. **策略调度器**：根据QoS配置调度消息
3. **流量控制器**：控制消息发送速率
4. **可靠性模块**：处理消息确认和重传
5. **监控模块**：监控QoS指标

## 3. QoS配置模型

### 3.1 QoS配置结构

```c
// QoS可靠性等级
typedef enum {
    IPC_RELIABILITY_BEST_EFFORT = 0,     // 尽力而为
    IPC_RELIABILITY_AT_LEAST_ONCE,        // 至少一次
    IPC_RELIABILITY_AT_MOST_ONCE,        // 至多一次
    IPC_RELIABILITY_EXACTLY_ONCE,        // 精确一次
    IPC_RELIABILITY_RELIABLE             // 可靠传输
} ipc_reliability_level_t;

// QoS优先级
typedef enum {
    IPC_PRIORITY_REALTIME = 0,          // 实时优先级
    IPC_PRIORITY_HIGH,                   // 高优先级
    IPC_PRIORITY_NORMAL,                 // 普通优先级
    IPC_PRIORITY_LOW,                    // 低优先级
    IPC_PRIORITY_BACKGROUND             // 后台优先级
} ipc_priority_level_t;

// QoS配置
typedef struct {
    // 可靠性配置
    ipc_reliability_level_t reliability;    // 可靠性等级
    uint32_t max_retries;                   // 最大重试次数
    uint32_t retry_timeout_ms;              // 重试超时时间(ms)
    bool enable_deduplication;              // 启用去重
    
    // 优先级配置
    ipc_priority_level_t priority;          // 传输优先级
    uint32_t deadline_ms;                   // 截止时间(ms)
    uint32_t max_delay_ms;                 // 最大延迟(ms)
    
    // 带宽配置
    uint32_t max_bandwidth_kbps;           // 最大带宽(kbps)
    uint32_t burst_bandwidth_kbps;         // 突发带宽(kbps)
    bool enable_rate_limiting;             // 启用速率限制
    
    // 延迟配置
    uint32_t max_latency_ms;               // 最大延迟(ms)
    uint32_t max_jitter_ms;               // 最大抖动(ms)
    
    // 生存周期配置
    uint32_t time_to_live_sec;             // 生存时间(秒)
    uint32_t max_hop_count;                // 最大跳数
    
    // 功能开关
    bool enable_compression;                // 启用压缩
    bool enable_encryption;                // 启用加密
    bool enable_fec;                      // 启用前向纠错
    uint8_t fec_redundancy;                // FEC冗余度(%)
    
    // 历史配置
    bool enable_history;                   // 启用消息历史
    uint32_t history_size;                // 历史消息数量
} ssn_qos_config_t;

// 默认QoS配置
static const ssn_qos_config_t IPC_QOS_DEFAULT = {
    .reliability = IPC_RELIABILITY_BEST_EFFORT,
    .max_retries = 3,
    .retry_timeout_ms = 1000,
    .enable_deduplication = false,
    .priority = IPC_PRIORITY_NORMAL,
    .deadline_ms = 0,
    .max_delay_ms = 0,
    .max_bandwidth_kbps = 0,  // 0表示无限制
    .burst_bandwidth_kbps = 0,
    .enable_rate_limiting = false,
    .max_latency_ms = 0,
    .max_jitter_ms = 0,
    .time_to_live_sec = 300,
    .max_hop_count = 64,
    .enable_compression = false,
    .enable_encryption = false,
    .enable_fec = false,
    .fec_redundancy = 10,
    .enable_history = false,
    .history_size = 0
};
```

### 3.2 QoS预设配置

```c
// 实时数据传输配置
static const ssn_qos_config_t IPC_QOS_REALTIME = {
    .reliability = IPC_RELIABILITY_BEST_EFFORT,
    .priority = IPC_PRIORITY_REALTIME,
    .deadline_ms = 100,
    .max_delay_ms = 50,
    .max_latency_ms = 100,
    .max_jitter_ms = 20,
    .enable_fec = true,
    .fec_redundancy = 20
};

// 可靠数据传输配置
static const ssn_qos_config_t IPC_QOS_RELIABLE = {
    .reliability = IPC_RELIABILITY_RELIABLE,
    .priority = IPC_PRIORITY_HIGH,
    .max_retries = 5,
    .retry_timeout_ms = 500,
    .enable_deduplication = true,
    .max_latency_ms = 5000,
    .time_to_live_sec = 600
};

// 高吞吐量配置
static const ssn_qos_config_t IPC_QOS_THROUGHPUT = {
    .reliability = IPC_RELIABILITY_AT_LEAST_ONCE,
    .priority = IPC_PRIORITY_NORMAL,
    .max_retries = 3,
    .enable_compression = true,
    .max_bandwidth_kbps = 10000,
    .burst_bandwidth_kbps = 15000
};
```

## 4. 可靠性等级实现

### 4.1 尽力而为传输

```c
// 尽力而为可靠性模块
typedef struct best_effort_reliability {
    ipc_reliability_module_t base;  // 基类
    
    // 统计
    uint64_t packets_sent;
    uint64_t packets_lost;
} best_effort_reliability_t;

// 尽力而为发送
static int best_effort_send(best_effort_reliability_t* mod, ipc_message_t* msg) {
    mod->packets_sent++;
    
    // 直接发送，不等待确认
    int result = ipc_transport_send(mod->transport, msg->data, msg->size);
    
    if (result < 0) {
        mod->packets_lost++;
    }
    
    return result;
}
```

### 4.2 至少一次传输

```c
// 至少一次可靠性模块
typedef struct at_least_once_reliability {
    ipc_reliability_module_t base;
    
    // 重传队列
    ipc_list_t* pending_acks;        // 等待确认的消息
    ipc_hash_table_t* retry_timers; // 重试定时器
    
    // 配置
    uint32_t max_retries;
    uint32_t retry_timeout_ms;
    uint32_t ack_timeout_ms;
    
    // 统计
    uint64_t packets_sent;
    uint64_t packets_acked;
    uint64_t packets_dropped;       // 丢弃的消息
} at_least_once_reliability_t;

// 至少一次发送
static int at_least_once_send(at_least_once_reliability_t* mod, ipc_message_t* msg) {
    // 为消息分配序列号
    uint16_t seqno = atomic_increment(&mod->base.next_seqno);
    msg->sequence = seqno;
    
    // 创建确认记录
    pending_ack_t* ack_record = create_pending_ack(seqno, msg, mod->retry_timeout_ms);
    mod->pending_acks->append(mod->pending_acks, ack_record);
    
    mod->packets_sent++;
    
    // 发送消息
    int result = ipc_transport_send(mod->transport, msg->data, msg->size);
    
    if (result < 0) {
        mod->packets_dropped++;
        remove_pending_ack(mod, seqno);
        return result;
    }
    
    // 启动重试定时器
    start_retry_timer(mod, ack_record);
    
    return result;
}

// 处理确认
static void at_least_once_handle_ack(at_least_once_reliability_t* mod, uint16_t seqno) {
    ipc_mutex_lock(mod->base.lock);
    
    pending_ack_t* ack_record = find_pending_ack(mod, seqno);
    if (ack_record) {
        mod->packets_acked++;
        stop_retry_timer(mod, ack_record);
        remove_pending_ack(mod, seqno);
        
        // 触发发送完成回调
        if (mod->base.on_send_complete) {
            mod->base.on_send_complete(ack_record->message, true);
        }
    }
    
    ipc_mutex_unlock(mod->base.lock);
}

// 重试处理
static void at_least_once_retry(void* arg) {
    at_least_once_reliability_t* mod = (at_least_once_reliability_t*)arg;
    
    ipc_mutex_lock(mod->base.lock);
    
    ipc_list_node_t* node = mod->pending_acks->head;
    while (node) {
        pending_ack_t* ack_record = (pending_ack_t*)node->data;
        
        if (ack_record->retry_count >= mod->max_retries) {
            LOG_WARN("Message seqno %d exceeded max retries, dropping", ack_record->sequence);
            mod->packets_dropped++;
            
            ipc_list_node_t* next = node->next;
            remove_pending_ack(mod, ack_record->sequence);
            node = next;
            continue;
        }
        
        if (time(NULL) - ack_record->send_time >= mod->retry_timeout_ms / 1000) {
            // 重新发送
            LOG_DEBUG("Retrying message seqno %d, attempt %d", 
                     ack_record->sequence, ack_record->retry_count + 1);
            
            ipc_transport_send(mod->transport, ack_record->message->data, 
                             ack_record->message->size);
            
            ack_record->retry_count++;
            ack_record->send_time = time(NULL);
        }
        
        node = node->next;
    }
    
    ipc_mutex_unlock(mod->base.lock);
}
```

### 4.3 精确一次传输

```c
// 精确一次可靠性模块
typedef struct exactly_once_reliability {
    ipc_reliability_module_t base;
    
    // 消息ID生成器
    uint64_t message_id_counter;
    
    // 已处理消息缓存（用于去重）
    ipc_hash_table_t* processed_messages; // message_id -> 处理时间
    uint32_t deduplication_window_sec;     // 去重窗口
    
    // 待确认消息
    ipc_list_t* pending_final_acks;
    
    // 两阶段提交状态
    ipc_hash_table_t* transaction_states; // transaction_id -> state
    
    // 统计
    uint64_t transactions_started;
    uint64_t transactions_completed;
    uint64_t duplicates_detected;
} exactly_once_reliability_t;

// 精确一次发送 - 第一阶段：准备
static int exactly_once_send_prepare(exactly_once_reliability_t* mod, ipc_message_t* msg) {
    // 生成唯一消息ID
    uint64_t message_id = atomic_increment(&mod->message_id_counter);
    msg->message_id = message_id;
    
    // 设置消息类型为准备阶段
    msg->flags |= IPC_MSG_FLAG_PREPARE;
    
    // 创建事务状态
    transaction_state_t* tx_state = create_transaction_state(message_id, msg);
    ipc_hash_table_set(mod->transaction_states, (void*)message_id, tx_state);
    
    mod->transactions_started++;
    
    // 发送准备消息
    return ipc_transport_send(mod->transport, msg->data, msg->size);
}

// 精确一次发送 - 第二阶段：提交
static int exactly_once_send_commit(exactly_once_reliability_t* mod, uint64_t message_id) {
    transaction_state_t* tx_state = ipc_hash_table_get(mod->transaction_states, (void*)message_id);
    if (!tx_state) {
        LOG_ERROR("Transaction %lu not found", message_id);
        return -1;
    }
    
    // 发送提交消息
    ipc_message_t commit_msg;
    commit_msg.message_id = message_id;
    commit_msg.flags = IPC_MSG_FLAG_COMMIT;
    memcpy(commit_msg.data, tx_state->original_message->data, tx_state->original_message->size);
    commit_msg.size = tx_state->original_message->size;
    
    int result = ipc_transport_send(mod->transport, commit_msg.data, commit_msg.size);
    
    if (result >= 0) {
        mod->transactions_completed++;
    }
    
    // 清理事务状态
    destroy_transaction_state(tx_state);
    ipc_hash_table_remove(mod->transaction_states, (void*)message_id);
    
    return result;
}

// 处理重复消息
static bool exactly_once_check_duplicate(exactly_once_reliability_t* mod, uint64_t message_id) {
    // 检查是否已处理过
    time_t* processed_time = (time_t*)ipc_hash_table_get(mod->processed_messages, (void*)message_id);
    
    if (processed_time) {
        mod->duplicates_detected++;
        LOG_DEBUG("Duplicate message detected: %lu", message_id);
        return true;
    }
    
    // 添加到已处理缓存
    time_t now = time(NULL);
    ipc_hash_table_set(mod->processed_messages, (void*)message_id, (void*)now);
    
    // 清理过期条目
    cleanup_expired_dedup_cache(mod);
    
    return false;
}

// 清理过期去重缓存
static void cleanup_expired_dedup_cache(exactly_once_reliability_t* mod) {
    time_t now = time(NULL);
    
    ipc_list_t* to_remove = NULL;
    
    ipc_hash_table_iter_t iter;
    ipc_hash_table_iter_init(&iter, mod->processed_messages);
    
    while (ipc_hash_table_iter_next(&iter)) {
        uint64_t* msg_id = (uint64_t*)iter.key;
        time_t* proc_time = (time_t*)iter.value;
        
        if (now - *proc_time > mod->deduplication_window_sec) {
            if (!to_remove) to_remove = ipc_list_create();
            to_remove->append(to_remove, (void*)*msg_id);
        }
    }
    
    if (to_remove) {
        ipc_list_node_t* node = to_remove->head;
        while (node) {
            ipc_hash_table_remove(mod->processed_messages, node->data);
            node = node->next;
        }
        ipc_list_destroy(to_remove);
    }
}
```

## 5. 优先级调度实现

### 5.1 多优先级队列

```c
// 优先级队列配置
#define IPC_PRIORITY_COUNT 5

// 优先级调度器
typedef struct priority_scheduler {
    // 优先级队列数组
    ipc_message_queue_t queues[IPC_PRIORITY_COUNT];
    
    // 调度权重
    uint32_t weights[IPC_PRIORITY_COUNT];
    
    // 调度统计
    uint64_t messages_scheduled[IPC_PRIORITY_COUNT];
    uint64_t bytes_scheduled[IPC_PRIORITY_COUNT];
    
    // 调度算法
    ipc_scheduling_algorithm_t algorithm;
    
    // 同步
    ipc_mutex_t* lock;
} priority_scheduler_t;

// 调度算法类型
typedef enum {
    IPC_SCHEDULING_STRICT_PRIORITY,      // 严格优先级
    IPC_SCHEDULING_WEIGHTED_ROUND_ROBIN, // 加权轮询
    IPC_SCHEDULING_DEFICIT_ROUND_ROBIN,  // 赤字轮询
    IPC_SCHEDULING_FAIR_QUEUEING         // 公平队列
} ipc_scheduling_algorithm_t;

// 严格优先级调度
static ipc_message_t* strict_priority_schedule(priority_scheduler_t* sched) {
    // 从高到低遍历队列
    for (int i = 0; i < IPC_PRIORITY_COUNT; i++) {
        if (!sched->queues[i].is_empty(&sched->queues[i])) {
            ipc_message_t* msg = sched->queues[i].dequeue(&sched->queues[i]);
            
            sched->messages_scheduled[i]++;
            sched->bytes_scheduled[i] += msg->size;
            
            return msg;
        }
    }
    
    return NULL;
}

// 加权轮询调度
typedef struct wrr_state {
    int current_priority;
    uint32_t current_weight[IPC_PRIORITY_COUNT];
    uint32_t quantum[IPC_PRIORITY_COUNT];
} wrr_state_t;

static ipc_message_t* weighted_round_robin_schedule(priority_scheduler_t* sched) {
    static wrr_state_t state = {0};
    
    // 初始化权重
    if (state.current_priority == 0) {
        for (int i = 0; i < IPC_PRIORITY_COUNT; i++) {
            state.current_weight[i] = sched->weights[i];
            state.quantum[i] = sched->weights[i];
        }
    }
    
    // 遍历所有队列，寻找有消息的队列
    int checked = 0;
    while (checked < IPC_PRIORITY_COUNT) {
        int idx = state.current_priority;
        
        if (state.current_weight[idx] > 0 && !sched->queues[idx].is_empty(&sched->queues[idx])) {
            ipc_message_t* msg = sched->queues[idx].dequeue(&sched->queues[idx]);
            
            state.current_weight[idx]--;
            
            // 如果消息大小超过当前quantum，需要放回队列
            if (msg->size > state.quantum[idx] * 1024) {
                sched->queues[idx].enqueue_front(&sched->queues[idx], msg);
                state.quantum[idx] *= 2;
            }
            
            sched->messages_scheduled[idx]++;
            sched->bytes_scheduled[idx] += msg->size;
            
            // 移动到下一个优先级
            state.current_priority = (state.current_priority + 1) % IPC_PRIORITY_COUNT;
            
            return msg;
        }
        
        // 移动到下一个优先级
        state.current_priority = (state.current_priority + 1) % IPC_PRIORITY_COUNT;
        checked++;
        
        // 如果轮完一圈，重置权重
        if (state.current_priority == 0) {
            for (int i = 0; i < IPC_PRIORITY_COUNT; i++) {
                state.current_weight[i] = sched->weights[i];
            }
        }
    }
    
    return NULL;
}

// 消息入队
static bool priority_scheduler_enqueue(priority_scheduler_t* sched, ipc_message_t* msg) {
    if (!sched || !msg) {
        return false;
    }
    
    int priority = msg->qos.priority;
    if (priority < 0 || priority >= IPC_PRIORITY_COUNT) {
        priority = IPC_PRIORITY_NORMAL;
    }
    
    ipc_mutex_lock(sched->lock);
    
    bool result = sched->queues[priority].enqueue(&sched->queues[priority], msg);
    
    ipc_mutex_unlock(sched->lock);
    
    return result;
}

// 消息出队
static ipc_message_t* priority_scheduler_dequeue(priority_scheduler_t* sched) {
    ipc_mutex_lock(sched->lock);
    
    ipc_message_t* msg = NULL;
    
    switch (sched->algorithm) {
        case IPC_SCHEDULING_STRICT_PRIORITY:
            msg = strict_priority_schedule(sched);
            break;
            
        case IPC_SCHEDULING_WEIGHTED_ROUND_ROBIN:
            msg = weighted_round_robin_schedule(sched);
            break;
            
        case IPC_SCHEDULING_DEFICIT_ROUND_ROBIN:
            // 实现DRR调度
            break;
            
        case IPC_SCHEDULING_FAIR_QUEUEING:
            // 实现公平队列调度
            break;
    }
    
    ipc_mutex_unlock(sched->lock);
    
    return msg;
}
```

## 6. 带宽控制实现

### 6.1 令牌桶算法

```c
// 令牌桶配置
typedef struct {
    uint32_t rate_kbps;           // 令牌生成速率(kbps)
    uint32_t bucket_capacity;     // 桶容量(字节)
    uint32_t current_tokens;       // 当前令牌数
    time_t last_update_time;      // 最后更新时间
    ipc_mutex_t* lock;           // 同步锁
} token_bucket_t;

// 创建令牌桶
static token_bucket_t* token_bucket_create(uint32_t rate_kbps, uint32_t bucket_capacity) {
    token_bucket_t* bucket = calloc(1, sizeof(token_bucket_t));
    if (!bucket) {
        return NULL;
    }
    
    bucket->rate_kbps = rate_kbps;
    bucket->bucket_capacity = bucket_capacity;
    bucket->current_tokens = bucket_capacity; // 初始时满桶
    bucket->last_update_time = time(NULL);
    bucket->lock = ipc_mutex_create();
    
    return bucket;
}

// 更新令牌桶
static void token_bucket_update(token_bucket_t* bucket) {
    ipc_mutex_lock(bucket->lock);
    
    time_t now = time(NULL);
    double elapsed = difftime(now, bucket->last_update_time);
    
    // 计算新增令牌数
    // rate_kbps = kbits/s = 125 bytes/ms
    double tokens_to_add = elapsed * bucket->rate_kbps * 125.0 / 1000.0;
    
    bucket->current_tokens = MIN(bucket->bucket_capacity, 
                               bucket->current_tokens + (uint32_t)tokens_to_add);
    bucket->last_update_time = now;
    
    ipc_mutex_unlock(bucket->lock);
}

// 尝试获取令牌
static bool token_bucket_try_consume(token_bucket_t* bucket, uint32_t bytes) {
    ipc_mutex_lock(bucket->lock);
    
    // 先更新令牌
    time_t now = time(NULL);
    double elapsed = difftime(now, bucket->last_update_time);
    double tokens_to_add = elapsed * bucket->rate_kbps * 125.0 / 1000.0;
    
    bucket->current_tokens = MIN(bucket->bucket_capacity,
                               bucket->current_tokens + (uint32_t)tokens_to_add);
    bucket->last_update_time = now;
    
    // 尝试消费令牌
    if (bucket->current_tokens >= bytes) {
        bucket->current_tokens -= bytes;
        ipc_mutex_unlock(bucket->lock);
        return true;
    }
    
    ipc_mutex_unlock(bucket->lock);
    return false;
}

// 计算需要等待的时间
static uint32_t token_bucket_wait_time(token_bucket_t* bucket, uint32_t bytes) {
    ipc_mutex_lock(bucket->lock);
    
    if (bucket->current_tokens >= bytes) {
        ipc_mutex_unlock(bucket->lock);
        return 0;
    }
    
    // 计算需要等待的时间
    uint32_t tokens_needed = bytes - bucket->current_tokens;
    double wait_time_ms = (tokens_needed * 1000.0) / (bucket->rate_kbps * 125.0 / 1000.0);
    
    ipc_mutex_unlock(bucket->lock);
    
    return (uint32_t)ceil(wait_time_ms);
}
```

### 6.2 漏桶算法

```c
// 漏桶配置
typedef struct {
    uint32_t rate_kbps;           // 漏出速率(kbps)
    uint32_t bucket_capacity;     // 桶容量(字节)
    uint32_t current_level;       // 当前水量(字节)
    time_t last_update_time;      // 最后更新时间
    ipc_mutex_t* lock;           // 同步锁
    
    // 等待队列
    ipc_list_t* waiting_messages;
    ipc_condition_t* cond;
} leak_bucket_t;

// 创建漏桶
static leak_bucket_t* leak_bucket_create(uint32_t rate_kbps, uint32_t bucket_capacity) {
    leak_bucket_t* bucket = calloc(1, sizeof(leak_bucket_t));
    if (!bucket) {
        return NULL;
    }
    
    bucket->rate_kbps = rate_kbps;
    bucket->bucket_capacity = bucket_capacity;
    bucket->current_level = 0;
    bucket->last_update_time = time(NULL);
    bucket->lock = ipc_mutex_create();
    bucket->waiting_messages = ipc_list_create();
    bucket->cond = ipc_condition_create();
    
    return bucket;
}

// 添加消息到漏桶
static bool leak_bucket_add(leak_bucket_t* bucket, ipc_message_t* msg) {
    ipc_mutex_lock(bucket->lock);
    
    uint32_t message_size = msg->size;
    
    // 如果消息太大，直接拒绝
    if (message_size > bucket->bucket_capacity) {
        ipc_mutex_unlock(bucket->lock);
        return false;
    }
    
    // 等待直到有足够空间
    while (bucket->current_level + message_size > bucket->bucket_capacity) {
        // 更新漏桶
        leak_bucket_update(bucket);
        
        if (bucket->current_level + message_size > bucket->bucket_capacity) {
            // 释放锁并等待
            ipc_condition_wait(bucket->cond, bucket->lock);
        }
    }
    
    // 添加消息到等待队列
    bucket->waiting_messages->append(bucket->waiting_messages, msg);
    bucket->current_level += message_size;
    
    ipc_mutex_unlock(bucket->lock);
    
    return true;
}

// 更新漏桶（漏水）
static void leak_bucket_update(leak_bucket_t* bucket) {
    time_t now = time(NULL);
    double elapsed = difftime(now, bucket->last_update_time);
    
    // 计算漏出的水量
    // rate_kbps = kbits/s = 125 bytes/ms
    double leaked = elapsed * bucket->rate_kbps * 125.0 / 1000.0;
    
    if (bucket->current_level > leaked) {
        bucket->current_level -= (uint32_t)leaked;
    } else {
        bucket->current_level = 0;
    }
    
    bucket->last_update_time = now;
    
    // 通知等待的线程
    if (bucket->current_level < bucket->bucket_capacity) {
        ipc_condition_signal(bucket->cond);
    }
}

// 从漏桶获取消息
static ipc_message_t* leak_bucket_get(leak_bucket_t* bucket) {
    ipc_mutex_lock(bucket->lock);
    
    // 更新漏桶
    leak_bucket_update(bucket);
    
    if (bucket->waiting_messages->size == 0) {
        ipc_mutex_unlock(bucket->lock);
        return NULL;
    }
    
    // 获取队列头部消息
    ipc_message_t* msg = (ipc_message_t*)bucket->waiting_messages->head->data;
    bucket->waiting_messages->remove_head(bucket->waiting_messages);
    
    // 更新水位
    if (bucket->current_level >= msg->size) {
        bucket->current_level -= msg->size;
    } else {
        bucket->current_level = 0;
    }
    
    ipc_mutex_unlock(bucket->lock);
    
    return msg;
}
```

## 7. 延迟保障机制

### 7.1 延迟监控

```c
// 延迟监控器
typedef struct latency_monitor {
    // 延迟统计
    uint64_t total_samples;
    uint32_t min_latency_us;
    uint32_t max_latency_us;
    uint64_t total_latency_us;
    
    // 抖动统计
    uint32_t last_latency_us;
    uint64_t total_jitter_us;
    uint32_t max_jitter_us;
    
    // 百分位统计
    uint64_t p50_latency_us;
    uint64_t p95_latency_us;
    uint64_t p99_latency_us;
    
    // 直方图
    uint32_t histogram[32]; // 延迟分布直方图
    
    // 同步
    ipc_mutex_t* lock;
    
    // 回调
    void (*on_latency_exceeded)(uint32_t latency_us, void* arg);
    void* callback_arg;
} latency_monitor_t;

// 记录延迟样本
static void latency_monitor_record(latency_monitor_t* mon, uint32_t latency_us) {
    ipc_mutex_lock(mon->lock);
    
    // 更新统计
    mon->total_samples++;
    mon->min_latency_us = MIN(mon->min_latency_us, latency_us);
    mon->max_latency_us = MAX(mon->max_latency_us, latency_us);
    mon->total_latency_us += latency_us;
    
    // 计算抖动
    if (mon->last_latency_us > 0) {
        uint32_t jitter = abs((int32_t)latency_us - (int32_t)mon->last_latency_us);
        mon->total_jitter_us += jitter;
        mon->max_jitter_us = MAX(mon->max_jitter_us, jitter);
    }
    mon->last_latency_us = latency_us;
    
    // 更新直方图
    int bucket = 0;
    uint32_t threshold = 1000; // 1ms
    for (int i = 0; i < 31; i++) {
        if (latency_us < threshold) {
            bucket = i;
            break;
        }
        threshold *= 2;
    }
    mon->histogram[bucket]++;
    
    // 检查延迟是否超过阈值
    if (mon->on_latency_exceeded && latency_us > 100000) { // > 100ms
        mon->on_latency_exceeded(latency_us, mon->callback_arg);
    }
    
    ipc_mutex_unlock(mon->lock);
}

// 获取延迟统计
static bool latency_monitor_get_stats(latency_monitor_t* mon, latency_stats_t* stats) {
    ipc_mutex_lock(mon->lock);
    
    if (mon->total_samples == 0) {
        ipc_mutex_unlock(mon->lock);
        return false;
    }
    
    stats->min_latency_us = mon->min_latency_us;
    stats->max_latency_us = mon->max_latency_us;
    stats->avg_latency_us = mon->total_latency_us / mon->total_samples;
    stats->p50_latency_us = mon->p50_latency_us;
    stats->p95_latency_us = mon->p95_latency_us;
    stats->p99_latency_us = mon->p99_latency_us;
    stats->avg_jitter_us = mon->total_jitter_us / mon->total_samples;
    stats->max_jitter_us = mon->max_jitter_us;
    
    ipc_mutex_unlock(mon->lock);
    
    return true;
}
```

### 7.2 延迟保障策略

```c
// 延迟保障配置
typedef struct {
    uint32_t target_latency_ms;       // 目标延迟
    uint32_t max_latency_ms;         // 最大延迟
    bool enable_priority_boost;      // 启用优先级提升
    uint32_t boost_threshold_ms;     // 提升阈值
    uint32_t boost_amount;          // 提升量
    bool enable_preemption;          // 启用抢占
} latency_guarantee_config_t;

// 延迟保障调度器
typedef struct latency_guarantee_scheduler {
    latency_guarantee_config_t config;
    
    // 优先级调度器
    priority_scheduler_t* priority_sched;
    
    // 延迟监控
    latency_monitor_t* monitor;
    
    // 截止时间管理
    ipc_heap_t* deadline_queue;     // 按截止时间组织的消息堆
    
    // 同步
    ipc_mutex_t* lock;
} latency_guarantee_scheduler_t;

// 延迟保障入队
static bool latency_guarantee_enqueue(latency_guarantee_scheduler_t* sched, ipc_message_t* msg) {
    // 设置消息的截止时间
    if (msg->qos.deadline_ms > 0) {
        msg->deadline = get_current_time_ms() + msg->qos.deadline_ms;
    } else if (msg->qos.max_delay_ms > 0) {
        msg->deadline = get_current_time_ms() + msg->qos.max_delay_ms;
    } else {
        msg->deadline = 0; // 无截止时间
    }
    
    // 如果启用了优先级提升，检查是否需要提升
    if (sched->config.enable_priority_boost) {
        uint32_t now = get_current_time_ms();
        uint32_t time_until_deadline = (msg->deadline > now) ? (msg->deadline - now) : 0;
        
        if (time_until_deadline < sched->config.boost_threshold_ms) {
            // 提升优先级
            msg->qos.priority = MAX(0, msg->qos.priority - sched->config.boost_amount);
            LOG_DEBUG("Boosted message priority to %d (deadline in %ums)", 
                     msg->qos.priority, time_until_deadline);
        }
    }
    
    // 添加到截止时间队列
    if (msg->deadline > 0) {
        ipc_mutex_lock(sched->lock);
        sched->deadline_queue->insert(sched->deadline_queue, msg);
        ipc_mutex_unlock(sched->lock);
    }
    
    // 添加到优先级队列
    return priority_scheduler_enqueue(sched->priority_sched, msg);
}

// 延迟保障出队
static ipc_message_t* latency_guarantee_dequeue(latency_guarantee_scheduler_t* sched) {
    uint32_t now = get_current_time_ms();
    
    ipc_mutex_lock(sched->lock);
    
    // 检查是否有消息超过截止时间
    while (sched->deadline_queue->size > 0) {
        ipc_message_t* earliest = sched->deadline_queue->peek_min(sched->deadline_queue);
        
        if (earliest->deadline > 0 && earliest->deadline < now) {
            // 消息已过期
            LOG_WARN("Message deadline exceeded, dropping message");
            sched->deadline_queue->extract_min(sched->deadline_queue);
            
            // 记录丢包
            if (sched->monitor) {
                // 记录过期丢包
            }
            
            continue;
        }
        
        break;
    }
    
    ipc_mutex_unlock(sched->lock);
    
    // 从优先级队列获取消息
    return priority_scheduler_dequeue(sched->priority_sched);
}
```

## 8. QoS管理器设计

### 8.1 QoS管理器结构

```c
// QoS管理器
typedef struct ipc_qos_manager {
    // 配置
    ssn_qos_config_t default_config;
    ssn_qos_config_t node_qos;  // 节点级QoS
    
    // 可靠性模块
    ipc_reliability_module_t* reliability;
    
    // 调度器
    priority_scheduler_t* scheduler;
    
    // 带宽控制器
    token_bucket_t* send_bucket;
    token_bucket_t* recv_bucket;
    
    // 延迟保障
    latency_guarantee_scheduler_t* latency_sched;
    
    // 监控
    latency_monitor_t* latency_monitor;
    bandwidth_monitor_t* bw_monitor;
    
    // 消息历史
    ipc_history_buffer_t* history;
    
    // 统计
    ipc_qos_stats_t stats;
    
    // 同步
    ipc_mutex_t* lock;
    
    // 回调
    ipc_qos_callbacks_t callbacks;
    void* callback_arg;
} ipc_qos_manager_t;
```

### 8.2 QoS管理器接口

```c
// 创建QoS管理器
ipc_qos_manager_t* ipc_qos_manager_create(const ssn_qos_config_t* default_config);

// 配置QoS
bool ipc_qos_manager_set_config(ipc_qos_manager_t* mgr, const ssn_qos_config_t* config);
bool ipc_qos_manager_get_config(ipc_qos_manager_t* mgr, ssn_qos_config_t* config);

// 发送消息（应用QoS策略）
int ipc_qos_manager_send(ipc_qos_manager_t* mgr, ipc_message_t* msg);

// 接收消息（应用QoS策略）
int ipc_qos_manager_recv(ipc_qos_manager_t* mgr, ipc_message_t* msg, uint32_t timeout_ms);

// 获取统计信息
bool ipc_qos_manager_get_stats(ipc_qos_manager_t* mgr, ipc_qos_stats_t* stats);

// 重置统计信息
void ipc_qos_manager_reset_stats(ipc_qos_manager_t* mgr);

// 销毁QoS管理器
void ipc_qos_manager_destroy(ipc_qos_manager_t* mgr);
```

## 9. QoS监控和管理接口

### 9.1 统计信息

```c
// QoS统计信息
typedef struct {
    // 消息统计
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t messages_dropped;
    uint64_t messages_expired;
    
    // 可靠性统计
    uint64_t packets_sent;
    uint64_t packets_acked;
    uint64_t packets_retransmitted;
    uint64_t packets_duplicated;
    uint64_t packets_lost;
    
    // 延迟统计
    latency_stats_t latency;
    
    // 带宽统计
    bandwidth_stats_t bandwidth;
    
    // 优先级统计
    uint64_t messages_by_priority[IPC_PRIORITY_COUNT];
    
    // 时间戳
    time_t last_update;
} ipc_qos_stats_t;

// 获取QoS统计
static bool ipc_qos_manager_get_stats(ipc_qos_manager_t* mgr, ipc_qos_stats_t* stats) {
    if (!mgr || !stats) {
        return false;
    }
    
    ipc_mutex_lock(mgr->lock);
    
    // 复制统计信息
    memcpy(stats, &mgr->stats, sizeof(ipc_qos_stats_t));
    
    // 获取延迟统计
    if (mgr->latency_monitor) {
        latency_monitor_get_stats(mgr->latency_monitor, &stats->latency);
    }
    
    // 获取带宽统计
    if (mgr->bw_monitor) {
        bandwidth_monitor_get_stats(mgr->bw_monitor, &stats->bandwidth);
    }
    
    stats->last_update = time(NULL);
    
    ipc_mutex_unlock(mgr->lock);
    
    return true;
}
```

### 9.2 动态QoS调整

```c
// 动态QoS调整配置
typedef struct {
    bool enable_automatic_adjustment;    // 启用自动调整
    uint32_t adjustment_interval_sec;    // 调整间隔
    
    // 延迟阈值
    uint32_t latency_warning_threshold_ms;
    uint32_t latency_critical_threshold_ms;
    
    // 带宽阈值
    uint32_t bandwidth_warning_percent;
    uint32_t bandwidth_critical_percent;
    
    // 自动调整策略
    bool auto_adjust_priority;
    bool auto_adjust_reliability;
    bool auto_adjust_compression;
} ipc_qos_automatic_config_t;

// 自动QoS调整线程
static void* qos_auto_adjust_thread(void* arg) {
    ipc_qos_manager_t* mgr = (ipc_qos_manager_t*)arg;
    ipc_qos_automatic_config_t* config = &mgr->auto_config;
    
    while (mgr->running) {
        sleep(config->adjustment_interval_sec);
        
        ipc_mutex_lock(mgr->lock);
        
        // 检查延迟统计
        latency_stats_t latency;
        if (latency_monitor_get_stats(mgr->latency_monitor, &latency)) {
            if (latency.p95_latency_us > config->latency_critical_threshold_ms * 1000) {
                LOG_WARN("Critical latency detected: p95=%lums, adjusting QoS",
                        latency.p95_latency_us / 1000);
                
                // 提高关键消息的优先级
                if (config->auto_adjust_priority) {
                    mgr->default_config.priority = MAX(0, mgr->default_config.priority - 1);
                }
                
                // 启用压缩
                if (config->auto_adjust_compression && !mgr->default_config.enable_compression) {
                    mgr->default_config.enable_compression = true;
                }
            }
        }
        
        // 检查带宽统计
        bandwidth_stats_t bw;
        if (bandwidth_monitor_get_stats(mgr->bw_monitor, &bw)) {
            uint32_t usage_percent = (bw.current_rate_kbps * 100) / bw.max_rate_kbps;
            
            if (usage_percent > config->bandwidth_critical_percent) {
                LOG_WARN("Critical bandwidth usage: %d%%, adjusting QoS", usage_percent);
                
                // 降低非关键消息的优先级
                if (config->auto_adjust_priority) {
                    mgr->default_config.priority = MIN(IPC_PRIORITY_COUNT - 1, 
                                                       mgr->default_config.priority + 1);
                }
            }
        }
        
        ipc_mutex_unlock(mgr->lock);
    }
    
    return NULL;
}
```

## 10. 与现有系统集成

### 10.1 API兼容性

```c
// 兼容现有API的封装
typedef struct {
    // 原有API保持不变
    ssn_client_t* (*original_client_create)(ssn_client_msg_handler_t on_publish, void* arg);
    bool (*original_client_connect)(ssn_client_t* client, const char* ipc_path,
                                     const struct timespec *timeout);
    int (*original_client_call)(ssn_client_t* client, const ssn_url_ref_t *url, 
                                const ssn_data_ref_t *data,
                                ssn_client_rpcreply_handler_t callback, void *arg, 
                                uint64_t timeout_ms);
    
    // 新增QoS-aware API
    ipc_client_qos_t* (*qos_client_create)(ssn_client_msg_handler_t on_publish, void* arg,
                                           const ssn_qos_config_t* qos);
    bool (*qos_client_connect)(ipc_client_qos_t* client, const char* address,
                               const ssn_qos_config_t* qos, const struct timespec *timeout);
    int (*qos_client_call)(ipc_client_qos_t* client, const ssn_url_ref_t *url,
                          const ssn_data_ref_t *data,
                          ssn_client_rpcreply_handler_t callback, void *arg,
                          const ssn_qos_config_t* qos, uint64_t timeout_ms);
} ipc_qos_compatibility_layer_t;
```

### 10.2 配置迁移

```c
// 从现有配置迁移到QoS配置
static bool migrate_to_qos_config(const char* old_config_file, ssn_qos_config_t* new_config) {
    // 读取现有配置
    FILE* fp = fopen(old_config_file, "r");
    if (!fp) {
        return false;
    }
    
    // 设置默认QoS配置
    memcpy(new_config, &IPC_QOS_DEFAULT, sizeof(ssn_qos_config_t));
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        
        char key[64], value[192];
        if (sscanf(line, "%63[^=]=%191[^\n]", key, value) == 2) {
            // 映射到QoS配置
            if (strcmp(key, "timeout") == 0) {
                new_config->retry_timeout_ms = atoi(value);
            } else if (strcmp(key, "max_retries") == 0) {
                new_config->max_retries = atoi(value);
            } else if (strcmp(key, "priority") == 0) {
                new_config->priority = atoi(value);
            }
        }
    }
    
    fclose(fp);
    return true;
}
```

## 11. 总结

QoS服务质量框架为SSN提供了完善的服务质量保障机制，包括：

1. **多级可靠性**：从尽力而为到精确一次的可靠性保障
2. **优先级调度**：支持严格优先级和加权轮询等多种调度算法
3. **带宽控制**：令牌桶和漏桶算法实现精确的流量控制
4. **延迟保障**：延迟监控和保障机制确保关键消息的及时传输
5. **动态调整**：支持根据网络状况自动调整QoS策略

该框架与现有系统保持良好的兼容性，能够平滑集成到现有的SSN架构中。