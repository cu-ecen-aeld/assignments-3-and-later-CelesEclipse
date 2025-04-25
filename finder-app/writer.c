#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    /* Setup system log */
    openlog(argv[0], LOG_CONS | LOG_PID | LOG_NDELAY, LOG_USER);

    /* Check the numbers of argument */
    if (argc != 3) {
        syslog(LOG_ERR, "Usage: %s <file> <writestr>\n", argv[0]);
        closelog();
        exit(1);
    }

    /* Create a new file using file I/O */
    int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        syslog(LOG_ERR, "Failed to open file: %s %s", argv[1], strerror(errno));
        closelog();
        exit(1);
    }
    syslog(LOG_INFO, "File created successfully: %s", argv[1]);

    /* Debug log for writing operation */
    syslog(LOG_DEBUG, "Writing \"%s\" to %s", argv[2], argv[1]);

    // Write the writestr to the file
    size_t sz = strlen(argv[2]);
    int wr = write(fd, argv[2], sz);
    if (wr == -1) {
        syslog(LOG_ERR, "Failed to write to file: %s %s", argv[1], strerror(errno));
        close(fd);
        closelog();
        exit(1);
    }
    syslog(LOG_INFO, "File written successfully %s", argv[1]);

    close(fd);
    closelog();
    return 0;
}
