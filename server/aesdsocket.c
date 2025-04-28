#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define PORT 9000
#define BACKLOG 5
#define WRITE_BUFFER_SIZE 1024
#define MAX_PACKET_SIZE 8192
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define CHUNK_SIZE 1024

int is_running = 1;

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        is_running = 0;
    }
}

int send_file_to_client(int socket, const char *file_path) {
    int file_fd = open(file_path, O_RDONLY);
    if (file_fd == -1) {
        perror("open file for reading");
        return -1;
    }

    char buffer[CHUNK_SIZE];
    ssize_t bytes_read;

    // Read the file in chunks and send it to the client
    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t bytes_sent = 0;
        while (bytes_sent < bytes_read) {
            ssize_t result = send(socket, buffer + bytes_sent, bytes_read - bytes_sent, 0);
            if (result == -1) {
                perror("send");
                close(file_fd);
                return -1;
            }
            bytes_sent += result;
        }
    }

    if (bytes_read == -1) {
        perror("read file");
        close(file_fd);
        return -1;
    }

    close(file_fd);
    return 0;
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid > 0) {
        // Parent process exits, child continues as daemon
        exit(0);
    }

    // Create new session and process group
    if (setsid() < 0) {
        perror("setsid");
        exit(1);
    }

    // Set file mode mask
    umask(0);

    // Change working directory to root
    if (chdir("/") < 0) {
        perror("chdir");
        exit(1);
    }

    // Close standard file descriptors (stdin, stdout, stderr)
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect stdin, stdout, stderr to /dev/null
    open("/dev/null", O_RDWR); // stdin
    open("/dev/null", O_RDWR); // stdout
    open("/dev/null", O_RDWR); // stderr
}

int main(int argc, char *argv[]) {
    int sockfd, acp_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[WRITE_BUFFER_SIZE];
    ssize_t bytes_received;
    int daemon_flag = 0;

    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
            case 'd':
                daemon_flag = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (daemon_flag) {
        daemonize();
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Create socket (IPv4, TCP)
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return -1;
    }

    // Allow reuse
    int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(sockfd);
        return -1;
    }

    // Setup server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    // Bind socket
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(sockfd);
        return -1;
    }

    // Listen on the socket
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        close(sockfd);
        return -1;
    }

    syslog(LOG_INFO, "Socket created and bound to port %d", PORT);

    // Accept a connection
    while (is_running) {
        acp_sock = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (acp_sock == -1) {
            if (is_running) {
                perror("accept");
            }
            continue;
        }

        // Log the accepted connection
        openlog("aesdsocket", LOG_PID, LOG_USER);
        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

        // Open the log file
        int fd = open(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1) {
            perror("open file");
            syslog(LOG_ERR, "Failed to open %s", FILE_PATH);
            close(acp_sock);
            continue;
        }

        // Receive data and write to file
        char *packet = NULL;
        size_t packet_len = 0;
        ssize_t total_received = 0;

        while ((bytes_received = recv(acp_sock, buffer, sizeof(buffer), 0)) > 0) {
            // Process each byte in the received buffer
            for (ssize_t i = 0; i < bytes_received; ++i) {
                // Check for newline (end of packet)
                if (buffer[i] == '\n') {
                    if (packet != NULL) {
                        if (write(fd, packet, packet_len) != packet_len) {
                            perror("write to file");
                            syslog(LOG_ERR, "Failed writing to %s", FILE_PATH);
                            free(packet);
                            close(fd);
                            close(acp_sock);
                            continue;
                        }

                        // Append newline after the packet in the file
                        write(fd, "\n", 1);
                        free(packet);
                    }

                    packet = NULL;
                    packet_len = 0;
                    // Send the file content back to the client
                    if (send_file_to_client(acp_sock, FILE_PATH) == -1) {
                        syslog(LOG_ERR, "Failed to send file contents to client");
                        close(fd);
                        close(acp_sock);
                        continue;
                    }
                } else {
                    // Allocate or reallocate memory for the packet
                    if (packet_len >= MAX_PACKET_SIZE) {
                        syslog(LOG_WARNING, "Packet size exceeded maximum length, discarding data.");
                        free(packet);
                        packet = NULL;
                        packet_len = 0;
                        break;
                    }

                    // Append current byte to the packet
                    char *new_packet = realloc(packet, packet_len + 1);
                    if (!new_packet) {
                        perror("malloc failed");
                        syslog(LOG_ERR, "Memory allocation failed for packet.");
                        free(packet);
                        close(fd);
                        close(acp_sock);
                        continue;
                    }

                    packet = new_packet;
                    packet[packet_len++] = buffer[i];
                }
            }
            total_received += bytes_received;
        }

        if (bytes_received == -1) {
            perror("recv");
            syslog(LOG_ERR, "Error receiving data");
        }

        if (packet != NULL && packet_len > 0) {
            write(fd, packet, packet_len);
            write(fd, "\n", 1);
            free(packet);
        }

        syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));

        close(fd);
        close(acp_sock);
    }

    // Close socket
    close(sockfd);

    // Delete file on exit
    if (remove(FILE_PATH) == 0) {
        syslog(LOG_INFO, "Deleted file %s", FILE_PATH);
    } else {
        syslog(LOG_WARNING, "Failed to delete file %s", FILE_PATH);
    }
    closelog();
    printf("Server shut down gracefully.\n");

    return 0;
}
