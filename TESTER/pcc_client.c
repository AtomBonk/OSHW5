#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

/*
    1. validate the cmd args and detect errors while opening the file
       argc == 4
       argv[1] server's IP address (assume a valid IP address is provided)
       argv[2] server's port number (assume a 16-bit unsigned integer is provided)
       argv[3] path of the file to send (cant assume that its valid)

    2. flow
       a. open the specified file for reading
       b. create a TCP connection to the specified server port on the specified server IP
       c. transfer the contents of the file to the server over TCP and recieve the printable characters counts
          computed by the server in the following protocol:
            - client send N, the number of bytes that will be sent (32-bit unsigned integer)
            - the client sends the server N bytes from the file
            - the server responds with C - the number of printable characters from the bytes sent
       d. print the number of printable characters obrtained from the server to stdout using:abort
            printf("# of printable characters: %u\n", C);
       e. close the connection and exit with code 0


    NOTICE:
        use only system calls to access files (no C lib functions like fopen)
        N can be rep resented as a 32-bit unsigned integer
        use inet_pton() func for converting the server IP address from string to binary form
        buffer size for reading the file and sending it to the server is 1024 bytes
        on error: print error to stderr containing the errno string and exit with code 1
        no need for cleanup on exit

*/

int main(int argc, char *argv[]) {

    // check if the number of command line arguments is correct
    if (argc != 4) {
        fprintf(stderr, "Error: %s\n", strerror(EINVAL));
        exit(1);
    }

    // open the specified file for reading
    int file_fd = open(argv[3], O_RDONLY);
    if (file_fd < 0) {
        fprintf(stderr, "Error opening file: %s\n", strerror(errno));
        exit(1);
    }

    // create a TCP connection to the specified server port on the specified server IP
    
    int sock_fd = -1;
    char recv_buff[sizeof(uint32_t)]; // buffer for receiving data from server
    char send_buff[1024]; // buffer for sending data to server

    struct sockaddr_in serv_addr; // where we Want to get to

    memset(recv_buff, 0, sizeof(recv_buff));
    memset(send_buff, 0, sizeof(send_buff));
    

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Error creating socket: %s\n", strerror(errno));
        close(file_fd);
        exit(1);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; // IPv4
    // convert server IP address from string to binary form
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error converting IP address: %s\n", strerror(errno));
        close(file_fd);
        close(sock_fd);
        exit(1);
    }
    serv_addr.sin_port = htons(atoi(argv[2])); // convert port number to network byte order

    
    // connect socket to the target address
    if (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "Error: connect failed. %s \n", strerror(errno));
        close(file_fd);
        close(sock_fd);
        exit(1);
    }

    //printf("Connected to server %s:%s\n", argv[1], argv[2]);

    //transfer the contents of the file to the server over TCP
    // and receive the printable characters counts computed by the server

    // send the size of the file first (N)
    off_t file_size = lseek(file_fd, 0, SEEK_END);
    lseek(file_fd, 0, SEEK_SET); // reset file pointer to the beginning
    uint32_t N = htonl((uint32_t)file_size); // convert to network byte order
    
    ssize_t sent = 0;
    // send the size of the file (N) to the server
    // loop until all bytes are sent
    while (sent < sizeof(N)) {
        ssize_t r = write(sock_fd, ((char *)&N) + sent, sizeof(N) - sent);
        if (r < 0) {
            fprintf(stderr, "Error sending file size: %s\n", strerror(errno));
            close(file_fd);
            close(sock_fd);
            exit(1);
        }
        sent += r;
    }

    //printf("Sent file size: %u bytes\n", ntohl(N));
    // now send the file contents to the server
    ssize_t bytes_read;
    while ((bytes_read = read(file_fd, send_buff, sizeof(send_buff))) > 0) {
        ssize_t total_sent = 0;
        // loop until all bytes are sent
        while (total_sent < bytes_read) {
            ssize_t r = write(sock_fd, send_buff + total_sent, bytes_read - total_sent);
            if (r < 0) {
                fprintf(stderr, "Error sending file data: %s\n", strerror(errno));
                close(file_fd);
                close(sock_fd);
                exit(1);
            }
            total_sent += r;
        }
    }
    
    // check for read errors (0 means EOF, < 0 means error)
    if (bytes_read < 0) {
        fprintf(stderr, "Error reading file: %s\n", strerror(errno));
        close(file_fd);
        close(sock_fd);
        exit(1);
    }

    //printf("Sent file data: %zd bytes\n", file_size);

    // now receive the number of printable characters from the server
    uint32_t C = 0; // to store the number of printable characters
    size_t bytes_received = 0;
    while (bytes_received < sizeof(C)) {
        ssize_t r = read(sock_fd, recv_buff + bytes_received, sizeof(C) - bytes_received);
        //printf("Received %zd bytes from server\n", r);
        if (r <= 0) {
            fprintf(stderr, "Error receiving data from server: %s\n", strerror(errno));
            close(file_fd);
            close(sock_fd);
            exit(1);
    
        }
        bytes_received += r;
    
    }
   

    C = ntohl(*(uint32_t *)recv_buff); // convert from network byte order to host byte order
    printf("# of printable characters: %u\n", C);

    // close the file and socket
    close(file_fd);
    close(sock_fd);
    exit(0); // exit with code 0



}