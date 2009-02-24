#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "libmemc.h"
#include "libmemctest.h"

int main(int argc, char **argv)
{
    // assuming max slab is 1M and default mem is 64M
    
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

    // create a big value for the largest slab
    char small[] = "a big value that's > .5M and < 1M. ";
    size_t max = 1024 * 1024;
    size_t totlen = strlen(small);
    while (totlen * 2 < max) {
        totlen = 2 * totlen;
    }

    char *big = malloc(totlen);
    memset(big, 'a', totlen);

    ok_test(totlen > (512 * 1024), "totlen > (512 * 1024)", "totlen <= (512 * 1024)");
    ok_test(totlen < (1024 * 1024), "totlen < (1024 * 1024)", "totlen >= (1024 * 1024)");

    // test that an even bigger value is rejected while we're here
    char *toobig = malloc(totlen * 3);
    memset(toobig, 'a', totlen * 3);
    struct Item item = {0};
    setItem(&item, 0, "too_big", 7, 0, toobig, totlen * 3, 0);
    ok_test(libmemc_set(memcache, &item) == -1, "too_big not stored", "too_big was stored");

    // set the big value
    setItem(&item, 0, "big", 3, 0, big, totlen, 0);
    ok_test(!libmemc_set(memcache, &item), "stored big", "failed to store big");
    mem_get_is(memcache, &item, "big found", "big not found");

    // no evictions yet
    char *stats = libmemc_stats(libmemc_get_server_no(memcache, 0), libmemc_get_protocol(memcache), NULL);
    char *ptr_evictions = (!stats) ? NULL : strstr(stats, "evictions");
    if (!ptr_evictions) {
        fprintf(stderr,"Could not find evictions in stats\n");
    } else {
        ok_test(atoi(ptr_evictions + strlen("evictions")+1) == 0, "no evictions to start", "evictions to start");
    }

    // set many big items, enough to get evictions
    for (int i=0; i<100; i++) {
        char key[20];
        sprintf(key,"item_%d", i);
        setItem(&item, 0, key, strlen(key), 0, big, totlen, 0);
        char msgOK[40];
        char msgNotOK[40];
        sprintf(msgOK,"stored %s", key);
        sprintf(msgNotOK,"Failed to store %s", key);
        ok_test(!libmemc_set(memcache, &item), msgOK, msgNotOK);
    }

    // some evictions should have happened
    stats = libmemc_stats(libmemc_get_server_no(memcache, 0), libmemc_get_protocol(memcache), NULL);
    ptr_evictions = (!stats) ? NULL : strstr(stats, "evictions");
    if (!ptr_evictions) {
        fprintf(stderr,"Could not find evictions in stats\n");
    } else {
        ok_test(atoi(ptr_evictions + strlen("evictions")+1) == 37, "some evictions happened", "wrong number of evictions happened");
    }

    // the first big value should be gone
    setItem(&item, 0, "big", 3, 0, NULL, 0, 0);
    mem_get_is(memcache, &item, "big == <undef>", "big != <undef>");
    int evicted = 36;


    // the other earliest items should be gone too        
    for (int i=0; i<evicted; i++) {
        char key[20];
        sprintf(key,"item_%d", i);
        setItem(&item, 0, key, strlen(key), 0, NULL, 0, 0);
        char msgOK[40];
        char msgNotOK[40];
        sprintf(msgOK,"%s == <undef>", key);
        sprintf(msgNotOK,"%s != <undef>", key);
        mem_get_is(memcache, &item, msgOK, msgNotOK);
    }

    // check that the non-evicted are the right ones
    for (int i=evicted; i<100; i++) {
        char key[20];
        sprintf(key,"item_%d", i);
        setItem(&item, 0, key, strlen(key), 0, big, totlen, 0);
        char msgOK[40];
        char msgNotOK[40];
        sprintf(msgOK,"%s found", key);
        sprintf(msgNotOK,"%s not found", key);
        ok_test(!libmemc_get(memcache, &item), msgOK, msgNotOK);
    }

    libmemc_destroy(memcache);
    test_report();
}
