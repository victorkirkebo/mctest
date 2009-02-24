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
    struct memcached_process_handle* mchandle = new_memcached(0, "-c 10");
    if (!mchandle) {
        fprintf(stderr,"Could not start memcached process\n\n");
        exit(0);
    }

    ok_test(new_sock(mchandle) != -1, "Connection 0", "Failed to make Connection 0");

    for (int i=1; i<11; i++) {
        char OKMsg[40];
        char NotOKMsg[40];
        sprintf(OKMsg, "Connection %d", i);
        sprintf(NotOKMsg, "Failed to make Connection %d", i);
        ok_test(new_sock(mchandle) != -1, OKMsg, NotOKMsg);
    }
    
    test_report();
}
