/*
 * Utility functions used by the server.c and server_unopt.c
 *
 * Author: Vamshi Reddy Konagari
 * Email: vkonagar@andrew.cmu.edu
 * Date: 2/19/2017
 */
#include <string.h>
#include <sys/sendfile.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <stdio.h>
#include <limits.h>
#include <sys/resource.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include "util.h"
#include "dlfcn.h"
#include "csapp.h"
#include "cache.h"

/* Statistics data */
static long request_cnt = 0;
static long reply_cnt = 0;
static pthread_mutex_t replycnt_mutex;
static cache_t* cache;

/* Creates a worker for static request */
void create_static_worker(int client_fd, void* (*func)(void*), char* res_name)
{
    pthread_t thread_id;
    request_item* item = create_static_request_item(res_name, client_fd);
    pthread_create(&thread_id, NULL, func, (void*) item);
}

void library_eviction_callback(cache_data_item_t* item)
{
    if (dlclose(item->value.value_data) < 0)
    {
        fprintf(stderr, "%s\n", dlerror());
    }
}

void* load_dyn_library(char* library_name)
{
    void* handle;
    void (*execute)(int fd, char* args[], int count);
    char* error;

    /* Dynamically load the library */
    /* Check if the library is already loaded. If it is */
    cache_key_t key;
    strcpy(key.key_data, library_name);
    cache_data_item_t* item = get_cached_data(cache, &key);
    if (item == NULL)
    {
        /* Library not present. */
        handle = dlopen(library_name, RTLD_LAZY);
        if (!handle)
        {
            fprintf(stderr, "%s\n", dlerror());
            return NULL;
        }
        /* Add it to cache */
        cache_entry_t* entry = get_new_cache_entry();
        entry->data = malloc(sizeof(cache_data_item_t));
        entry->data->value.value_data = handle;
        entry->delete_callback = library_eviction_callback;
        entry->data_size = 100;
        if (add_to_cache(cache, entry) == CACHE_INSERT_ERR)
        {
            Free(entry->data);
            Free(entry);
        }
    }
    return handle;
}

/* Loads and runs the required .so module for the request */
void handle_dynamic_exec_lib(int client_fd, char* resource_name)
{
    int path_len = MAX_DLL_NAME_LENGTH + strlen(CGIBIN_DIR_NAME) + MAX_PATH_CHARS;
    char lib_path[path_len];
    snprintf(lib_path, path_len, "./%s/%s.so", CGIBIN_DIR_NAME, resource_name);
    void* handle = load_dyn_library(lib_path);
    if (handle == NULL)
    {
        http_write_response_header(client_fd, HTTP_404);
    }
    else
    {
        /* Success */
        void (*func)(int) = dlsym(handle, "cgi_function");
        func(client_fd);
    }
}

/* Handler for static request type (html, txt, jpg, etc) */
void handle_static(int fd, char* resource_name)
{
    int path_len = MAX_RESOURCE_NAME_LENGTH + strlen(STATIC_DIR_NAME) + MAX_PATH_CHARS;
    char res_path[path_len];
    snprintf(res_path, path_len, "./%s/%s", STATIC_DIR_NAME, resource_name);
    /* Now read and write the resource */
    int filefd = open(res_path, O_RDONLY);
    if (filefd == -1)
    {
        perror("open");
        http_write_response_header(fd, HTTP_404);
        return;
    }
    http_write_response_header(fd, HTTP_200);
    int read_count;
    while ((read_count = sendfile(fd, filefd, 0, MAX_READ_LENGTH)) > 0);
    if (read_count == -1)
    {
        perror("sendfile");
    }
    Close(filefd);
}

int make_socket_non_blocking (int sfd)
{
    int flags, s;
    flags = fcntl (sfd, F_GETFL, 0);
    if (flags == -1)
    {
        perror ("fcntl");
        return -1;
    }
    flags |= O_NONBLOCK;
    s = fcntl (sfd, F_SETFL, flags);
    if (s == -1)
    {
        perror ("fcntl");
        return -1;
    }
    return 0;
}

int increase_fd_limit(int max_fds)
{
    /* Increase the max fd resource limit */
    struct rlimit res;
    res.rlim_cur = max_fds;
    res.rlim_max = max_fds;
    if(setrlimit(RLIMIT_NOFILE, &res) == -1)
    {
	    perror("Resource FD limit");
        return -1;
    }
    return 1;
}

int parse_port_number(int argc, char* argv)
{
    if (argc == SERVER_REQUIRED_CMD_ARG_COUNT)
    {
        errno = 0;
        int port = strtol(argv, NULL, 10);
        if (((port == LONG_MIN || port == LONG_MAX) && errno == ERANGE) || port == 0)
        {
            printf("Provide a valid port number\n");
            exit(EXIT_FAILURE);
        }
        return port;
    }
    return -1;
}

int create_listen_tcp_socket(int port, int backlog, int socket_shared)
{
    int sfd = Socket(AF_INET, SOCK_STREAM, 0);
    if (socket_shared)
    {
        /* This socket is shared by more than one thread */
        int optval = 1;
        setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    }
    /* Reuse the socket in multiple instantiations of the same server */
    int optval = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    /* Bind the socket to a port */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    server_addr.sin_family = AF_INET;
    Bind(sfd, (struct sockaddr*) &server_addr, sizeof(server_addr));
    Listen(sfd, backlog);
    return sfd;
}

