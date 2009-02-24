#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libmemc.h"
#include "libmemctest.h"

int main(int argc, char **argv)
{
    test_init(argc, argv);
    
    // start the server
    struct memcached_process_handle* mchandle = new_memcached(0, "");
    if (!mchandle) {
        fprintf(stderr,"Could not start memcached process\n\n");
        exit(0);
    }
    
    struct Memcache* memcache = libmemc_create(Automatic);
    if (libmemc_add_server(memcache, "127.0.0.1", mchandle->port) == -1) {
        fprintf(stderr,"Could not add server\n\n");
        exit(0);
    }

    int sock = libmemc_get_socket(libmemc_get_server_no(memcache, 0));

    if (libmemc_get_protocol(memcache) == Textual) {
        // Test that commands can take 'noreply' parameter.
        char* sendbuffer = "flush_all noreply\r\n";
        send(sock, sendbuffer, strlen(sendbuffer), 0);
        sendbuffer = "flush_all 0 noreply\r\n";
        send(sock, sendbuffer, strlen(sendbuffer), 0);

        sendbuffer = "verbosity 0 noreply\r\n";
        send(sock, sendbuffer, strlen(sendbuffer), 0);

        sendbuffer = "add noreply:foo 0 0 1 noreply\r\n1\r\n";
        send(sock, sendbuffer, strlen(sendbuffer), 0);

        struct Item item = {0};
        setItem(&item, 0, "noreply:foo", 11, 0, "1", 1, 0);
        mem_get_is(memcache, &item, "1", "not 1");

        sendbuffer = "set noreply:foo 0 0 1 noreply\r\n2\r\n";
        send(sock, sendbuffer, strlen(sendbuffer), 0);
        setItem(&item, 0, "noreply:foo", 11, 0, "2", 1, 0);
        mem_get_is(memcache, &item, "2", "not 2");

        sendbuffer = "replace noreply:foo 0 0 1 noreply\r\n3\r\n";
        send(sock, sendbuffer, strlen(sendbuffer), 0);
        setItem(&item, 0, "noreply:foo", 11, 0, "3", 1, 0);
        mem_get_is(memcache, &item, "3", "not 3");

        sendbuffer = "append noreply:foo 0 0 1 noreply\r\n4\r\n";
        send(sock, sendbuffer, strlen(sendbuffer), 0);
        setItem(&item, 0, "noreply:foo", 11, 0, "34", 2, 0);
        mem_get_is(memcache, &item, "34", "not 34");

        sendbuffer = "prepend noreply:foo 0 0 1 noreply\r\n5\r\n";
        send(sock, sendbuffer, strlen(sendbuffer), 0);
        setItem(&item, 0, "noreply:foo", 11, 0, "534", 3, 0);
        mem_get_is(memcache, &item, "534", "not 534");

        setItem(&item, 0, "noreply:foo", 11, 0, NULL, 0, 0);
        libmemc_get(memcache, &item);
        sendbuffer = malloc(100);
        sprintf(sendbuffer, "cas noreply:foo 0 0 1 %llu noreply\r\n6\r\n", item.cas_id);
        send(sock, sendbuffer, strlen(sendbuffer), 0);
        setItem(&item, 0, "noreply:foo", 11, 0, "6", 1, 0);
        mem_get_is(memcache, &item, "6", "not 6");
        free(sendbuffer);

        sendbuffer = "incr noreply:foo 3 noreply\r\n";
        send(sock, sendbuffer, strlen(sendbuffer), 0);
        setItem(&item, 0, "noreply:foo", 11, 0, "9", 1, 0);
        mem_get_is(memcache, &item, "9", "not 9");

        sendbuffer = "decr noreply:foo 2 noreply\r\n";
        send(sock, sendbuffer, strlen(sendbuffer), 0);
        setItem(&item, 0, "noreply:foo", 11, 0, "7", 1, 0);
        mem_get_is(memcache, &item, "7", "not 7");

        sendbuffer = "delete noreply:foo noreply\r\n";
        send(sock, sendbuffer, strlen(sendbuffer), 0);
        setItem(&item, 0, "noreply:foo", 11, 0, NULL, 0, 0);
        mem_get_is(memcache, &item, "<undef>", "not <undef>");
    } else {
        fprintf(stdout, "Tests not implemented for binary protocol\n");
    }
    
    libmemc_destroy(memcache);
    test_report();
}
