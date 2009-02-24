#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include "libmemc.h"
#include "libmemctest.h"

#define MAXARGLEN 256
#define MAXARGS 15

void test_init(int argc, char **argv) {
    int c;
    /* process arguments */
    while ((c = getopt(argc, argv, "btvp:")) != -1) {
        switch (c) {
        case 'b': binary_protocol = 1;
            setenv("PROTOCOL", "Binary", 1);
            break;
        case 'p': memcached_path = optarg;
            break;
        case 't': textual_protocol = 1;
            setenv("PROTOCOL", "Textual", 1);
            break;
        case 'v': verbose = 1;
            break;
        default:
            fprintf(stderr, "Illegal argument \"%c\"\n", c);
            break;
        }
    }
}

void setItem(struct Item *item,
            uint64_t cas_id,
            const char *key,
            int keylen,
            uint32_t flags,
            void *data,
            size_t size,
            size_t exptime)
{
    item->cas_id = cas_id;
    item->key = key;
    item->keylen = keylen;
    item->flags = flags;
    if (item->data != NULL) {
        free(item->data);
    }
    item->data = NULL;
    item->errmsg = 0;
    item->exptime = exptime;
    
    if (size > 0) {
        item->data = malloc(size);
        if (item->data != NULL) {
            memcpy(item->data, data, size);
            item->size = size;
            item->errmsg = 0;
        } else {
            item->size = 0;
            char errmsg2[200];
            char key2[100];
            int k = keylen > 99 ? 99 : keylen;
            memcpy(key2, key, k);
            memset(key2 + k, 0 ,1);
            sprintf(errmsg2, "(libmemctest.c) setItem function: malloc failed for key: %s\n", key2);
            fprintf(stderr, errmsg2);
            fflush(stderr);
            item->errmsg = strdup(errmsg2);
        }        
    } else {
        item->size = 0;
    }
}

int ok_test(int ok_result, const char* ok_str, const char* not_ok_str)
{
    test_counter++;
    if (ok_result) {
        test_success_counter++;
        if (verbose)
            fprintf(stdout,"ok %d - %s\n", test_counter, ok_str);
    } else {
        fprintf(stdout,"not ok %d - %s\n", test_counter, not_ok_str);
    }
    return ok_result;
}

int test_report()
{
    if (test_success_counter == test_counter)
       fprintf(stdout,"    ok - All tests successful (Tests=%d)\n", test_counter);
    else
       fprintf(stdout,"%    !!! Some tests failed (Tests=%d) (Failed tests=%d)\n", test_counter, test_counter - test_success_counter);
}

int mem_get_is(struct Memcache* mc, const struct Item *item,
            const char* msg_ok, const char* msg_not_ok)
{
    struct Item item_recv = {0};
    item_recv.key = item->key;
    item_recv.keylen = item->keylen;
    int ret_get = libmemc_get(mc, &item_recv);

    return ok_test(
            ((item_recv.size == item->size) && 
            (item_recv.flags == item->flags) &&
            (!memcmp(item_recv.data, item->data, item->size))) ||
            ((item_recv.size == 0) && (item->data == NULL)),
            msg_ok, msg_not_ok);
}

int mem_gets_is(struct Memcache* mc, const struct Item *item,
            const char* msg_ok, const char* msg_not_ok)
{
    struct Item item_recv = {0};
    item_recv.key = item->key;
    item_recv.keylen = item->keylen;

    libmemc_get(mc, &item_recv);
    return ok_test(
            ((item_recv.size == item->size) && 
            (item_recv.flags == item->flags) &&
            (item_recv.cas_id == item->cas_id) &&
            (!memcmp(item_recv.data, item->data, item->size))) ||
            ((item_recv.size == 0) && (item->data == NULL)),
            msg_ok, msg_not_ok);
}

static struct addrinfo *lookuphost(const char *hostname, in_port_t port,
                                   char *type)
{
    struct addrinfo *ai = 0;
    struct addrinfo hints;
    char service[NI_MAXSERV];
    int error;

    hints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC;
    if (!strcmp(type,"tcp")) {
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_socktype = SOCK_STREAM;
    } else {
        hints.ai_protocol = IPPROTO_UDP;
        hints.ai_socktype = SOCK_DGRAM;        
    }
    
    (void)snprintf(service, NI_MAXSERV, "%d", port);
    if ((error = getaddrinfo(hostname, service, &hints, &ai)) != 0) {
        if (error != EAI_SYSTEM) {
            if (verbose)
                fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(error));
        } else {
            if (verbose)
                perror("getaddrinfo()");
        }
    }

    return ai;
}

int connect_server(const char *hostname, in_port_t port, char *type)
{
    struct addrinfo *ai = lookuphost(hostname, port, type);
    int sock = -1;
    if (ai != NULL) { 
        if ((sock = socket(ai->ai_family, ai->ai_socktype,
                          ai->ai_protocol)) != -1) {
            if (connect(sock, ai->ai_addr, ai->ai_addrlen) == -1) {
                if (verbose)
                    fprintf(stderr, "Failed to connect socket: %s\n", 
                            strerror(errno));
             close(sock);
             sock = -1;
          }
       } else {
            if (verbose)
                fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
       }
       freeaddrinfo(ai);
    }
    return sock;
}

