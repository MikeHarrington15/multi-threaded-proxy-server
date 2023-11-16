#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "proxyserver.h"
#include "safepqueue.h"

/*
 * Constants
 */
#define RESPONSE_BUFSIZE 10000
#define REQUEST_BUFSIZE 10000

/*
 * Global configuration variables.
 * Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int num_listener;
int *listener_ports;
int num_workers;
char *fileserver_ipaddr;
int fileserver_port;
int max_queue_size;

PriorityQueue pq;
FILE *log_file;

void send_error_response(int client_fd, status_code_t err_code, char *err_msg) {
    http_start_response(client_fd, err_code);
    http_send_header(client_fd, "Content-Type", "text/html");
    http_end_headers(client_fd);
    char *buf = malloc(strlen(err_msg) + 2);
    sprintf(buf, "%s\n", err_msg);
    http_send_string(client_fd, buf);
    return;
}

/*
 * forward the client request to the fileserver and
 * forward the fileserver response to the client
 */
// Create multiple threads that run serve request
void serve_request(int client_fd) {

    // create a fileserver socket
    int fileserver_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fileserver_fd == -1) {
        fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
        exit(errno);
    }

    // create the full fileserver address
    struct sockaddr_in fileserver_address;
    fileserver_address.sin_addr.s_addr = inet_addr(fileserver_ipaddr);
    fileserver_address.sin_family = AF_INET;
    fileserver_address.sin_port = htons(fileserver_port);

    // connect to the fileserver
    int connection_status = connect(fileserver_fd, (struct sockaddr *)&fileserver_address,
                                    sizeof(fileserver_address));
    if (connection_status < 0) {
        // failed to connect to the fileserver
        printf("Failed to connect to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");
        return;
    }

    // successfully connected to the file server
    char *buffer = (char *)malloc(RESPONSE_BUFSIZE * sizeof(char));

    // forward the client request to the fileserver
    int bytes_read = read(client_fd, buffer, RESPONSE_BUFSIZE);
    int ret = http_send_data(fileserver_fd, buffer, bytes_read);
    if (ret < 0) {
        printf("Failed to send request to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");

    } else {
        // forward the fileserver response to the client
        while (1) {
            int bytes_read = recv(fileserver_fd, buffer, RESPONSE_BUFSIZE - 1, 0);
            if (bytes_read <= 0) // fileserver_fd has been closed, break
                break;
            ret = http_send_data(client_fd, buffer, bytes_read);
            if (ret < 0) { // write failed, client_fd has been closed
                break;
            }
        }
    }

    // close the connection to the fileserver
    shutdown(fileserver_fd, SHUT_WR);
    close(fileserver_fd);

    // Free resources and exit
    free(buffer);
}

int get_request_priority(const char *request) {
    const char *start = strchr(request, '/');
    if (start != NULL) {
        start++; // Move past the first '/'
        const char *end = strchr(start, '/');
        if (end != NULL) {
            size_t length = end - start;
            char *priorityStr = strndup(start, length);
            int priority = atoi(priorityStr);
            free(priorityStr);
            return priority;
        }
    }
    return -1;
}

int server_fd;

