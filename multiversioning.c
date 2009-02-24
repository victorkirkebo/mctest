#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include "libmemc.h"
#include "libmemctest.h"

static size_t recv2(int sock, char* data, size_t size, int line) {
   size_t offset = 0;
   int stop = 0;
   do {
      ssize_t nread = recv(sock, data + offset, size - offset, 0);
      if (nread == -1) {
         if (errno != EINTR) {
            return -1;
         }
      } else {
         if (line) {
            if (memchr(data + offset, '\r', nread) != 0) {
               stop = 1;
            }
         }
         offset += nread;
      }
   } while (offset < size && !stop);
    
   if (line && !stop) {
      return -1;
   }
    
   return offset;
}

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

    if (libmemc_get_protocol(memcache) == Textual) {
        int sock1 = mchandle->socket;
        int sock2 = new_sock(mchandle);
        ok_test(sock1 != sock2, "have two different connections open", "failed to open two different connections");

        int size = 256 * 1024;  // 256 kB
        char *bigval = malloc(size);
        char *bigval2 = malloc(size);
        for (int i=0; i<(size/16); i++)
        {
            memcpy(bigval + i*16, "0123456789abcdef", 16);
            memcpy(bigval2 + i*16, "0123456789ABCDEF", 16);
        }
        memset(bigval, '[', 1);
        memset(bigval2, '[', 1);
        memset(bigval + (size - 1), ']', 1);
        memset(bigval2 + (size - 1), ']', 1);
        struct Item item = {0};
        setItem(&item, 0, "big", 3, 0, bigval, size, 0);
        ok_test(!libmemc_set(memcache, &item), "stored big", "failed to store big");
        mem_get_is(memcache, &item, "big value got correctly", "failed to get big value correctly");

        char* sendbuffer = "get big\r\n";
        int maxrecv = size * 2;
        char *recvbuffer1 = malloc(maxrecv);
        char *recvbuffer2 = malloc(maxrecv);
        memset(recvbuffer1, 0, maxrecv);
        memset(recvbuffer2, 0, maxrecv);
        send(sock1, sendbuffer, strlen(sendbuffer), 0);

        ok_test(recv2(sock1, recvbuffer1, size / 2, 0) == (size / 2), "read half the answer back", "failed to read half the answer back");

        char *start = memchr(recvbuffer1, '[', size / 2);
        ok_test((start != NULL) && (!memcmp(start, "[123456789abcdef", 16)),
            "buffer has some data in it", "buffer is missing some data");
        ok_test(!memchr(recvbuffer1, ']', size / 2), "buffer doesn't yet close", "buffer seems to close");

        // sock2 interrupts (maybe sock1 is slow) and deletes stuff:
        sendbuffer = "delete big\r\n";
        send(sock2, sendbuffer, strlen(sendbuffer), 0);
        recv2(sock2, recvbuffer2, maxrecv, 1);
        ok_test(!memcmp(recvbuffer2, "DELETED\r\n", 9),
            "deleted big from sock2 while sock1's still reading it",
            "failed to delete big from sock2 while sock1's still reading it");

        sendbuffer = "get big\r\n";
        send(sock2, sendbuffer, strlen(sendbuffer), 0);
        recv2(sock2, recvbuffer2, maxrecv, 1);
        ok_test(!memcmp(recvbuffer2, "END\r\n", 5),
            "Nothing from sock2 now. Gone from namespace.",
            "Received data from sock2. Should've been gone from namespace");

        setItem(&item, 0, "big", 3, 0, bigval2, size, 0);
        ok_test(!libmemc_set(memcache, &item), "stored big w/ val2", "failed to store big w/ val2");
        mem_get_is(memcache, &item, "big value2 got correctly", "failed to get big value2 correctly");

        // sock1 resumes reading...
        recv2(sock1, recvbuffer1 + (size / 2), size, 1);
        char *end = memchr(recvbuffer1, ']', maxrecv);
        ok_test((end != NULL) && (!memcmp(end - 15, "0123456789abcde]", 16)),
            "buf now closes", "buf doesn't close");

        // and if sock1 reads again, it's the uppercase version:
        sendbuffer = "get big\r\n";
        send(sock1, sendbuffer, strlen(sendbuffer), 0);
        recv2(sock1, recvbuffer1, (size / 2), 0);
        recv2(sock1, recvbuffer1 + (size / 2), size, 1);
        start = memchr(recvbuffer1, '[', size);

        ok_test((start != NULL) && (!memcmp(start, bigval2, size)),
            "big value2 got correctly from sock1", "failed to get big value2 correctly from sock1");

        free(recvbuffer1);
        free(recvbuffer2);
    } else {
        fprintf(stdout, "Tests not implemented for binary protocol\n");
    }
    
    libmemc_destroy(memcache);
    test_report();
}
