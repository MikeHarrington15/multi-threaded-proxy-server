# Multi-threaded Priority Queue Proxy Server

## Project Overview

A high-performance, multi-threaded HTTP proxy server implementation featuring a priority queue system for request handling. This project demonstrates advanced systems programming concepts including thread management, synchronization, and network programming in C.

## Key Features

- **Multi-threaded Architecture**
  - Configurable number of listener threads
  - Separate worker threads for request processing
  - Thread-safe priority queue implementation
- **Priority-based Request Handling**

  - Requests are processed based on priority levels
  - Support for delayed request execution
  - Queue size management to prevent overflow

- **Network Communication**

  - Full HTTP request/response handling
  - Support for multiple simultaneous connections
  - Efficient request forwarding to target servers

- **Robust Error Handling**
  - Comprehensive error reporting
  - Graceful handling of queue overflow
  - Clean shutdown mechanism

## Technical Implementation

- Written in C with POSIX threads
- Uses mutex locks and condition variables for synchronization
- Implements a custom thread-safe priority queue
- Features logging system for debugging and monitoring
- Supports dynamic port allocation

## Usage

### Prerequisites

- GCC compiler
- POSIX-compliant operating system
- Python 3.x (for testing server)

### Building the Project

```bash
make all
```

### Running the Server

Basic usage:

```bash
./proxyserver -l <num_listeners> <port1> <port2> ... -w <num_workers> -q <queue_size>
```

Example with specific configuration:

```bash
./proxyserver -l 2 8000 8001 -w 4 -i 127.0.0.1 -p 3333 -q 100
```

Parameters:

- `-l`: Number of listener threads followed by their ports
- `-w`: Number of worker threads
- `-i`: Target server IP address
- `-p`: Target server port
- `-q`: Maximum queue size

### Testing the Server

1. Start the test environment:

```bash
./create_disk.sh    # Create test environment
python3 -m http.server 3333  # Start test server
```

2. Clean up after testing:

```bash
./clean.sh  # Stops all running servers
```

## Architecture

```
Client Requests → Listener Threads → Priority Queue → Worker Threads → Target Server
```

- **Listener Threads**: Accept incoming connections and enqueue requests
- **Priority Queue**: Orders requests based on priority
- **Worker Threads**: Process requests in priority order
- **Target Server**: Receives forwarded requests and sends responses

## Error Handling

- Queue Full: Returns appropriate error message
- Connection Failures: Graceful error reporting
- Invalid Requests: Proper HTTP error responses

## Technical Highlights

- Thread-safe priority queue implementation
- Zero-copy request forwarding
- Efficient memory management
- Configurable threading model
- Comprehensive logging system

## Performance Considerations

- Priority-based scheduling ensures critical requests are handled first
- Multi-threading enables parallel request processing
- Thread pooling prevents resource exhaustion
- Configurable queue size for memory management

## Future Enhancements

- SSL/TLS support
- Request rate limiting
- Load balancing capabilities
- Extended metrics and monitoring
- Dynamic thread pool sizing

## Development Tools

- GCC compiler
- GDB debugger
- Python test framework
- Make build system

This project demonstrates proficiency in:

- Systems programming
- Multi-threaded application design
- Network programming
- Synchronization primitives
- Error handling
- Performance optimization
