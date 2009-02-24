#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
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
    ok_test(!libmemc_set(memcache, &item), "stored foo", "failed to store foo");
    mem_get_is(memcache, &item, "foo == 'fooval'", "foo != 'fooval'");
    ok_test(!libmemc_flush_all(memcache, -1), "did flush_all", "flush_all failed");    

    struct Item item2 = {0};
    setItem(&item2, 0, "foo", 3, 0, NULL, 0, 0);
    mem_get_is(memcache, &item2, "foo == <undef>", "foo != <undef>");

    // Test flush_all with zero delay.
    ok_test(!libmemc_set(memcache, &item), "stored foo", "failed to store foo");
    mem_get_is(memcache, &item, "foo == 'fooval'", "foo != 'fooval'");
    ok_test(!libmemc_flush_all(memcache, 0), "did flush_all", "flush_all failed");
    mem_get_is(memcache, &item2, "foo == <undef>", "foo != <undef>");

    // check that flush_all doesn't blow away items that immediately get set
    struct Item item3 = {0};
    setItem(&item3, 0, "foo", 3, 0, "new", 3, 0);
    ok_test(!libmemc_set(memcache, &item3), "stored foo = 'new'", "failed to store foo = 'new'");
    mem_get_is(memcache, &item3, "foo == 'new'", "foo != 'new'");

    // and the other form, specifying a flush_all time...
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ok_test(!libmemc_flush_all(memcache, tv.tv_sec + 2), "did flush_all in future", "flush_all in future failed");
    struct Item item4 = {0};
    setItem(&item4, 0, "foo", 3, 0, "1234", 4, 0);
    ok_test(!libmemc_set(memcache, &item4), "stored foo = '1234'", "failed to store foo = '1234'");
    mem_get_is(memcache, &item4, "foo == '1234'", "foo != '1234'");
    usleep(2200000); // sleep 2.2 secs
    mem_get_is(memcache, &item2, "foo == <undef>", "foo != <undef>");
    
    libmemc_destroy(memcache);
    test_report();
}
