#ifndef SAFE_PQUEUE_H
#define SAFE_PQUEUE_H

#include <pthread.h>

// Structure for a single queue element
typedef struct {
    char *request;   // The HTTP request
    int priority;    // Priority of the request
} PriorityQueueElement;

// Structure for the priority queue
typedef struct {
    PriorityQueueElement *elements;
    int size;
    int capacity;
    pthread_mutex_t mutex;      // Mutex for thread synchronization
    pthread_cond_t cond_var;    // Condition variable for signaling
} PriorityQueue;

// Function declarations
void init_priority_queue(PriorityQueue *pq, int capacity);
void enqueue(PriorityQueue *pq, const char *request, int priority);
PriorityQueueElement dequeue(PriorityQueue *pq);
void destroy_priority_queue(PriorityQueue *pq);

#endif // SAFE_PQUEUE_H