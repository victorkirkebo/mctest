#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>
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

    int sock = libmemc_get_socket(libmemc_get_server_no(memcache, 0));
    int sock2 = new_sock(mchandle);
    char recvbuffer[256];

    // do we have two open connections?
    ok_test(sock != sock2, "have two different connections open",
            "failed to open two different connections");

    // gets foo (should not exist)
    struct Item item = {0};
    setItem(&item, 0, "foo", 3, 0, NULL, 0, 0);
    ok_test(libmemc_get(memcache, &item) == -1, "foo == <undef>", "foo != <undef>");

    // set foo
    setItem(&item, 0, "foo", 3, 0, "barval", 6, 0);
    ok_test(!libmemc_set(memcache, &item), "stored foo", "failed to store foo");

    // gets foo and verify identifier exists
    setItem(&item, 0, "foo", 3, 0, NULL, 0, 0);
    libmemc_get(memcache, &item);
    mem_gets_is(memcache, &item, "foo == barval", "foo != barval");

    // cas fail
    setItem(&item, 123, "foo", 3, 0, "barva2", 6, 0);
    ok_test(libmemc_cas(memcache, &item) == -1, "cas failed for foo", "cas did not fail for foo");

    // gets foo - success
    setItem(&item, 0, "foo", 3, 0, NULL, 0, 0);
    libmemc_get(memcache, &item);
    mem_gets_is(memcache, &item, "foo == barval", "foo != barval");

    // cas success
    ok_test(!libmemc_cas(memcache, &item), "cas success, set foo", "cas failed, foo not set");

    // cas failure (reusing the same key)
    ok_test(libmemc_cas(memcache, &item) == -1, "cas fails when reusing a CAS ID",
        "cas did not fail when reusing a CAS ID");

    // delete foo
    int casid = item.cas_id;
    setItem(&item, 0, "foo", 3, 0, NULL, 0, 0);
    ok_test(!libmemc_delete(memcache, &item), "deleted foo", "failed to delete foo");

    // cas missing
    setItem(&item, casid, "foo", 3, 0, NULL, 0, 0);
    ok_test(libmemc_cas(memcache, &item) == -1, "cas failed, foo does not exist",
        "cas did not fail even if foo does not exist");

    // set foo1
    setItem(&item, 0, "foo1", 4, 0, "1", 1, 0);
    ok_test(!libmemc_set(memcache, &item), "set foo1", "failed to set foo1");
    // set foo2
    setItem(&item, 0, "foo2", 4, 0, "2", 1, 0);
    ok_test(!libmemc_set(memcache, &item), "set foo2", "failed to set foo2");

    // gets foo1 check
    setItem(&item, 0, "foo1", 4, 0, NULL, 0, 0);
    ok_test(!libmemc_get(memcache, &item), "get foo1 success", "failed to get foo1");
    uint64_t cas1 = item.cas_id;
    ok_test((item.size == 1) && !memcmp(item.data, "1", 1), "foo1 data is 1", "foo1 data is not 1");

    // gets foo2 check
    setItem(&item, 0, "foo2", 4, 0, NULL, 0, 0);
    ok_test(!libmemc_get(memcache, &item), "get foo2 success", "failed to get foo2");
    uint64_t cas2 = item.cas_id;
    ok_test((item.size == 1) && !memcmp(item.data, "2", 1), "foo2 data is 2", "foo2 data is not 2");

    // validate foo1 != foo2
    ok_test(cas1 != cas2, "foo1 != foo2 single-gets success", "foo1 == foo2 single-gets failure");

    // multi-gets
    struct Item item_arr[2];
    for (int i=0; i<2; i++)
       memset(&item_arr[i], 0, sizeof(struct Item));
    setItem(&item_arr[0], 0, "foo1", 4, 0, NULL, 0, 0);
    setItem(&item_arr[1], 0, "foo2", 4, 0, NULL, 0, 0);
    libmemc_gets(libmemc_get_server_no(memcache, 0),
        libmemc_get_protocol(memcache), item_arr, 2);
    cas1 = item_arr[0].cas_id;
    cas2 = item_arr[1].cas_id;
    ok_test((item_arr[0].size == 1) && !memcmp(item_arr[0].data, "1", 1),
        "foo1 data is 1", "foo1 data is not 1");
    ok_test((item_arr[1].size == 1) && !memcmp(item_arr[1].data, "2", 1),
        "foo2 data is 2", "foo2 data is not 2");

    // validate foo1 != foo2
    ok_test(cas1 != cas2, "foo1 != foo2 multi-gets success", "foo1 == foo2 multi-gets failure");

    // simulate race condition with cas
    // gets foo1 - success
    setItem(&item_arr[0], 0, "foo1", 4, 0, NULL, 0, 0);
    libmemc_gets(libmemc_get_server_no(memcache, 0),
        libmemc_get_protocol(memcache), &item_arr[0], 1);
    ok_test(item_arr[0].cas_id > 0, "sock - gets foo1 is not empty", "sock - gets foo1 is empty");

    // gets foo2 - success
    libmemc_set_socket(libmemc_get_server_no(memcache, 0), sock2);
    setItem(&item_arr[1], 0, "foo1", 4, 0, NULL, 0, 0);
    libmemc_gets(libmemc_get_server_no(memcache, 0),
        libmemc_get_protocol(memcache), &item_arr[1], 1);
    ok_test(item_arr[1].cas_id > 0, "sock2 - gets foo1 is not empty", "sock2 - gets foo1 is empty");

    libmemc_set_socket(libmemc_get_server_no(memcache, 0), sock);
    setItem(&item_arr[0], item_arr[0].cas_id, "foo1", 4, 0, "barva2", 6, 0);
    libmemc_cas(memcache, &item_arr[0]);
    libmemc_set_socket(libmemc_get_server_no(memcache, 0), sock2);
    setItem(&item_arr[1], item_arr[1].cas_id, "foo1", 4, 0, "apple", 5, 0);
    libmemc_cas(memcache, &item_arr[1]);

    ok_test(((strstr(item_arr[0].errmsg, "STORED") == item_arr[0].errmsg) &&
             (strstr(item_arr[1].errmsg, "EXISTS") == item_arr[1].errmsg))
            ||
            ((strstr(item_arr[0].errmsg, "EXISTS") == item_arr[0].errmsg) &&
             (strstr(item_arr[1].errmsg, "STORED") == item_arr[1].errmsg))
            || //binary protocol
            ((strstr(item_arr[0].errmsg, "Stored") == item_arr[0].errmsg) &&
             (strstr(item_arr[1].errmsg, "Data exists for key") == item_arr[1].errmsg))
            || //binary protocol
            ((strstr(item_arr[0].errmsg, "Data exists for key") == item_arr[0].errmsg) &&
             (strstr(item_arr[1].errmsg, "Stored") == item_arr[1].errmsg)),
            "cas on same item from two sockets", "cas on same item from two sockets failed");

    libmemc_destroy(memcache);
    test_report();
}