int connect_server_unixsocket(const char *filename)
{
    struct sockaddr_unix {
        unsigned short family;  /* AF_UNIX */
        char path[256];
    } sa;
    int sock = -1;
    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) != -1) {
        sa.family = AF_UNIX;
        strcpy(sa.path, filename);
        int len = strlen(sa.path) + sizeof(sa.family);
        if (connect(sock, (struct sockaddr *)&sa, len) == -1) {
            if (verbose)
                fprintf(stderr, "Failed to connect socket: %s\n", strerror(errno));
            close(sock);
            sock = -1;
        }
    } else {
        if (verbose)
            fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
    }
    return sock;
}

in_port_t free_port(char *type)
{
    const char *hostname = "127.0.0.1";
    in_port_t port = 0;
    int sock = -1;
    
    while (sock < 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        srand((unsigned int) tv.tv_usec);
        port = (rand() % 20000) + 30000;

        struct addrinfo *ai = lookuphost(hostname, port, type);
        if (ai != NULL) {
            sock = socket(ai->ai_family, ai->ai_socktype,
                          ai->ai_protocol);
            if (sock != -1)
                close(sock);
        }
    }
    
    return port;
}

struct memcached_process_handle *new_memcached(int port, char* args)
{
    if (port <= 0) {
        port = free_port("tcp");
    }
    
    int udpport = free_port("udp");

    char argsbuffer [MAXARGS*(MAXARGLEN+1)];
    if (strlen(args))
        sprintf(argsbuffer, "%s -p %d -U %d", args, port, udpport);
    else
        sprintf(argsbuffer, "-p %d -U %d", port, udpport);

    int pid = fork();
    if (pid < 0) {
        perror("fork");
        return NULL;
    }

    if (pid==0) {
        char *argv[MAXARGS];
        for (int i=0; i<MAXARGS; i++) {
            argv[i] = malloc(MAXARGLEN * sizeof(char));
            argv[i] = (char *)0;
        }

        int argc=0;
        argv[0] = "memcached-debug";
        argc++;
        const char delimiters[] = " ";
        char *token = strtok(argsbuffer, delimiters);
        while ((token != NULL) && (argc<(MAXARGS-1))) {
            argv[argc] = token;
            token = strtok(NULL, delimiters);
            argc++;
        }

        if (token == NULL) {
            argv[argc] = (char *)0;
        } else {
            printf("Too many command line arguments\n");
            return NULL;
        }

        char path[256];
        sprintf(path, "%s/memcached-debug", memcached_path);
        execv(path, argv);
        perror(path);
        exit(0);
    }

    sleep(1);

    // get connection
    int sock;
    // unix domain sockets
    if (strstr(argsbuffer, "-s ")) {
        char filename[256];
        char *start = strstr(argsbuffer, "-s ") + 3;
        char *end = strstr(start, " ");
        memcpy(filename, start, end - start);
        memset(filename + (end - start), 0, 1);
        sock = connect_server_unixsocket(filename);
    } else {
        const char *hostname = "127.0.0.1";
        sock = connect_server(hostname, port, "tcp");
    }

    struct memcached_process_handle* handle = malloc(sizeof(*handle));
    handle->pid = pid;
    handle->port = port;
    handle->udpport = udpport;
    handle->socket = sock;
    handle->next = NULL;

    static struct memcached_process_handle* current_handle = NULL;
    if (!current_handle) {
        current_handle = handle;
        process_handle = handle;
        atexit(exit_cleanup);
    } else {
        current_handle->next = handle;
        current_handle = handle;
    }

    if (sock == -1) {
        return NULL;
    } else {
        return handle;
    }
}

int new_sock(struct memcached_process_handle *handle) {
        const char *hostname = "127.0.0.1";
        return connect_server(hostname, handle->port, "tcp");
}

int new_udp_sock(struct memcached_process_handle *handle) {
        const char *hostname = "127.0.0.1";
        return connect_server(hostname, handle->udpport, "udp");
}

void exit_cleanup(void)
{
    while (process_handle) {
        // Kill with SIGINT to enable test code coverage tools (gcov,tcov) to work with memcached.
        kill(process_handle->pid,2);
        // We must wait a while after SIGINT is sent to a memcached process.
        // This lets gcov finish its processing for that memcached process
        // before we send SIGINT to the next memcached process.
        // If we fail to wait between sending SIGINT to multiple memcached processes
        // gcov will cause the memcached processes to hang.
        // (This doesn't seem to be a problem with tcov).
        process_handle = process_handle->next;
        if (process_handle)
            sleep(2); // sleep 2 secs
    }
    exit(0);
}
