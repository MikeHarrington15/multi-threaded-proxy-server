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
void serve_request(QueuedRequest *request) {
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
        http_start_response(request->client_fd, QUEUE_FULL);
        return;
    }

    // successfully connected to the file server
    char *buffer = (char *)malloc(RESPONSE_BUFSIZE * sizeof(char));
    int ret = http_send_data(fileserver_fd, request->httpreq, strlen(request->httpreq));

    if (ret < 0) {
        fprintf(log_file, "Failed %lu after ret\n", (unsigned long)pthread_self());
        fflush(log_file);
    }

    while(1) {
        int bytes_read = recv(fileserver_fd, buffer, RESPONSE_BUFSIZE - 1, 0);
        if (bytes_read <= 0)
            break;
        ret = http_send_data(request->client_fd, buffer, bytes_read);
        if (ret < 0)
            send_error_response(request->client_fd, BAD_GATEWAY, "Bad Gateway");
    }

    // close the connection to the fileserver
    shutdown(fileserver_fd, SHUT_WR);
    close(fileserver_fd);

    // Free resources and exit
    free(buffer);
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
        fprintf(log_file, "%lu: Top of while loop in serve_forever\n", (unsigned long)pthread_self());
        fflush(log_file);
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);
        int *client_fd_ptr = malloc(sizeof(int));  // Dynamically allocate memory to pass to the thread

        fprintf(log_file, "%lu: 1\n", (unsigned long)pthread_self());
        fflush(log_file);

        *client_fd_ptr = accept(server_fd, (struct sockaddr *)&client_address, &client_address_length);
        if (*client_fd_ptr < 0) {
            fprintf(log_file, "%lu: 2.5\n", (unsigned long)pthread_self());
            fflush(log_file);
            perror("Error accepting socket");
            free(client_fd_ptr);  // Free the memory if accept fails
            continue;
        }

        fprintf(log_file, "%lu: 2\n", (unsigned long)pthread_self());
        fflush(log_file);

        printf("Accepted connection from %s on port %d\n",
               inet_ntoa(client_address.sin_addr),
               ntohs(client_address.sin_port));

        struct http_request *request = parse_client_request(*client_fd_ptr);

        fprintf(log_file, "%lu: 3 %s\n", (unsigned long)pthread_self(), request->path);
        fflush(log_file);

        if (strstr(request->path, GETJOBCMD) == NULL) {
            if (add_work(&pq, request->path, request->priority, request->client_fd, request->delay, request->httpreq)) {
    
                
                fprintf(log_file, "%lu: Enqueued %s with priority %d and delay: %d, Queue Size: %d\n", (unsigned long)pthread_self(), request->path, request->priority, request->delay,  pq.size);
                fflush(log_file);

            } else {
                fprintf(log_file, "Queue is full, cannot enqueue %s\n", request->path);
                fflush(log_file);

                http_start_response(*client_fd_ptr, QUEUE_FULL);
                http_send_header(*client_fd_ptr, "Content-Type", "text/plain");
                http_end_headers(*client_fd_ptr);
                char *message = "Error: Queue is full.";
                http_send_string(*client_fd_ptr, message);

                shutdown(request->client_fd, SHUT_WR);
                close(request->client_fd);

                fprintf(log_file, "%lu: Response sent %s\n",(unsigned long)pthread_self(), request->path);
                fflush(log_file);
            }
        } else {
            QueuedRequest *get_request = try_get_work(&pq);
            if (get_request) {
                fprintf(log_file, "%lu: GETJOB %s with priority %d\n", (unsigned long)pthread_self(), get_request->path, get_request->priority);
                fflush(log_file);

                http_start_response(get_request->client_fd, OK);
                http_send_header(get_request->client_fd, "Content-Type", "text/plain");
                http_end_headers(get_request->client_fd);
                http_send_string(get_request->client_fd, get_request->path);

                shutdown(get_request->client_fd, SHUT_WR);
                close(get_request->client_fd);

                fprintf(log_file, "%lu: GETJOB response sent %s\n",(unsigned long)pthread_self(), get_request->path);
                fflush(log_file);
            } else {
                fprintf(log_file, "Queue is empty, cannot dequeue %s\n", request->path);
                fflush(log_file);

                http_start_response(*client_fd_ptr, QUEUE_EMPTY);
                http_send_header(*client_fd_ptr, "Content-Type", "text/plain");
                http_end_headers(*client_fd_ptr);
                http_send_string(*client_fd_ptr, "Error: Queue is empty.");

                shutdown(request->client_fd, SHUT_WR);
                close(request->client_fd);

                fprintf(log_file, "Response sent for empty queue %s\n", request->path);
                fflush(log_file);
            }
            
        } 
        fprintf(log_file, "%lu: Reached end of while in serve_forever\n", (unsigned long)pthread_self());
        fflush(log_file);      
    }

    
    // Close the server socket
    // shutdown(server_fd, SHUT_RDWR);
    // close(server_fd);
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

void *worker_thread_function(void *arg) {
    while (1) {
        QueuedRequest *request = get_work(&pq);

        fprintf(log_file, "Worker Thread %lu dequeued %s\n", (unsigned long)pthread_self(), request->path);
        fflush(log_file);

        if (request->delay > 0) {
            fprintf(log_file, "Worker Thread %lu delaying %d\n", (unsigned long)pthread_self(), request->delay);
            fflush(log_file);
            sleep(request->delay); // Sleep for 'delay' seconds
            fprintf(log_file, "Worker Thread %lu done sleeping\n", (unsigned long)pthread_self());
            fflush(log_file);
        }

        fprintf(log_file, "Worker Thread %lu client fd %d\n", (unsigned long)pthread_self(), request->client_fd);
        fflush(log_file);

        // Serve the request
        serve_request(request); // Assuming serve_request is implemented to handle the request
        fprintf(log_file, "Worker Thread %lu served request %s\n", (unsigned long)pthread_self(), request->path);
        fflush(log_file);
    }

    return NULL;
}


int main(int argc, char **argv) {
    log_file = fopen("proxy_log.txt", "w");
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

    create_queue(&pq, max_queue_size);

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


    for (int i = 0; i < num_listener; i++) {
        pthread_join(threads[i], NULL);
    }

    for (int i = 0; i < num_workers; i++) {
        pthread_join(worker_threads[i], NULL);
    }

    free(worker_threads);
    free(threads);
    free(listener_ports);
    fclose(log_file);
    return EXIT_SUCCESS;
}