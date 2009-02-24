#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include "libmemc.h"
#include "libmemctest.h"

typedef struct {
   uint16_t request_id; // opaque
   uint16_t seq_num;
   uint16_t packets;
   uint16_t reserved;
   char body[4096];
} udp_data;

void test_single(int udp_sock) {
        udp_data senddata;
        senddata.request_id = htons(45);
        senddata.seq_num = htons(0);
        senddata.packets = htons(1);
        senddata.reserved = htons(0);
        strcpy(senddata.body, "get foo\r\n");
        int len = 8 + strlen(senddata.body);
        ok_test(send(udp_sock, &senddata, len, 0) == len, "sent request", "failed to send request");

        fd_set rfds;
        struct timeval tv;

        /* Watch stdin (fd 0) to see when it has input. */
        FD_ZERO(&rfds);
        FD_SET(udp_sock, &rfds);
        /* Wait up to two seconds. */
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        ok_test(select(udp_sock + 1, &rfds, NULL, NULL, &tv) > 0, "got readability", "did not get readability");

        udp_data recvdata;
        int recvlen = recv(udp_sock, &recvdata, sizeof(recvdata), 0);
        recvdata.request_id = ntohs(recvdata.request_id);
        recvdata.seq_num = ntohs(recvdata.seq_num);
        recvdata.packets = ntohs(recvdata.packets);
        recvdata.reserved = ntohs(recvdata.reserved);

        ok_test((recvdata.request_id == 45) &&
                (recvdata.seq_num == 0) &&
                (recvdata.packets == 1) &&
                (recvdata.reserved == 0), "header is correct" ,"header is not correct");

        ok_test(recvlen == 36, "received length is 36", "received length is not 36");
        ok_test(!memcmp(recvdata.body, "VALUE foo 0 6\r\nfooval\r\nEND\r\n", 28),
                "payload is as expected", "payload is not as expected");
}
        
udp_data* send_udp_request(int udp_sock, uint16_t request_id, char* request) {
    udp_data senddata;
    senddata.request_id = htons(request_id);
    senddata.seq_num = htons(0);
    senddata.packets = htons(1);
    senddata.reserved = htons(0);
    strcpy(senddata.body, request);
    int len = 8 + strlen(senddata.body);
    if (send(udp_sock, &senddata, len, 0) != len) {
        fprintf(stdout, "udp send failed\n");
        return NULL;
    }

    int got = 0;   // # packets got
    int numpkts = 0;
    
    udp_data *retdata = NULL;
    while ((numpkts == 0) || (got < numpkts)) {
        fd_set rfds;
        struct timeval tv;
        /* Watch stdin (fd 0) to see when it has input. */
        FD_ZERO(&rfds);
        FD_SET(udp_sock, &rfds);
        /* Wait up to 1.5 seconds. */
        tv.tv_sec = 1;
        tv.tv_usec = 500000;

        if (select(udp_sock + 1, &rfds, NULL, NULL, &tv) <= 0) {
            fprintf(stdout, "timeout after %d packets\n", got);
            return NULL;
        }
        
        udp_data recvdata;
        int recvlen = recv(udp_sock, &recvdata, sizeof(recvdata), 0);
        recvdata.request_id = ntohs(recvdata.request_id);
        recvdata.seq_num = ntohs(recvdata.seq_num);
        recvdata.packets = ntohs(recvdata.packets);
        recvdata.reserved = ntohs(recvdata.reserved);

        if (request_id != recvdata.request_id) {
            fprintf(stdout, "Response ID of %d doesn't match request ID of %d\n",
                    recvdata.request_id, request_id);
            return NULL;
        }
        if (recvdata.reserved != 0) {
            fprintf(stdout, "Reserved area not zero\n");
            return NULL;
        }
        if ((numpkts != 0) && (numpkts != recvdata.packets)) {
            fprintf(stdout, "num packets changed midstream!\n");
            return NULL;
        }
        
        numpkts = recvdata.packets;
        if (retdata == NULL)
            retdata = (udp_data*) malloc(numpkts * sizeof(udp_data));
        memcpy(&retdata[got], &recvdata, sizeof(udp_data));
        got++;
    }
    return retdata;
}

int main(int argc, char **argv)
{
    test_init(argc, argv);
    
    struct memcached_process_handle* mchandle = new_memcached(0, "");
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

    int usock = new_udp_sock(mchandle);
    char ok_str[50];
    char notok_str[50];

    if (libmemc_get_protocol(memcache) == Textual) {
        // set foo
        struct Item item = {0};
        setItem(&item, 0, "foo", 3, 0, "fooval", 6, 0);
        ok_test(!libmemc_set(memcache, &item), "stored foo", "failed to store foo");
        mem_get_is(memcache, &item, "foo == fooval", "foo != fooval");

        test_single(usock);

        int offset[3] = {1, 1, 2};
        for (int i=0; i<3; i++) {        
            udp_data *res = send_udp_request(usock, 160 + offset[i], "get foo\r\n");

            ok_test(res != NULL, "got result", "did not get result");
            ok_test(res[0].packets == 1, "one packet", "not one packet");
            ok_test(res[0].seq_num == 0, "only got seq number 0", "did not only get seq number 0");
            ok_test(!memcmp(res[0].body, "VALUE foo 0 6\r\nfooval\r\nEND\r\n", 28),
                    "payload is as expected", "payload is not as expected");
            sprintf(ok_str, "request id in response %d is correct", res[0].request_id);
            sprintf(notok_str, "request id in response %d is not correct", res[0].request_id);
            ok_test(res[0].request_id == 160 + offset[i], ok_str, notok_str);
                
            if (res)
                free(res);
        }
        
        // testing non-existent stuff
        udp_data *res = send_udp_request(usock, 404, "get notexist\r\n");
        ok_test(res != NULL, "got result", "did not get result");
        ok_test(res[0].packets == 1, "one packet", "not one packet");
        ok_test(res[0].seq_num == 0, "only got seq number 0", "did not only get seq number 0");
        sprintf(ok_str, "request id %d correct", res[0].request_id);
        sprintf(notok_str, "request id %d not correct", res[0].request_id);
        ok_test(res[0].request_id == 404, ok_str, notok_str);
        ok_test(!memcmp(res[0].body, "END\r\n", 5),
                "payload is as expected", "payload is not as expected");
        if (res)
            free(res);
        
        int size = 4096;  // 256 kB
        char *big = malloc(size);
        for (int i=0; i<(size/4); i++)
        {
            memcpy(big + i*4, "abcd", 4);
        }
        setItem(&item, 0, "big", 3, 0, big, size, 0);
        ok_test(!libmemc_set(memcache, &item), "stored big", "failed to store big");
        mem_get_is(memcache, &item, "big value matches", "big value does not match");
        res = send_udp_request(usock, 999, "get big\r\n");
        ok_test(res[0].packets == 3, "three packet response", "not three packet response");
        ok_test(!memcmp(res[0].body, "VALUE big 0 4096\r\n", 18),
                "first packet has value line", "first packet is missing value line");
        ok_test(strstr(res[2].body, "END\r\n") != NULL,
                "last packet has end", "last packet is missing end");
        ok_test(res[1].request_id == 999, "request id of middle packet is correct", 
                "request id of middle packet is incorrect");

        if (res)
            free(res);        
    } else {
        fprintf(stdout, "Tests not implemented for binary protocol\n");
    }
    
    libmemc_destroy(memcache);
    test_report();
}
