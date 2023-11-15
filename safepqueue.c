#include "safepqueue.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void init_priority_queue(PriorityQueue *pq, int capacity) {
    pq->elements = malloc(sizeof(PriorityQueueElement) * capacity);
    pq->size = 0;
    pq->capacity = capacity;
    pthread_mutex_init(&pq->mutex, NULL);
    pthread_cond_init(&pq->cond_var, NULL);
}

void enqueue(PriorityQueue *pq, const char *request, int priority) {
    pthread_mutex_lock(&pq->mutex);

    if (pq->size == pq->capacity) {
        printf("Queue is full, unable to enqueue request\n");
        pthread_mutex_unlock(&pq->mutex);
        return;
    }

    printf("Enqueuing request with priority %d\n", priority);
    pq->elements[pq->size].request = strdup(request);
    pq->elements[pq->size].priority = priority;
    pq->size++;

    // TODO: Implement priority insertion logic

    pthread_cond_signal(&pq->cond_var);
    pthread_mutex_unlock(&pq->mutex);
}

PriorityQueueElement dequeue(PriorityQueue *pq) {
    pthread_mutex_lock(&pq->mutex);

    while (pq->size == 0) {
        pthread_cond_wait(&pq->cond_var, &pq->mutex);
    }

    // TODO: Implement priority dequeue logic
    // For now, dequeueing the last element
    PriorityQueueElement element = pq->elements[pq->size - 1];
    pq->size--;

    pthread_mutex_unlock(&pq->mutex);
    return element;
}

void destroy_priority_queue(PriorityQueue *pq) {
    for (int i = 0; i < pq->size; i++) {
        free(pq->elements[i].request);
    }
    free(pq->elements);
    pthread_mutex_destroy(&pq->mutex);
    pthread_cond_destroy(&pq->cond_var);
}
