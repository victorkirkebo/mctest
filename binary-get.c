#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
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

    char* data[] = {"mooo\0", "mumble\0\0\0\0\r\rblarg", "\0", "\r"};
    int datasize[] = {5, 17, 1, 1};

    for (int i=0; i<4; i++) {
        struct Item item = {0};
        char key[5];
        sprintf(key, "foo%d", i);
        setItem(&item, 0, key, strlen(key), 0, data[i], datasize[i], 0);
        char msg_ok[50];
        char msg_not_ok[50];
        sprintf(msg_ok, "stored %s", key);
        sprintf(msg_not_ok, "failed to store %s", key);
        ok_test(libmemc_set(memcache, &item) != -1,
                msg_ok, msg_not_ok);

        struct Item item_recv = {0};
        setItem(&item_recv, 0, key, strlen(key), 0, NULL, 0, 0);
        libmemc_get(memcache, &item_recv);
        sprintf(msg_ok, "%s == '%s'", key, data[i]);
        sprintf(msg_not_ok, "%s != '%s'", key, data[i]);
        ok_test(!memcmp(item_recv.data, data[i], datasize[i]),
                msg_ok, msg_not_ok);
    }

    libmemc_destroy(memcache);
    test_report();
}