void *serve_forever(void *arg) {
    int listener_port = *(int *)arg;
    free(arg); // Free the dynamically allocated memory for the port number

    int server_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Failed to create a new socket");
        exit(errno);
    }

    // manipulate options for the socket
    int socket_option = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                   sizeof(socket_option)) == -1) {
        perror("Failed to set socket options");
        exit(errno);
    }

    struct sockaddr_in proxy_address;
    memset(&proxy_address, 0, sizeof(proxy_address));
    proxy_address.sin_family = AF_INET;
    proxy_address.sin_addr.s_addr = INADDR_ANY;
    proxy_address.sin_port = htons(listener_port); // use listener_port

    // bind the socket to the address and port number
    if (bind(server_fd, (struct sockaddr *)&proxy_address,
             sizeof(proxy_address)) == -1) {
        perror("Failed to bind on socket");
        exit(errno);
    }

    // starts waiting for the client to request a connection
    if (listen(server_fd, 1024) == -1) {
        perror("Failed to listen on socket");
        exit(errno);
    }

    printf("Listening on port %d...\n", listener_port);

    while (1) {
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);
        int *client_fd_ptr = malloc(sizeof(int));  // Dynamically allocate memory to pass to the thread

        *client_fd_ptr = accept(server_fd, (struct sockaddr *)&client_address, &client_address_length);
        if (*client_fd_ptr < 0) {
            perror("Error accepting socket");
            free(client_fd_ptr);  // Free the memory if accept fails
            continue;
        }

        printf("Accepted connection from %s on port %d\n",
               inet_ntoa(client_address.sin_addr),
               ntohs(client_address.sin_port));

        struct http_request *request = http_request_parse(*client_fd_ptr);

        if (strstr(request->path, "/GetJob") == NULL) {
            if (!is_queue_full(&pq)) {
                int priority = get_request_priority(request->path);
                char *path_copy = strdup(request->path);
                enqueue(&pq, path_copy, priority, *client_fd_ptr);
                
                fprintf(log_file, "%lu: Enqueued %s with priority %d\n", (unsigned long)pthread_self(), path_copy, priority);
                fflush(log_file);

                fprintf(log_file, "Queue Size: %d\n", pq.size);
                fflush(log_file);
            } else {
                fprintf(log_file, "Queue is full, cannot enqueue %s\n", request->path);
                fflush(log_file);
                send_error_response(*client_fd_ptr, QUEUE_FULL, http_get_response_message(QUEUE_FULL));
            }
        } else {
            if (!is_queue_empty(&pq)) {
                PriorityQueueElement dequeuedElement = dequeue(&pq);
                if (dequeuedElement.request != NULL) {
                    fprintf(log_file, "%lu: Dequeued %s with priority %d\n", (unsigned long)pthread_self(), dequeuedElement.request, dequeuedElement.priority);
                    fflush(log_file);
                    http_start_response(*client_fd_ptr, OK);
                    dprintf(*client_fd_ptr, "%s\n", dequeuedElement.request);
                }
            } else {
                fprintf(log_file, "Queue is empty, cannot dequeue %s\n", request->path);
                fflush(log_file);
                send_error_response(*client_fd_ptr, QUEUE_EMPTY, http_get_response_message(QUEUE_EMPTY));
            }
            
        }       
    }

    // Close the server socket
    shutdown(server_fd, SHUT_RDWR);
    close(server_fd);
}

/*
 * Default settings for in the global configuration variables
 */
void default_settings() {
    num_listener = 1;
    listener_ports = (int *)malloc(num_listener * sizeof(int));
    listener_ports[0] = 8000;

    num_workers = 1;

    fileserver_ipaddr = "127.0.0.1";
    fileserver_port = 3333;

    max_queue_size = 100;
}

void print_settings() {
    printf("\t---- Setting ----\n");
    printf("\t%d listeners [", num_listener);
    for (int i = 0; i < num_listener; i++)
        printf(" %d", listener_ports[i]);
    printf(" ]\n");
    printf("\t%d workers\n", num_listener);
    printf("\tfileserver ipaddr %s port %d\n", fileserver_ipaddr, fileserver_port);
    printf("\tmax queue size  %d\n", max_queue_size);
    printf("\t  ----\t----\t\n");
}

void signal_callback_handler(int signum) {
    printf("Caught signal %d: %s\n", signum, strsignal(signum));
    for (int i = 0; i < num_listener; i++) {
        if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
    }
    free(listener_ports);
    exit(0);
}

char *USAGE =
    "Usage: ./proxyserver [-l 1 8000] [-n 1] [-i 127.0.0.1 -p 3333] [-q 100]\n";

void exit_with_usage() {
    fprintf(stderr, "%s", USAGE);
    exit(EXIT_SUCCESS);
}

int extract_delay(const char *request) {
    // Placeholder implementation
    // Extract the delay from the request headers
    // This is an example and should be adapted to your actual header format
    int delay = 0;
    // Parse request to find the delay value
    return delay;
}

