/*
 * HTTP/1.0 compliant concurrent server
 *
 * Features
 * ********
 * 1. Implements HTTP/1.0 GET requests for static and dynamic content.
 * 2. Assumes one connection per request (no persistent connections).
 * 3. Uses the CGI protocol to serve dynamic content.
 * 4. Serves HTML (.html), image (.gif and .jpg), and text (.txt) files.
 * 5. Accepts a single command-line argument: the port to listen on.
 * 6. Implements concurrency using threads.
 */

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include "http_header.h"
#include "http.h"
#include "csapp.h"

#define DEFAULT_LISTEN_PORT 80
#define MAX_LISTEN_QUEUE 100
#define MAX_NAME_LENGTH 100
#define IO_BLOCK_SIZE 10

int get_resource_type(char* url, char* resource_name)
{
    if (sscanf(url, "/cgi-bin/%s", resource_name) == 1)
    {
        return RESOURCE_TYPE_CGI_BIN;
    }
    else if (sscanf(url, "/%s.html", resource_name) == 1)
    {
        return RESOURCE_TYPE_HTML;
    }
    else if (sscanf(url, "/%s.txt", resource_name) == 1)
    {
        return RESOURCE_TYPE_TXT;
    }
    else if (sscanf(url, "/%s.gif", resource_name) == 1)
    {
        return RESOURCE_TYPE_GIF;
    }
    else if (sscanf(url, "/%s.jpg", resource_name) == 1)
    {
        return RESOURCE_TYPE_JPG;
    }
    return RESOURCE_TYPE_UNKNOWN;
}

void handle_dynamic(int fd, char* resource_name)
{
    printf("Resource is %s\n", resource_name);
    http_write_response_header(fd, HTTP_200);
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1)
    {
        perror("pipe");
    }

    if (fork() == 0)
    {
        /* Child */
        close(STDOUT_FILENO);
        close(pipe_fds[0]);
        dup(pipe_fds[1]);
        char *newargv[] = { resource_name, NULL };
        if (execve(resource_name, newargv, NULL) == -1)
        {
            perror("exec");
            exit(EXIT_FAILURE);
        }
    }

    /* Parent. Wait for the child to finish */
    int status;
    if (wait(&status) == -1)
        perror("wait");

    close(pipe_fds[1]);

    if (WIFEXITED(status) && WEXITSTATUS(status) != EXIT_FAILURE)
    {
        char buf[4096];
        int read_cnt;
        while((read_cnt = read(pipe_fds[0], buf, 4096)) > 0)
        {
            write(fd, buf, read_cnt);
        }
    }
}

void handle_static(int fd, char* resource_name)
{
    /* Now read and write the resource */
    int filefd = open(resource_name, O_RDONLY);
    if (filefd == -1)
    {
        perror("open");
        http_write_response_header(fd, HTTP_404);
        return;
    }
    http_write_response_header(fd, HTTP_200);
    int read_count;
    while ((read_count = sendfile(fd, filefd, 0, IO_BLOCK_SIZE)) > 0);
    if (read_count == -1)
    {
        perror("sendfile");
        return;
    }
}

void handle_unknown(int fd, char* resource_name)
{
    printf("Unknown resource type\n");
}

void* client_handler(void* arg)
{
    int fd = *((int*)arg);
    free(arg);
    /* This thread is independent, its resources like stack, etc should
     * be freed automatically. */
    if (pthread_detach(pthread_self()) == -1)
    {
        perror("Thread cannot be detached");
        return (void*)-1;
    }

    http_header_t header;
    init_header(&header);
    http_scan_header(fd, &header);

    printf("HTTP request is %s %s %s\n",header.request_type,
                                        header.request_url,
                                        header.request_http_version);

    char resource_name[MAX_NAME_LENGTH];
    switch (get_resource_type(header.request_url, resource_name))
    {
        case RESOURCE_TYPE_CGI_BIN: handle_dynamic(fd, resource_name);
                                    break;
        case RESOURCE_TYPE_HTML:
        case RESOURCE_TYPE_TXT:
        case RESOURCE_TYPE_GIF:
        case RESOURCE_TYPE_JPG:
                                    handle_static(fd, resource_name);
                                    break;
        case RESOURCE_TYPE_UNKNOWN:
                                    handle_unknown(fd, resource_name);
                                    break;
    }
    close(fd);
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);
    int port = DEFAULT_LISTEN_PORT;
    if (argc == 2)
    {
        errno = 0;
        port = strtol(argv[1], NULL, 10);
        if (((port == LONG_MIN || port == LONG_MAX) && errno == ERANGE) || port == 0)
        {
            printf("Provide a valid port number\n");
        }
    }
    else
    {
        printf("Port not provided. Using the default port %d\n", DEFAULT_LISTEN_PORT);
    }

    /* Create a server socket */
    int server_sock = Socket(AF_INET, SOCK_STREAM, 0);

    /* Bind the socket to a port */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    server_addr.sin_family = AF_INET;
    Bind(server_sock, (struct sockaddr*) &server_addr, sizeof(server_addr));

    Listen(server_sock, MAX_LISTEN_QUEUE);

    /* Accept concurrent connections */
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    int size = 0;
    pthread_t thread_id;
    while (1)
    {
        int* client_fd = malloc(sizeof(int));
        *client_fd = Accept(server_sock, (struct sockaddr*) &client_addr, &size);
        pthread_create(&thread_id, NULL, client_handler, (void*) client_fd);
        printf("Connection from a client\n");
    }
    close(server_sock);
}
