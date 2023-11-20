#include "safepqueue.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void create_queue(PriorityQueue *pq, int capacity) {
    pq->q_requests = malloc(capacity * sizeof(QueuedRequest *));
    pq->size = 0;
    pq->capacity = capacity;
    pthread_mutex_init(&pq->mutex, NULL);
    pthread_cond_init(&pq->cond_var, NULL);
}

int add_work(PriorityQueue *pq, const char *path, int priority, int client_fd, int delay, const char *httpreq) {
    pthread_mutex_lock(&pq->mutex);

    if (pq->size == pq->capacity) {
        printf("Queue is full, unable to enqueue request\n");
        pthread_mutex_unlock(&pq->mutex);
        return 0;
    }

    // Create and populate the new QueuedRequest object
    QueuedRequest *newRequest = malloc(sizeof(QueuedRequest));
    newRequest->httpreq = strdup(httpreq);
    newRequest->path = strdup(path); // Duplicate the path string
    newRequest->client_fd = client_fd;
    newRequest->priority = priority;
    newRequest->delay = delay;

    // Find the position to insert the new element
    int insertPos = pq->size;
    while (insertPos > 0 && pq->q_requests[insertPos - 1]->priority < priority) {
        pq->q_requests[insertPos] = pq->q_requests[insertPos - 1];
        insertPos--;
    }

    // Insert the new element
    pq->q_requests[insertPos] = newRequest;
    pq->size++;

    printf("Enqueued request %s with priority %d\n", path, priority);

    pthread_cond_signal(&pq->cond_var);
    pthread_mutex_unlock(&pq->mutex);
    return 1;
}


QueuedRequest *get_work(PriorityQueue *pq) {
    pthread_mutex_lock(&pq->mutex);

    while (pq->size == 0) {
        // Wait for the condition variable to be signaled
        pthread_cond_wait(&pq->cond_var, &pq->mutex);
    }

    // At this point, there is at least one item in the queue
    QueuedRequest *request = pq->q_requests[pq->size - 1];
    pq->size--;

    pthread_mutex_unlock(&pq->mutex);
    return request;
}

QueuedRequest *try_get_work(PriorityQueue *pq) {
    pthread_mutex_lock(&pq->mutex);

    QueuedRequest *request = NULL;
    if (pq->size > 0) {
        request = pq->q_requests[pq->size - 1];
        pq->size--;
    }

    pthread_mutex_unlock(&pq->mutex);
    return request;
}
