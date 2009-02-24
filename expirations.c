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
    
    // wait for early second
    struct timeval tv;
    while (1) {
        gettimeofday(&tv, NULL);
        if (tv.tv_usec < 500000)
            break;
        usleep(100000); // 1/10th of a second sleeps until time changes.
    }
    struct Item item = {0};
    // We choose expiry time = 2 secs instead of 1 sec.
    // 1 sec is too small for these tests since memcached updates current_time once a second
    // which means that an item will frequently expire in much less than a second if exptime=1
    setItem(&item, 0, "foo", 3, 0, "fooval", 6, 2);
    ok_test(!libmemc_set(memcache, &item), "stored foo", "failed to store foo");
    mem_get_is(memcache, &item, "foo == 'fooval'", "foo != 'fooval'");

    usleep(2500000); // sleep 2.5 secs
    struct Item item2 = {0};
    setItem(&item2, 0, "foo", 3, 0, NULL, 0, 0);
    mem_get_is(memcache, &item2, "foo == <undef>", "foo != <undef>");

    gettimeofday(&tv, NULL);
    item.exptime = tv.tv_sec - 1;
    ok_test(!libmemc_set(memcache, &item), "stored foo", "failed to store foo");
    mem_get_is(memcache, &item2, "already expired", "not expired");

    struct Item item3 = {0};
    setItem(&item3, 0, "foo", 3, 0, "foov+1", 6, 2);
    ok_test(!libmemc_set(memcache, &item3), "stored foo", "failed to store foo");
    mem_get_is(memcache, &item3, "foo == 'foov+1'", "foo != 'foov+1'");
    usleep(2500000); // sleep 2.5 secs
    struct Item item4 = {0};
    setItem(&item4, 0, "foo", 3, 0, NULL, 0, 0);
    mem_get_is(memcache, &item4, "now expired", "not expired");
    
    gettimeofday(&tv, NULL);
    struct Item item5 = {0};
    setItem(&item5, 0, "boo", 3, 0, "booval", 6, tv.tv_sec - 20);
    ok_test(!libmemc_set(memcache, &item3), "stored boo", "failed to store boo");
    struct Item item6 = {0};
    setItem(&item6, 0, "boo", 3, 0, NULL, 0, 0);
    mem_get_is(memcache, &item6, "now expired", "not expired");
    
    libmemc_destroy(memcache);
    test_report();
}
