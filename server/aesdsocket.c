#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <getopt.h>

#define PORT 9000
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"

volatile sig_atomic_t keep_running = 1;
int fd_file = -1;
int server_fd = -1;

void cleanup()
{
    syslog(LOG_INFO, "Shutting down server ...");
    if (fd_file >= 0) {
        close(fd_file);
    }
    if (server_fd >= 0) {
        close(server_fd);
    }
    unlink(DATA_FILE);
    closelog();
}

void signal_handler(int sig) 
{
    keep_running = 0;
    cleanup();
    exit(0);
}

int main(int argc, char* argv[]) 
{
    int client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[1024];
    int daemon_mode = 0;
    int daemon_opt;

    while ((daemon_opt = getopt(argc, argv, "d")) != -1) {
        switch (daemon_opt) {
            case 'd':
                daemon_mode = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
                break;
        }
    }
    
    // Setting up signal handlers for SIGINT and SIGTERM
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Open syslog
    openlog("aesdsocket", LOG_PID, LOG_DAEMON);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        syslog(LOG_ERR, "Socket creation failed");
        cleanup();
        return -1;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "Setsockopt failed");
        cleanup();
        return -1;
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "Bind failed");
        cleanup();
        return -1;
    }

    // Fork to daemonize if -d is specified
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed");
            cleanup();
            return -1;
        }
        if (pid > 0) {
            // Exit parent process
            cleanup();
            return 0;
        }

        // Continue child process
        umask(0);
        if (setsid() < 0) {
            syslog(LOG_ERR, "setsid failed");
            cleanup();
            return -1;
        }
        if (chdir("/") < 0) {
            syslog(LOG_ERR, "chdir failed");
            cleanup();
            return -1;
        }
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    // Listen for connections
    if (listen(server_fd, BACKLOG) < 0) {
        syslog(LOG_ERR, "Listen failed");
        cleanup();
        return -1;
    }

    // Open or create data file
    fd_file = open(DATA_FILE, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd_file < 0) {
        syslog(LOG_ERR, "Failed to open data file");
        cleanup();
        return -1;
    }

    while (keep_running) {
        // Accept connection
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (keep_running) {
                syslog(LOG_ERR, "Accept failed");
            }
            continue;
        }

        // Log connection
        syslog(LOG_INFO, "Accepted connection from port %d", PORT);

        // Receive and write data
        ssize_t bytes_read;
        int packet_complete = 0;
        while ((bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytes_read] = '\0';
            if (write(fd_file, buffer, bytes_read) != bytes_read) {
                syslog(LOG_ERR, "Write to file failed");
            }
            
            // Check for packet completion (newline)
            if (strchr(buffer, '\n')) {
                packet_complete = 1;
                break;
            }
        }

        if (bytes_read < 0 && keep_running) {
            syslog(LOG_ERR, "Receive failed");
        }

        // Send file contents back to client if packet is complete
        if (packet_complete) {
            lseek(fd_file, 0, SEEK_SET);
            while ((bytes_read = read(fd_file, buffer, sizeof(buffer) - 1)) > 0) {
                if (send(client_fd, buffer, bytes_read, 0) != bytes_read) {
                    syslog(LOG_ERR, "Send failed");
                    break;
                }
            }
        }

        // Close client connection and log
        close(client_fd);
        syslog(LOG_INFO, "Closed connection from port %d", PORT);
    }

    // Cleanup
    cleanup();
    return 0;
}
