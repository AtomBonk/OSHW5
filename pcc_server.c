#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>



/*
    argv[1] server's port number (assume a 16-bit unsigned integer is provided)
    need to validate the right number of cmd args

    printable chars are chars b such that 32 <= b <= 126
    

    flow:
    1. init a data structure pcc_total that will count how many times each printable char was oserved in all clients streams
        each count is a 32-bit unsigned integer
    2. create a TCP socket and bind it to the specified port number
        listen for incoming connections on the given port number, queue size 10
    3. enter a loop. in each iter:
        a. accept a new connection from a client
        b. when a connection is accepeted read a stream of bytes from the client
            compute the number of printable characters in the stream
            send the number of printable characters in the stream to the client
            update the pcc_total struct with the counts of each printable char
    4. if the server receives a SIGINT signal (Ctrl+C):
        a. if the server is processing a client when SIGINT is delivered, it should finish processing that client (incl updating struct)
        b. for each printable char in the pcc_total struct, print the char and its count to stdout in the following format:
            printf("char '%c' : %u times\n", chatr, count);
            only print chars that were observed at least once
            print the chars in ascending order of their ASCII values
        c. exit with code 0
    
    NOTICE:
        handling of SIGINT should be atomic with respect to processing of client requests
        processing of a client is defind as the period between returning from accept() until closing its socket.

        a tcp error occurs iff a system call sending/rec data to/from a client returns an error with errno being EPIPE or ECONNRESET or ETIMEDOUT.

*/

static volatile sig_atomic_t interrupted = 0; // flag to indicate if the server was interrupted by a signal
static volatile int conn_fd = -1; // connection file descriptor, initialized to -1
static uint32_t pcc_total[95] = {0}; // global array to hold the counts of printable characters, initialized to 0


void handle_sigint(int sig) {
    // this function will be called when the server receives a SIGINT signal
    interrupted = 1; // set the interrupted flag to indicate that the server was interrupted
    if (conn_fd == -1) {
        // if there is no active connection print the counts and exit
        // print the counts of printable characters in pcc_total
        for (size_t i = 0; i < 95; i++) {
            if (pcc_total[i] > 0) {
                printf("char '%c' : %u times\n", (char)(i + 32), pcc_total[i]);
            }
        }
        exit(0); // exit with code 0
        
    }
    

}



