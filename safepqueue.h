#ifndef SAFE_PQUEUE_H
#define SAFE_PQUEUE_H

#include <pthread.h>

// Structure for a single queue element
typedef struct {
    char *httpreq;
    char *path;     // The HTTP request
    int client_fd;
    int priority;      // Priority of the request
    int delay;     // Client file descriptor
} QueuedRequest;

// Structure for the priority queue
typedef struct {
    QueuedRequest **q_requests;     // Array of pointers to http_request
    int size;                    // Current number of elements in the queue
    int capacity;                // Maximum capacity of the queue
    pthread_mutex_t mutex;       // Mutex for thread synchronization
    pthread_cond_t cond_var;     // Condition variable for signaling
} PriorityQueue;

// Function declarations
void create_queue(PriorityQueue *pq, int capacity);
int add_work(PriorityQueue *pq, const char *path, int priority, int client_fd, int delay, const char *httpreq);
QueuedRequest *get_work(PriorityQueue *pq);
QueuedRequest *try_get_work(PriorityQueue *pq);

#endif // SAFE_PQUEUE_H