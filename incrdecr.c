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
    setItem(&item, 0, "num", 3, 0, "1", 1, 0);
    ok_test(!libmemc_set(memcache, &item), "stored num", "failed to store num");
    mem_get_is(memcache, &item, "num == '1'", "num != '1'");

    ok_test(!libmemc_incr(memcache, &item, 1) && 
            !memcmp(item.data, "2", item.size), "+ 1 = 2", "+ 1 != 2");
    mem_get_is(memcache, &item, "num == '2'", "num != '2'");

    ok_test(!libmemc_incr(memcache, &item, 8) && 
            !memcmp(item.data, "10", item.size), "+ 8 = 10", "+ 8 != 10");
    mem_get_is(memcache, &item, "num == '10'", "num != '10'");

    ok_test(!libmemc_decr(memcache, &item, 1) && 
            !memcmp(item.data, "9", item.size), "- 1 = 9", "- 1 != 9");

    ok_test(!libmemc_decr(memcache, &item, 9) && 
            !memcmp(item.data, "0", item.size), "- 9 = 0", "- 9 != 0");

    ok_test(!libmemc_decr(memcache, &item, 5) && 
            !memcmp(item.data, "0", item.size), "- 5 = 0", "- 5 != 0");

    setItem(&item, 0, "num", 3, 0, "4294967296", 10, 0);
    ok_test(!libmemc_set(memcache, &item), "stored 2**32", "failed to store 2**32");
    ok_test(!libmemc_incr(memcache, &item, 1) && 
            !memcmp(item.data, "4294967297", item.size),
            "4294967296 + 1 = 4294967297", "4294967296 + 1 != 4294967297");

    setItem(&item, 0, "num", 3, 0, "18446744073709551615", 20, 0);
    ok_test(!libmemc_set(memcache, &item), "stored 2**64-1", "failed to store 2**64-1");
    ok_test(!libmemc_incr(memcache, &item, 1) && 
            !memcmp(item.data, "0", item.size),
            "(2**64 - 1) + 1 = 0", "(2**64 - 1) + 1 != 0");

    if (libmemc_get_protocol(memcache) == Textual)
    {
        setItem(&item, 0, "bogus1", 6, 0, "0", 1, 0);
        ok_test((libmemc_decr(memcache, &item, 5) == -1), "can't decr bogus key", "can decr bogus key");
        setItem(&item, 0, "bogus2", 6, 0, "0", 1, 0);
        ok_test((libmemc_incr(memcache, &item, 5) == -1), "can't incr bogus key", "can incr bogus key");
    } else {
        setItem(&item, 0, "bogus1", 6, 0, "4", 1, 0xffffffff);
        ok_test((libmemc_decr(memcache, &item, 5) == -1), 
                              "can't decr bogus key when expiry == 0xffffffff",
                              "can decr bogus key when expiry == 0xffffffff");

        setItem(&item, 0, "bogus2", 6, 0, "4", 1, 0xffffffff);
        ok_test((libmemc_incr(memcache, &item, 5) == -1),
                              "can't incr bogus key when expiry == 0xffffffff",
                              "can incr bogus key when expiry == 0xffffffff");

        setItem(&item, 0, "bogus1", 6, 0, "4", 1, 0);
        ok_test(!libmemc_decr(memcache, &item, 5), 
                              "can decr bogus key when expiry != 0xffffffff",
                              "can't decr bogus key when expiry != 0xffffffff");
        setItem(&item, 0, "bogus2", 6, 0, "4", 1, 0);
        ok_test(!libmemc_incr(memcache, &item, 5),
                              "can incr bogus key when expiry != 0xffffffff",
                              "can't incr bogus key when expiry != 0xffffffff");
    }

    setItem(&item, 0, "text", 4, 0, "hi", 2, 0);
    ok_test(!libmemc_set(memcache, &item), "stored text", "failed to store text");
    ok_test(!libmemc_incr(memcache, &item, 1) && 
            !memcmp(item.data, "1", item.size),
            "hi + 1 = 1", "hi + 1 != 1");

    setItem(&item, 0, "text", 4, 0, "hi", 2, 0);
    ok_test(!libmemc_set(memcache, &item), "stored text", "failed to store text");
    ok_test(!libmemc_decr(memcache, &item, 1) && 
            !memcmp(item.data, "0", item.size),
            "hi - 1 = 0", "hi - 1 != 0");

    libmemc_destroy(memcache);
    test_report();
}
