#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "libmemc.h"
#include "libmemctest.h"

int main(int argc, char **argv)
{
    test_init(argc, argv);

    // start the server
    char* tmpfilename = tmpnam(NULL);
    char args[50];
    sprintf(args, "-d -P %s", tmpfilename);
    struct memcached_process_handle* mchandle = new_memcached(0, args);
    if (!mchandle) {
        fprintf(stderr,"Could not start memcached process\n\n");
        exit(0);
    }

    struct Memcache* memcache = libmemc_create(Automatic);
    if (libmemc_add_server(memcache, "127.0.0.1", mchandle->port) == -1) {
        fprintf(stderr,"Could not add server\n\n");
        exit(0);
    }

    // Test 1
    FILE *file = fopen(tmpfilename, "r");
    ok_test(file != NULL, "pid file exists", "pid file does not exist");

    // Test 2
    char buf[10];
    ok_test(fgets(buf, 10, file) != NULL, "pid file has length", 
            "pid file has no length");
    fclose(file);

    // Test 3
    int readpid = atoi(buf);
    ok_test(kill(readpid, 0) != -1, "process is still running", 
            "process was killed");

    // Test 4
    char* statsbuffer = libmemc_stats(libmemc_get_server_no(memcache, 0), 
            libmemc_get_protocol(memcache), "");
    char *ptr = (!statsbuffer) || (strlen(statsbuffer) == 0) ? NULL : strstr(statsbuffer, "pid");
    if (!ptr) {
        fprintf(stderr,"Could not find pid in stats\n");
    } else {
        int pid = atoi(ptr + (strlen("pid")+1));
        ok_test(pid == readpid, "memcached reports same pid as file", 
                "memcached does not report same pid as file");        
    }

    // Test 5
    ok_test(new_sock(mchandle) != -1, "opened new socket",
            "failed to open a new socket");

    // Test 6
    ok_test(kill(readpid, 9) != -1, "sent KILL signal",
            "sent KILL signal failed");

    // Test 7
    sleep(1);
    ok_test(new_sock(mchandle) == -1, "failed to open new socket",
            "did not fail to open new socket");

    libmemc_destroy(memcache);
    remove(tmpfilename);
    test_report();
}
