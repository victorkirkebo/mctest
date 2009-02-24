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

    char *stats = libmemc_stats(libmemc_get_server_no(memcache, 0),
        libmemc_get_protocol(memcache), "detail dump");
    char *expected;
    if (libmemc_get_protocol(memcache) == Textual) // textual protocol
        expected = "END\r\n";
    else
        expected = "detailed END\r\n"; // binary protocol
    ok_test((stats != NULL) && (!strncmp(stats, expected, strlen(expected))),
        "verified empty stats at start", "failed to verify empty stats at start");

    ok_test(libmemc_stats(libmemc_get_server_no(memcache, 0), libmemc_get_protocol(memcache), "detail on") != NULL,
         "detail collection turned on", "failed to turn detail collection on");

    struct Item item = {0};
    setItem(&item, 0, "foo:123", 7, 0, "fooval", 6, 0);
    ok_test(!libmemc_set(memcache, &item), "stored foo", "failed to store foo");

    stats = libmemc_stats(libmemc_get_server_no(memcache, 0),
        libmemc_get_protocol(memcache), "detail dump");
    expected = "PREFIX foo get 0 hit 0 set 1 del 0\r\nEND\r\n";
    ok_test((stats != NULL) && (strstr(stats, expected) != NULL),
        "details after set", "failed to get details after set");

    mem_get_is(memcache, &item, "fooval", "failed to get fooval");
    stats = libmemc_stats(libmemc_get_server_no(memcache, 0),
        libmemc_get_protocol(memcache), "detail dump");
    expected = "PREFIX foo get 1 hit 1 set 1 del 0\r\nEND\r\n";
    ok_test((stats != NULL) && (strstr(stats, expected) != NULL),
        "details after get with hit", "failed to get details after get with hit");

    setItem(&item, 0, "foo:124", 7, 0, NULL, 0, 0);
    mem_get_is(memcache, &item, "<undef>", "not <undef>");
    stats = libmemc_stats(libmemc_get_server_no(memcache, 0),
        libmemc_get_protocol(memcache), "detail dump");
    expected = "PREFIX foo get 2 hit 1 set 1 del 0\r\nEND\r\n";
    ok_test((stats != NULL) && (strstr(stats, expected) != NULL),
        "details after get without hit", "failed to get details after get without hit");

    setItem(&item, 0, "foo:125", 7, 0, NULL, 0, 0);
    ok_test(!libmemc_delete(memcache, &item), "sent delete command", "delete command failed");

    stats = libmemc_stats(libmemc_get_server_no(memcache, 0),
        libmemc_get_protocol(memcache), "detail dump");
    expected = "PREFIX foo get 2 hit 1 set 1 del 1\r\nEND\r\n";
    ok_test((stats != NULL) && (strstr(stats, expected) != NULL),
        "details after delete", "failed to get details after delete");

    stats = libmemc_stats(libmemc_get_server_no(memcache, 0),
        libmemc_get_protocol(memcache), "reset");

    if (libmemc_get_protocol(memcache) == Textual)
        expected = "RESET\r\n";
    else
        expected = "";
    ok_test((stats != NULL) && (!strcmp(stats, expected)), "stats cleared", "failed to clear stats");

    stats = libmemc_stats(libmemc_get_server_no(memcache, 0),
        libmemc_get_protocol(memcache), "detail dump");
    expected = "END\r\n";
    ok_test((stats != NULL) && (strstr(stats, expected) != NULL),
        "empty stats after clear", "failed to verify empty stats after clear");

    setItem(&item, 0, "foo:123", 7, 0, "fooval", 6, 0);
    mem_get_is(memcache, &item, "fooval", "failed to get fooval");
    stats = libmemc_stats(libmemc_get_server_no(memcache, 0),
        libmemc_get_protocol(memcache), "detail dump");
    expected = "PREFIX foo get 1 hit 1 set 0 del 0\r\nEND\r\n";
    ok_test((stats != NULL) && (strstr(stats, expected) != NULL),
        "details after clear and get", "failed to get details after clear and get");

    ok_test(libmemc_stats(libmemc_get_server_no(memcache, 0), libmemc_get_protocol(memcache), "detail off") != NULL,
         "detail collection turned off", "failed to turn detail collection off");

    setItem(&item, 0, "foo:124", 7, 0, NULL, 0, 0);
    mem_get_is(memcache, &item, "<undef>", "not <undef>");

    setItem(&item, 0, "foo:123", 7, 0, "fooval", 6, 0);
    mem_get_is(memcache, &item, "fooval", "failed to get fooval");
    stats = libmemc_stats(libmemc_get_server_no(memcache, 0),
        libmemc_get_protocol(memcache), "detail dump");
    expected = "PREFIX foo get 1 hit 1 set 0 del 0\r\nEND\r\n";
    ok_test((stats != NULL) && (strstr(stats, expected) != NULL),
        "details after stats turned off", "failed to get details after stats turned off");
    
    libmemc_destroy(memcache);
    test_report();
}
