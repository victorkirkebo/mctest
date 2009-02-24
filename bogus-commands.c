#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    char* sendbuffer = "boguscommand slkdsldkfjsd\r\n";
    send(mchandle->socket, sendbuffer, strlen(sendbuffer), 0);
    char recvbuffer[50];
    recv(mchandle->socket, recvbuffer, 50, 0);
    ok_test(!strncmp(recvbuffer, "ERROR\r\n", 7),
            "got error back", "did not get error back");

    test_report();
}
