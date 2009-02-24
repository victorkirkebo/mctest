#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "libmemc.h"
#include "libmemctest.h"

int main(int argc, char **argv)
{
    test_init(argc, argv);

    //2**32 + 32 , just over 4GB
    setenv("T_MEMD_INITIAL_MALLOC", "4294967328", 1);
    // don't preallocate slabs
    setenv("T_MEMD_SLABS_ALLOC", "0", 1);

    struct memcached_process_handle* mchandle = new_memcached(0, "-m 4098 -M");
    // start the server
    if (!mchandle) {
        fprintf(stderr,"Could not start memcached process\n\n");
        exit(0);
    }

    struct Memcache* memcache = libmemc_create(Automatic);

    if (libmemc_add_server(memcache, "127.0.0.1", mchandle->port) == -1) {
        fprintf(stderr,"Could not add server\n\n");
        exit(0);
    }

    char msg[100];
    // Test 1: is 64 bit
    char *stats = libmemc_stats(libmemc_get_server_no(memcache, 0), libmemc_get_protocol(memcache), NULL);
    char *ptr_size = (!stats) || (!strlen(stats)) ? NULL : strstr(stats, "pointer_size");
    if (ptr_size != NULL) {
        ptr_size += (strlen("pointer_size")+1);
        if (!memcmp(ptr_size,"32",2)) {
            fprintf(stdout, "Skipping 64-bit tests on 32-bit build\n");
            exit(0);
        }
        ok_test(!memcmp(ptr_size,"64",2), "is 64 bit", "is not 64 bit");
    } else {
        fprintf(stderr,"Could not find pointer_size in stats\n");
        fflush(stderr);
    }

    // Test 2: max bytes is 4098 MB
    char *ptr_maxbytes = (!stats) || (!strlen(stats)) ? NULL : strstr(stats, "limit_maxbytes");
    if (ptr_maxbytes != NULL) {
        ptr_maxbytes += (strlen("limit_maxbytes")+1);
        long long maxbytes = atoll(ptr_maxbytes);
        sprintf(msg, "max bytes = %d", maxbytes);
        ok_test(maxbytes == 4297064448, "max bytes is 4098 MB", msg);
    } else {
        fprintf(stderr,"Could not find limit_maxbytes in stats\n");
    }

    // Test 3: expected (faked) value of total_malloced
    stats = libmemc_stats(libmemc_get_server_no(memcache, 0), libmemc_get_protocol(memcache), "slabs");
    char *ptr_totmalloc = (!stats) || (!strlen(stats)) ? NULL : strstr(stats, "total_malloced");
    if (ptr_totmalloc != NULL) {
        ptr_totmalloc += (strlen("total_malloced")+1);
        long long total_malloced = atoll(ptr_totmalloc);
        sprintf(msg, "total_malloced = %d", total_malloced);
        ok_test(total_malloced == 4294967328,
                "expected (faked) value of total_malloced", msg);
    } else {
        fprintf(stderr,"Could not find total_malloced in stats\n\n");
    }

    // Test 4: no active slabs
    char *ptr_activeslabs = (!stats) || (!strlen(stats)) ? NULL : strstr(stats, "active_slabs");
    if (ptr_activeslabs != NULL) {
        ptr_activeslabs += (strlen("active_slabs")+1);
        long active_slabs = atol(ptr_activeslabs);
        sprintf(msg, "active_slabs = %d", active_slabs);
        ok_test(active_slabs == 0, "no active slabs", msg);
    } else {
        fprintf(stderr,"Could not find active_slabs in stats\n\n");
    }

    // Test 5: hit size limit
    int hit_limit = 0;
    size_t size = 400 * 1024;
    struct Item item = {0};
    setItem(&item, 0, "big0", 4, 0, NULL, 0, 0);
    item.data = malloc(size);
    
    if (item.data != NULL) {
        item.size = size;
        memset(item.data, 'a', size);

        for (int i=0; i<5; i++) {
            char key[5];
            sprintf(key, "big%d", i);
            item.key = key;
            if (libmemc_set(memcache, &item) == -1) {
                hit_limit = 1;
                break;
            }
        }
        free(item.data);
        item.data = NULL;
        item.size = 0;
    } else {
        fprintf(stdout,"Failed to allocate data\n");
    }

    ok_test(hit_limit, "hit size limit", "did not hit size limit");

    // Test 6: 1 active slab
    stats = libmemc_stats(libmemc_get_server_no(memcache, 0), libmemc_get_protocol(memcache), "slabs");
    ptr_activeslabs = (!stats) || (!strlen(stats)) ? NULL : strstr(stats, "active_slabs");
    if (ptr_activeslabs != NULL) {
        ptr_activeslabs += (strlen("active_slabs")+1);
        long active_slabs = atol(ptr_activeslabs);
        sprintf(msg, "active_slabs = %d", active_slabs);
        ok_test(active_slabs == 1, "1 active slab", msg);
    } else {
        fprintf(stderr,"Could not find active_slabs in stats\n\n");
    }

    libmemc_destroy(memcache);
    test_report();
}
