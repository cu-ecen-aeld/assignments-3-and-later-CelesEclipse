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
#include <sys/queue.h>
#include <pthread.h>
#include <stdbool.h>

// Threaded client struct
struct client {
    int client_fd;
    pthread_t thread;
    bool complete;
    SLIST_ENTRY(client) entries;
};

SLIST_HEAD(client_list, client);
struct client_list client_head = SLIST_HEAD_INITIALIZER(client_head);

#define PORT 9000
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"

int server_fd = -1;
int data_fd = -1;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t running = 1;

// Cleanup resources
void cleanup() {
    syslog(LOG_INFO, "Shutting down server ...");
    if (server_fd >= 0) close(server_fd);
    if (data_fd >= 0) close(data_fd);
    unlink(DATA_FILE);
    closelog();
}

// Signal handler
void handle_signal(int sig) {
    running = 0;
    shutdown(server_fd, SHUT_RDWR);
}

// Thread handler
void* client_handler(void* arg) {
    struct client* client = (struct client*)arg;
    char buffer[1024];
    ssize_t rcv_len;
    bool packet_done = false;

    sleep(1);
    syslog(LOG_INFO, "thread started: client_fd = %d", client->client_fd);

    while ((rcv_len = recv(client->client_fd, buffer, sizeof(buffer), 0)) > 0) {
        pthread_mutex_lock(&mutex);
        if (write(data_fd, buffer, rcv_len) != rcv_len) {
            syslog(LOG_ERR, "Write failed");
        }
        pthread_mutex_unlock(&mutex);
        if (memchr(buffer, '\n', rcv_len)) {
            packet_done = true;
            break;
        }
    }

    if (rcv_len < 0) {
        syslog(LOG_ERR, "Receive failed");
    }

    if (packet_done) {
        pthread_mutex_lock(&mutex);
        lseek(data_fd, 0, SEEK_SET);
        while ((rcv_len = read(data_fd, buffer, sizeof(buffer))) > 0) {
            if (send(client->client_fd, buffer, rcv_len, 0) != rcv_len) {
                syslog(LOG_ERR, "Send failed");
                break;
            }
        }
        pthread_mutex_unlock(&mutex);
    }

    close(client->client_fd);
    syslog(LOG_INFO, "Close connection on fd %d", client->client_fd);
    client->complete = true;
    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    int daemon_mode = 0;
    int opt;

    while ((opt = getopt(argc, argv, "d")) != -1) {
        if (opt == 'd') daemon_mode = 1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    struct sockaddr_in srv_addr = {0};
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        syslog(LOG_ERR, "Socket failed");
        cleanup();
        return EXIT_FAILURE;
    }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) {
        syslog(LOG_ERR, "Bind failed");
        cleanup();
        return EXIT_FAILURE;
    }

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed");
            cleanup();
            return EXIT_FAILURE;
        }
        if (pid > 0) {
            exit(EXIT_SUCCESS);
        }

        umask(0);
        setsid();
        chdir("/");
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    if (listen(server_fd, BACKLOG) < 0) {
        syslog(LOG_ERR, "Listen failed");
        cleanup();
        return EXIT_FAILURE;
    }

    data_fd = open(DATA_FILE, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (data_fd < 0) {
        syslog(LOG_ERR, "Failed to open data file");
        cleanup();
        return EXIT_FAILURE;
    }

    while (running) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&cli_addr, &cli_len);

        if (client_fd < 0) {
            if (running) syslog(LOG_ERR, "Accept failed");
            continue;
        }

        struct client* new_client = calloc(1, sizeof(struct client));
        new_client->client_fd = client_fd;
        new_client->complete = false;
        SLIST_INSERT_HEAD(&client_head, new_client, entries);

        if (pthread_create(&new_client->thread, NULL, client_handler, new_client) != 0) {
            syslog(LOG_ERR, "Thread creation failed");
            close(client_fd);
            SLIST_REMOVE(&client_head, new_client, client, entries);
            free(new_client);
            continue;
        }

        syslog(LOG_INFO, "Accepted connection from port %d", PORT);
    }

    // Join and free all threads
    struct client *cp;
    SLIST_FOREACH(cp, &client_head, entries) {
        pthread_join(cp->thread, NULL);
        SLIST_REMOVE(&client_head, cp, client, entries);
        free(cp);
    }

    cleanup();
    return EXIT_SUCCESS;
}
