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

    struct Item item = {0};
    setItem(&item, 0, "foo", 3, 0, "fooval", 6, 0);
    int flags[3] = {0, 123, 65535};
    char ok_msg[50];
    char not_ok_msg[50];
    
    for (int i=0; i<3; i++) {
        item.flags = flags[i];
        ok_test(!libmemc_set(memcache, &item), "stored foo", "failed to store foo");
        sprintf(ok_msg, "got flags %d back", flags[i]);
        sprintf(not_ok_msg, "did not get flags %d back", flags[i]);
        mem_get_is(memcache, &item, ok_msg, not_ok_msg);
    }
    
    libmemc_destroy(memcache);
    test_report();
}
