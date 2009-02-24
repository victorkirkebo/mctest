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

    // set foo (and should get it)
    struct Item item = {0};
    setItem(&item, 0, "foo", 3, 0, "fooval", 6, 0);
    ok_test(!libmemc_set(memcache, &item), "stored foo", "failed to store foo");
    mem_get_is(memcache, &item, "foo == 'fooval'", "foo != 'fooval'");

    // add bar (and should get it)
    setItem(&item, 0, "bar", 3, 0, "barval", 6, 0);
    ok_test(!libmemc_add(memcache, &item), "stored bar", "failed to store bar");
    mem_get_is(memcache, &item, "bar == 'barval'", "bar != 'barval'");

    // add foo (but shouldn't get new value)
    setItem(&item, 0, "bar", 3, 0, "foov2", 5, 0);
    ok_test(libmemc_add(memcache, &item) != 0, "not stored", "stored foo");
    setItem(&item, 0, "foo", 3, 0, "fooval", 6, 0);
    mem_get_is(memcache, &item, "foo == 'fooval'", "foo != 'fooval'");

    // replace bar (should work)
    setItem(&item, 0, "bar", 3, 0, "barva2", 6, 0);
    ok_test(!libmemc_replace(memcache, &item), "replaced barval 2", "failed to replace barval 2");

    // replace notexist (shouldn't work)
    setItem(&item, 0, "notexist", 8, 0, "barva2", 6, 0);
    ok_test(libmemc_replace(memcache, &item) != 0, "didn't replace notexist", "replaced notexist");

    // delete foo
    setItem(&item, 0, "foo", 3, 0, NULL, 0, 0);
    ok_test(!libmemc_delete(memcache, &item), "deleted foo", "failed to delete foo");

    // delete foo again.  not found this time.
    libmemc_delete(memcache, &item);
    ok_test((strstr(item.errmsg, "NOT_FOUND") == item.errmsg) ||
            (strstr(item.errmsg, "Not found") == item.errmsg),
        "deleted foo, but not found", "deleted foo, and found");

    // add moo
    setItem(&item, 0, "moo", 3, 0, "mooval", 6, 0);
    ok_test(!libmemc_add(memcache, &item), "added mooval", "failed to add mooval");
    mem_get_is(memcache, &item, "moo == 'mooval'", "moo != 'mooval'");

    // check-and-set (cas) failure case, try to set value with incorrect cas unique val
    setItem(&item, 100, "moo", 3, 0, "MOOVAL", 6, 0);
    ok_test(libmemc_cas(memcache, &item) != 0, "check and set with invalid id", "check and set with invalid id succeeded");

    // test "gets", grab unique ID
    setItem(&item, 0, "moo", 3, 0, NULL, 0, 0);
    libmemc_gets(libmemc_get_server_no(memcache, 0),
        libmemc_get_protocol(memcache), &item, 1);
    ok_test(item.cas_id == 4, "unique cas ID == 4", "unique cas ID != 4");

    // now test that we can store moo with the correct unique id
    setItem(&item, item.cas_id, "moo", 3, 0, "MOOVAL", 6, 0);
    ok_test(!libmemc_cas(memcache, &item), "cas success, set moo", "cas failed, moo not set");
    mem_get_is(memcache, &item, "moo == 'MOOVAL'", "moo != 'MOOVAL'");

    // Test sets up to a large size around 1MB.
    // Everything up to 1MB - 1k should succeed, everything 1MB +1k should fail.
    int len = 1024;
    while (len < 1024*1028) {
       char *val = malloc(len);
       char key[20];
       sprintf(key, "foo_%d", len);
       char okmsg[50];
       char notokmsg[50];
       memset(val, 'B', len);
       if (len > 1024*1024) {
          setItem(&item, 0, key, strlen(key), 0, "MOO", 3, 0);
          sprintf(okmsg, "stored %s with size 3", key);
          sprintf(notokmsg, "failed to store %s with size 3", key);
          ok_test(!libmemc_set(memcache, &item), okmsg, notokmsg);
          setItem(&item, 0, key, strlen(key), 0, val, len, 0);
          sprintf(okmsg, "failed to store %s with size %d", key, len);
          sprintf(notokmsg, "stored %s with size %d", key, len);
          ok_test(libmemc_set(memcache, &item) != 0, okmsg, notokmsg);
          setItem(&item, 0, key, strlen(key), 0, NULL, 0, 0);
          sprintf(okmsg, "%s == <undef>", key);
          sprintf(notokmsg, "%s != <undef>", key);
          mem_get_is(memcache, &item, okmsg, notokmsg);
       } else {
          setItem(&item, 0, key, strlen(key), 0, val, len, 0);
          sprintf(okmsg, "stored %s with size %d", key, len);
          sprintf(notokmsg, "failed to store %s with size %d", key, len);
          ok_test(!libmemc_set(memcache, &item), okmsg, notokmsg);
       }

       free(val);
       len += 2048;
    }
    
    libmemc_destroy(memcache);
    test_report();
}