/* API to create 'no_thread' of threads with 'func' */
int create_threads(int no_threads, void* (*func)(void*))
{
    int i;
    for(i=0; i<no_threads; i++)
    {
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, func, NULL);
    }
}

/* Create request items for communication between master and worker threads */
request_item* create_dynamic_request_item(char* name)
{
    request_item* item = malloc(sizeof(request_item));
    memset(item, 0, sizeof(item));
    sprintf(item->resource_name, "%s", name);
    return item;
}

/* Create request items for communication between master and worker threads */
request_item* create_static_request_item(char* name, int client_fd)
{
    request_item* item = malloc(sizeof(request_item));
    memset(item, 0, sizeof(item));
    sprintf(item->resource_name, "%s", name);
    item->client_fd = client_fd;
    return item;
}

void add_client_fd_to_epoll(int epollfd, int cli_fd)
{
    struct epoll_event event;
    epoll_conn_state* conn = malloc(sizeof(epoll_conn_state));
    conn->client_fd = cli_fd;
    conn->worker_fd = -1;
    conn->type = EVENT_OWNER_CLIENT;

    event.data.ptr = conn;
    event.events = EPOLLIN | EPOLLET | EPOLLHUP | EPOLLERR;
                    /* Edge triggered client.
                     * We get all the contents of a request from client in
                     * one shot */
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, cli_fd, &event) == -1)
    {
        perror("epoll add client fd");
        exit(EXIT_FAILURE);
    }
}


void add_worker_fd_to_epoll(int epollfd, int worker_fd, epoll_conn_state* cli_con)
{
    struct epoll_event event;
    epoll_conn_state* conn = malloc(sizeof(epoll_conn_state));
    conn->client_fd = cli_con->client_fd;
    conn->worker_fd = worker_fd;
    conn->type = EVENT_OWNER_WORKER;
    conn->client_con = cli_con;

    event.data.ptr = conn;
    event.events = EPOLLIN | EPOLLHUP | EPOLLERR; /* Level triggered
                                                     Worker generates a lot of
                                                     output. We won't get all of
                                                     it in one go */
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, worker_fd, &event) == -1)
    {
        perror("epoll add client fd");
        exit(EXIT_FAILURE);
    }
}

int send_to_worker_thread(request_item* reqitem)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("ERROR opening socket");
    }
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(WORKER_THREAD_PORT);
    if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)<=0)
    {
        dbg_printf("inet_pton error occured\n");
        return -1;
    }
    if (connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1)
    {
        perror("Connect to worker thread");
    }
    int a = rio_writen(sockfd, reqitem, sizeof(request_item));
    return sockfd;
}

/* All worker threads increment this. */
void increment_reply_count()
{
    pthread_mutex_lock(&replycnt_mutex);
    reply_cnt++;
    pthread_mutex_unlock(&replycnt_mutex);
}

/* All worker threads use this. */
long get_reply_count()
{
    pthread_mutex_lock(&replycnt_mutex);
    int res = (reply_cnt);
    pthread_mutex_unlock(&replycnt_mutex);
    return res;
}

/* Only main thread increments this. No mutex required */
long get_request_count()
{
    return request_cnt;
}

/* Only main thread increments this. No mutex required */
void increment_request_count()
{
    request_cnt++;
}

void init_stat_mutexes()
{
    if (pthread_mutex_init(&replycnt_mutex, NULL) != 0)
    {
        perror("mutex init");
        exit(EXIT_FAILURE);
    }
}

/* This presents the connection rate and other server performance metrics
 * every STAT_INTERVAL seconds */
void* statistics_thread(void* arg)
{
    if (pthread_detach(pthread_self()) == -1)
    {
        perror("Thread cannot be detached");
        return (void*)-1;
    }
    int replys = 0;
    int last_replys = 0;
    int last_requests = 0;
    while (1)
    {
        long replys = get_reply_count(&reply_cnt, &replycnt_mutex);
        long requests = request_cnt;
        printf("REQ: %ld\tREP: %ld\tREQ_Rate(/sec):%ld \tREP_Rate(/sec):%ld \n",
                requests, replys, (replys - last_replys) / STAT_INTERVAL,
                                    (requests - last_requests) / STAT_INTERVAL);
        last_replys = replys;
        last_requests = requests;
        sleep(STAT_INTERVAL);
    }
}

void create_stat_thread()
{
    create_threads(1, statistics_thread);
}

/*
 * Wrapper for pthread_rwlock_rdlock */
void Pthread_rwlock_rdlock(pthread_rwlock_t* lock)
{
    if (pthread_rwlock_rdlock(lock)!=0)
    {
        printf("Error obtaining a read lock\n");
        exit(EXIT_FAILURE);
    }
}

/*
 * Wrapper for pthread_rwlock_wrlock */
void Pthread_rwlock_wrlock(pthread_rwlock_t* lock)
{
    if (pthread_rwlock_wrlock(lock)!=0)
    {
        printf("Error obtaining a write lock\n");
        exit(EXIT_FAILURE);
    }
}

/*
 * Wrapper for pthread_rwlock_unlock */
void Pthread_rwlock_unlock(pthread_rwlock_t* lock)
{
    if (pthread_rwlock_unlock(lock)!=0)
    {
        printf("Error unlocking rdwr lock\n");
        exit(EXIT_FAILURE);
    }
}

void init_cache()
{
    cache = get_new_cache();
}
