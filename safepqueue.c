#include "safepqueue.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

FILE *file_q;

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

void create_queue(PriorityQueue *pq, int capacity) {
    pq->elements = malloc(sizeof(PriorityQueueElement) * capacity);
    pq->size = 0;
    pq->capacity = capacity;
    pthread_mutex_init(&pq->mutex, NULL);
    pthread_cond_init(&pq->cond_var, NULL);
}

void add_work(PriorityQueue *pq, const char *request, int priority, int client_fd) {
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

PriorityQueueElement get_work(PriorityQueue *pq) {
    FILE *file_q = fopen("q_log.txt", "w");
    if (file_q == NULL) {
        perror("Error opening q_log.txt");
    }

    fprintf(file_q, "In Dequeue\n");
    fflush(file_q);


    fprintf(file_q, "Checking size of Dequeue\n");
    fflush(file_q);

    if (pq->size == 0) {
        fprintf(file_q, "Queue is empty\n");
        fflush(file_q);
        fclose(file_q);
        pthread_mutex_unlock(&pq->mutex);

        PriorityQueueElement errorElement = {0};
        return errorElement;
    }

    fprintf(file_q, "Grabbing element\n");
    fflush(file_q);

    PriorityQueueElement element = pq->elements[pq->size - 1];
    pq->size--;

    fprintf(file_q, "Grabbed %d, %s\n", element.client_fd, element.request);
    fprintf(file_q, "Queue size after removal: %d\n", pq->size);
    fflush(file_q);
    fclose(file_q);

    return element;
}
