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

    char *stats = libmemc_stats(libmemc_get_server_no(memcache, 0), libmemc_get_protocol(memcache), "");

    int count = 0;
    char *ptr = (!stats) || (strlen(stats) == 0) ? NULL : strstr(stats, "\r\n");
    while (ptr != NULL) {
        count++;
        ptr +=2;
        ptr = strstr(ptr, "\r\n");
    }

    if (libmemc_get_protocol(memcache) == Textual)
        count--; //due to an extra "END\r\n"
    ok_test(count == 22, "22 stats values", "not 22 stats values");

    char *keys1[] = {"curr_items", "total_items", "bytes", "cmd_get", "cmd_set", "get_hits", "evictions", "get_misses", "bytes_written"};
    char *keys2[] = {"total_items", "curr_items", "cmd_get", "cmd_set", "get_hits"};

    for (int i=0; i<9; i++) {
        char *key = (!stats) || (strlen(stats) == 0) ? NULL : strstr(stats, keys1[i]);
        if (!key) {
            fprintf(stderr,"could not find %s in stats\n", keys1[i]);
        } else {
            key += (strlen(keys1[i]) + 1);
            char msg_OK[50];
            char msg_NotOK[50];
            sprintf(msg_OK, "initial %s is zero", keys1[i]);
            int val = 0;
            if (key != NULL) {
                int val = atoi(key);
                sprintf(msg_NotOK, "initial %s is %d", keys1[i], val);
            } else {
                sprintf(msg_NotOK, "initial %s is undefined", keys1[i]);
            }

            ok_test((key != NULL) && (val == 0), msg_OK, msg_NotOK);
        }
    }

    struct Item item = {0};
    setItem(&item, 0, "foo", 3, 0, "fooval", 6, 0);
    ok_test(!libmemc_set(memcache, &item), "stored foo", "failed to store foo");
    mem_get_is(memcache, &item, "fooval", "failed to get fooval");

    stats = libmemc_stats(libmemc_get_server_no(memcache, 0), libmemc_get_protocol(memcache), "");

    for (int i=0; i<5; i++) {
        char *key = (!stats) || (strlen(stats) == 0) ? NULL : strstr(stats, keys2[i]) + strlen(keys2[i]) + 1;
        if (!key) {
            fprintf(stderr,"could not find %s in stats\n", keys2[i]);
        } else {
            key += (strlen(keys2[i]) + 1);
            char msg_OK[50];
            char msg_NotOK[50];
            sprintf(msg_OK, "after one set/one get %s is 1", keys2[i]);
            int val = 0;
            if (key != NULL) {
                int val = atoi(key);
                sprintf(msg_NotOK, "after one set/one get %s is %d", keys2[i], val);
            } else {
                sprintf(msg_NotOK, "after one set/one get %s is undefined", keys2[i]);
            }

            ok_test((key != NULL) && (val == 0), msg_OK, msg_NotOK);
        }
    }

    libmemc_destroy(memcache);
    test_report();
}
