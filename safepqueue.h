#ifndef SAFE_PQUEUE_H
#define SAFE_PQUEUE_H

#include <pthread.h>

// Structure for a single queue element
typedef struct {
    char *request;     // The HTTP request
    int priority;      // Priority of the request
    int client_fd;     // Client file descriptor
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
void create_queue(PriorityQueue *pq, int capacity);
int is_queue_full(PriorityQueue *pq);
int is_queue_empty(PriorityQueue *pq);
void add_work(PriorityQueue *pq, const char *request, int priority, int client_fd);
PriorityQueueElement get_work(PriorityQueue *pq);

#endif // SAFE_PQUEUE_H