void *worker_thread_function(void *arg) {
    while (1) {
        PriorityQueueElement job;

        // Wait for and pop a job from the queue
        pthread_mutex_lock(&pq.mutex);
        while (pq.size == 0) {
            fprintf(log_file, "Worker Thread %lu waiting for queue to fill\n", (unsigned long)pthread_self());
            fflush(log_file);
            pthread_cond_wait(&pq.cond_var, &pq.mutex);
        }
        job = dequeue(&pq);
        fprintf(log_file, "Worker Thread %lu dequeued %s\n", (unsigned long)pthread_self(), job.request);
        fflush(log_file);
        pthread_mutex_unlock(&pq.mutex);

        // Process the job
        int delay = extract_delay(job.request); // Implement this function to extract the delay from the request headers
        if (delay > 0) {
            sleep(delay); // Sleep for 'delay' seconds
        }

        // Serve the request
        serve_request(job.client_fd); // Assuming serve_request is implemented to handle the request

        // Clean up (if necessary)
        free(job.request);
    }

    return NULL;
}


int main(int argc, char **argv) {
    log_file = fopen("log.txt", "w");
    if (!log_file) {
        perror("Error opening log file");
        return EXIT_FAILURE;
    }

    fprintf(log_file, "%sStarting new test%s\n", 
    "--------------------------------------------------",
    "--------------------------------------------------");
    fflush(log_file);
    // pthread_self(void);

    signal(SIGINT, signal_callback_handler);

    /* Default settings */
    default_settings();

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp("-l", argv[i]) == 0) {
            num_listener = atoi(argv[++i]);
            free(listener_ports);
            listener_ports = (int *)malloc(num_listener * sizeof(int));
            for (int j = 0; j < num_listener; j++) {
                listener_ports[j] = atoi(argv[++i]);
            }
        } else if (strcmp("-w", argv[i]) == 0) {
            num_workers = atoi(argv[++i]);
        } else if (strcmp("-q", argv[i]) == 0) {
            max_queue_size = atoi(argv[++i]);
        } else if (strcmp("-i", argv[i]) == 0) {
            fileserver_ipaddr = argv[++i];
        } else if (strcmp("-p", argv[i]) == 0) {
            fileserver_port = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
            exit_with_usage();
        }
    }
    print_settings();

    init_priority_queue(&pq, max_queue_size);

    pthread_t *worker_threads = malloc(num_workers * sizeof(pthread_t));
    if (!worker_threads) {
        perror("Failed to allocate memory for worker threads");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < num_workers; i++) {
        if (pthread_create(&worker_threads[i], NULL, worker_thread_function, NULL) != 0) {
            perror("Failed to create worker thread");
            exit(EXIT_FAILURE);
        }
        fprintf(log_file, "Created worker thread id: %lu\n", (unsigned long)worker_threads[i]);
        fflush(log_file);
    }

    pthread_t *threads = malloc(num_listener * sizeof(pthread_t));
    if (!threads) {
        perror("Failed to allocate memory for threads");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < num_listener; i++) {
        int *port_ptr = malloc(sizeof(int)); // Allocate memory for port number
        if (!port_ptr) {
            perror("Failed to allocate memory for port number");
            exit(EXIT_FAILURE);
        }

        *port_ptr = listener_ports[i];
        if (pthread_create(&threads[i], NULL, serve_forever, port_ptr) != 0) {
            perror("Failed to create listener thread");
            exit(EXIT_FAILURE);
        }
        // Log the thread ID
        fprintf(log_file, "Created listener thread id: %lu\n", (unsigned long)threads[i]);
        fflush(log_file);
    }

    fprintf(log_file, "Done creating listeners\n");
    fflush(log_file);

    for (int i = 0; i < num_listener; i++) {
        pthread_join(threads[i], NULL);
    }

    for (int i = 0; i < num_workers; i++) {
        pthread_join(worker_threads[i], NULL);
    }

    free(worker_threads);
    free(threads);
    free(listener_ports);
    destroy_priority_queue(&pq);
    fclose(log_file);
    return EXIT_SUCCESS;
}