/* POSIX stand-ins for the few FreeRTOS calls used by the tested firmware files. */
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define TICK_MS 10

void vTaskDelay(unsigned ticks)
{
    usleep(ticks * TICK_MS * 1000);
}

struct start { void (*task)(void *); void *arg; };

static void *runner(void *pointer)
{
    struct start start = *(struct start *)pointer;

    free(pointer);
    start.task(start.arg);
    return NULL;
}

int xTaskCreate(void (*task)(void *), const char *name, int stack, void *arg, int priority, void *handle)
{
    (void)name; (void)stack; (void)priority; (void)handle;
    struct start *start = malloc(sizeof(*start));
    pthread_t thread;

    start->task = task;
    start->arg = arg;
    pthread_create(&thread, NULL, runner, start);
    pthread_detach(thread);
    return pdPASS;
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t ready;
    int length, item_size, head, count;
    char *items;
} queue_t;

QueueHandle_t xQueueCreate(int length, int item_size)
{
    queue_t *queue = calloc(1, sizeof(*queue));
    pthread_condattr_t attr;

    pthread_mutex_init(&queue->mutex, NULL);
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&queue->ready, &attr);
    queue->length = length;
    queue->item_size = item_size;
    queue->items = malloc((size_t)(length * item_size));
    return queue;
}

void vQueueDelete(QueueHandle_t handle)
{
    queue_t *queue = handle;

    free(queue->items);
    free(queue);
}

int xQueueSend(QueueHandle_t handle, const void *item, unsigned ticks)
{
    queue_t *queue = handle;
    int sent = 0;

    (void)ticks;
    pthread_mutex_lock(&queue->mutex);

    if (queue->count < queue->length) {
        int tail = (queue->head + queue->count) % queue->length;

        memcpy(queue->items + tail * queue->item_size, item, (size_t)queue->item_size);
        queue->count++;
        sent = 1;
        pthread_cond_signal(&queue->ready);
    }

    pthread_mutex_unlock(&queue->mutex);
    return sent;
}

int xQueueReceive(QueueHandle_t handle, void *item, unsigned ticks)
{
    queue_t *queue = handle;
    struct timespec deadline;
    int received = 0;

    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += ticks / 100;
    deadline.tv_nsec += (long)(ticks % 100) * TICK_MS * 1000000L;
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;

    pthread_mutex_lock(&queue->mutex);

    while (queue->count == 0) {
        if (ticks == portMAX_DELAY) {
            pthread_cond_wait(&queue->ready, &queue->mutex);
        } else if (pthread_cond_timedwait(&queue->ready, &queue->mutex, &deadline) == ETIMEDOUT) {
            break;
        }
    }

    if (queue->count > 0) {
        memcpy(item, queue->items + queue->head * queue->item_size, (size_t)queue->item_size);
        queue->head = (queue->head + 1) % queue->length;
        queue->count--;
        received = 1;
    }

    pthread_mutex_unlock(&queue->mutex);
    return received;
}
