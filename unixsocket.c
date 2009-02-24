#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libmemc.h"
#include "libmemctest.h"

int main(int argc, char **argv)
{
    test_init(argc, argv);
    
    char *filename = "/tmp/memcachetest$$";
    char mcargs[50];
    sprintf(mcargs, "-s %s", filename);
    // start the server
    struct memcached_process_handle* mchandle = new_memcached(0, mcargs);
    if (!mchandle) {
        fprintf(stderr,"Could not start memcached process\n\n");
		exit(0);
    }
    
    struct Memcache* memcache = libmemc_create(Automatic);
    if (libmemc_add_server(memcache, "127.0.0.1", mchandle->port) == -1) {
        fprintf(stderr,"Could not add server\n\n");
        exit(0);
    }

    libmemc_set_socket(libmemc_get_server_no(memcache, 0), mchandle->socket);

    struct Item item = {0};
    setItem(&item, 0, "foo", 3, 0, "fooval", 6, 0);
    ok_test(!libmemc_set(memcache, &item), "stored foo", "failed to store foo");
    mem_get_is(memcache, &item, "fooval", "failed to get fooval");

    unlink(filename);
    
    libmemc_destroy(memcache);
    test_report();
}
