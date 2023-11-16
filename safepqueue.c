#include "safepqueue.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int is_queue_full(PriorityQueue *pq) {
    pthread_mutex_lock(&pq->mutex);
    int isFull = (pq->size == pq->capacity);
    pthread_mutex_unlock(&pq->mutex);
    return isFull;
}

int is_queue_empty(PriorityQueue *pq) {
    pthread_mutex_lock(&pq->mutex);
    int isEmpty = (pq->size == 0);
    pthread_mutex_unlock(&pq->mutex);
    return isEmpty;
}

void init_priority_queue(PriorityQueue *pq, int capacity) {
    pq->elements = malloc(sizeof(PriorityQueueElement) * capacity);
    pq->size = 0;
    pq->capacity = capacity;
    pthread_mutex_init(&pq->mutex, NULL);
    pthread_cond_init(&pq->cond_var, NULL);
}

void enqueue(PriorityQueue *pq, const char *request, int priority, int client_fd) {
    pthread_mutex_lock(&pq->mutex);

    if (pq->size == pq->capacity) {
        printf("Queue is full, unable to enqueue request\n");
        pthread_mutex_unlock(&pq->mutex);
        return;
    }

    // Find the position to insert the new element
    int insertPos = 0;
    for (insertPos = pq->size; insertPos > 0; insertPos--) {
        if (pq->elements[insertPos - 1].priority > priority) {
            // Shift element back to make room for the new element
            pq->elements[insertPos] = pq->elements[insertPos - 1];
        } else {
            // Correct position found
            break;
        }
    }

    // Insert the new element
    pq->elements[pq->size].client_fd = client_fd;
    pq->elements[insertPos].request = strdup(request);
    pq->elements[insertPos].priority = priority;
    pq->size++;

    printf("Enqueued request with priority %d\n", priority);

    pthread_cond_signal(&pq->cond_var);
    pthread_mutex_unlock(&pq->mutex);
}

PriorityQueueElement dequeue(PriorityQueue *pq) {
    pthread_mutex_lock(&pq->mutex);

    while (pq->size == 0) {
        pthread_cond_wait(&pq->cond_var, &pq->mutex);
    }

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