int main(int argc, char *argv[]) {
    // TODO: maybe fix prints when r == 0, r < 0 for reads/writes
    // EINTR handling?
    // r==0 r< 0 in pcc_client.c !!!!!!
    // retry on EINTR for read/write, accept should not retry on EINTR
    
    // register the signal handler for SIGINT
    struct sigaction sa;
    sa.sa_handler = handle_sigint; // set the handler function
    sa.sa_flags = 0; // no special flags
    sigemptyset(&sa.sa_mask); // no additional signals to block while handling SIGINT
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        fprintf(stderr, "Error setting up signal handler: %s\n", strerror(errno));
        exit(1);
    }
    
    

    // check if the number of cmd args is correct
    if (argc != 2){
        fprintf(stderr, "Error: %s\n", strerror(EINVAL));
        exit(1);
    }

    

    // create a TCP socket and bind it to the specified port number
    struct sockaddr_in serv_addr; // server address structure
    struct sockaddr_in peer_addr; // client address structure
    socklen_t addrsize = sizeof(struct sockaddr_in);
    int sock_fd = -1;
    char recv_buff[1024]; // buffer for receiving data from server
    char send_buff[sizeof(uint32_t)]; // buffer for sending data to server

    memset(recv_buff, 0, sizeof(recv_buff));
    memset(send_buff, 0, sizeof(send_buff));
    

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Error creating socket: %s\n", strerror(errno));
        exit(1);
    }

    memset(&serv_addr, 0, addrsize);
    serv_addr.sin_family = AF_INET; // IPv4
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // bind to any local address
    serv_addr.sin_port = htons(atoi(argv[1])); // convert port number to network byte order

    int optval = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind(sock_fd, (struct sockaddr *)&serv_addr, addrsize) < 0) {
        fprintf(stderr, "Error binding socket: %s\n", strerror(errno));
        close(sock_fd);
        exit(1);
    }

    if (listen(sock_fd, 10) < 0) {
        fprintf(stderr, "Error listening on socket: %s\n", strerror(errno));
        close(sock_fd);
        exit(1);
    }
    

    // enter a loop to accept and process client connections
    while (!interrupted) {
        //printf("Server is listening on port %s...\n", argv[1]);
        // Accept a connection
        conn_fd = accept(sock_fd, (struct sockaddr *)&peer_addr, &addrsize);
        //printf("Accepted connection from %s:%d\n", inet_ntoa(peer_addr.sin_addr), ntohs(peer_addr.sin_port));
        if (conn_fd < 0) {
            fprintf(stderr, "Error accepting connection: %s\n", strerror(errno));
            close(sock_fd);
            exit(1);
        }
        //printf("Accepted connection from %s:%d\n", inet_ntoa(peer_addr.sin_addr), ntohs(peer_addr.sin_port));
        
        // Read data from the client

        // first get N from the client
        uint32_t N = 0; // to store the size of the stream
        
        ssize_t bytes_received = 0;
        while (bytes_received < sizeof(N)) {
            ssize_t r = read(conn_fd, ((char *)&N) + bytes_received, sizeof(N) - bytes_received);
            if (r < 0) {
                if (errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE) {
                    fprintf(stderr, "TCP error occurred while reading from client: %s\n", strerror(errno));
                    close(conn_fd);
                    conn_fd = -1; // reset the connection fd for the next iteration
                    break; // exit the loop to handle the next client
                }
                else {
                    // if the error is not a TCP error, print the error and exit
                    fprintf(stderr, "Error reading from client: %s\n", strerror(errno));
                    close(conn_fd);
                    exit(1);
                }
            } 
            // if r == 0, it means the client disconnected before sending data
            else if (r == 0) {
                fprintf(stderr, "Client disconnected before sending data\n");
                close(conn_fd);
                break;
            }
            
            bytes_received += r;
        }
        if (bytes_received < sizeof(N)) {
            continue; // if we didn't receive enough bytes - this is because the client disconnected
        }

        N = ntohl(N); // convert from network byte order to host byte order
        //printf("Received N from client: %u\n", N);


        // now read the stream of bytes from the client
        if (N == 0) {
            close(conn_fd);
            continue; // skip to the next client
        }

        uint32_t curr_cnts[95];
        memset(curr_cnts, 0, sizeof(curr_cnts)); // initialize counts for this client to 0

        uint32_t C = 0; // to store the number of printable characters
        ssize_t bytes_read;
        bytes_received = 0;
        while (bytes_received < N  && (bytes_read = read(conn_fd, recv_buff, sizeof(recv_buff))) > 0) {
            //printf("Received data: %s\n", recv_buff);
            // Count printable characters
            for (size_t i = 0; i < bytes_read; i++) {
                if (32 <= recv_buff[i] && recv_buff[i] <= 126) {
                    curr_cnts[recv_buff[i] - 32]++; // increment the count for the printable character
                    C++; // increment the total count of printable characters
                    //printf("Received char '%c' from client\n", recv_buff[i]);
                }

            }
            bytes_received += bytes_read;
        
        }


        if (bytes_read == 0 && bytes_received < N) {
            fprintf(stderr, "Client disconnected before sending all data\n");
            close(conn_fd);
            conn_fd = -1; // reset the connection fd for the next iteration
            continue; // skip to the next client
        }

        if (bytes_read < 0) {
            if (errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE) {
                fprintf(stderr, "TCP error occurred while reading from client: %s\n", strerror(errno));
                close(conn_fd);
                conn_fd = -1; // reset the connection fd for the next iteration
                continue; // exit the loop to handle the next client
            }
            else {
                // if the error is not a TCP error, print the error and exit
                fprintf(stderr, "Error reading from client: %s\n", strerror(errno));
                close(conn_fd);
                exit(1);
            }
        }
        
        //printf("Received %zd bytes from client\n", bytes_received);
        // Send C to the client
        
        uint32_t C_net = htonl(C); // convert to network byte order
        ssize_t sent = 0;
        while (sent < sizeof(C_net)) {
            ssize_t r = write(conn_fd, ((char *)&C_net) + sent, sizeof(C_net) - sent);
            
            if (r < 0) {
                if (errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE) {
                    fprintf(stderr, "TCP error occurred while sending to client: %s\n", strerror(errno));
                    close(conn_fd);
                    conn_fd = -1; // reset the connection fd for the next iteration
                    break; // exit the loop to handle the next client
                }
                else {
                    // if the error is not a TCP error, print the error and exit
                    fprintf(stderr, "Error sending to client: %s\n", strerror(errno));
                    close(conn_fd);
                    exit(1);
                }
            } 
            // if r == 0, it means the client disconnected before reading data
            else if (r == 0) {
                fprintf(stderr, "Client disconnected before reading data\n");
                close(conn_fd);
                break;
            }
            sent += r;
        }

        if (sent < sizeof(C_net)) {
            continue; // skip to the next client
        }

        //printf("Sent C to client: %u\n", C);

        // Update the global pcc_total counts
        for (size_t i = 0; i < 95; i++) {
            pcc_total[i] += curr_cnts[i]; // add the counts from this client
        }

        // Close the client connection
        close(conn_fd);
        conn_fd = -1; // reset the connection fd for the next iteration
    }

    // print the counts of printable characters in pcc_total
    for (size_t i = 0; i < 95; i++) {
        if (pcc_total[i] > 0) {
            printf("char '%c' : %u times\n", (char)(i + 32), pcc_total[i]);
        }
    }

    exit(0);


}