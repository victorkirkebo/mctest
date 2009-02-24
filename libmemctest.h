#ifndef LIBMEMCTEST_H
#define	LIBMEMCTEST_H

#ifdef __cplusplus
extern "C"  {
#endif
    
#include "libmemc.h"

static int test_counter = 0;
static int test_success_counter = 0;
static int verbose = 0;
static int binary_protocol = 0;
static int textual_protocol = 0;
static char *memcached_path = "..";

struct memcached_process_handle {
   int pid;
   int port;
   int udpport;
   int socket;
   struct memcached_process_handle *next;
};

struct memcached_process_handle* process_handle;

void test_init(int argc, char **argv);

int ok_test(int ok_result, const char* ok_str, const char* not_ok_str);

int test_report();

int mem_get_is(struct Memcache* mc, const struct Item *item,
            const char* msg_ok, const char* msg_not_ok);

int mem_gets_is(struct Memcache* mc, const struct Item *item,
            const char* msg_ok, const char* msg_not_ok);

struct memcached_process_handle *new_memcached(int port, char* args);

int new_sock(struct memcached_process_handle *handle);

int new_udp_sock(struct memcached_process_handle *handle);

void exit_cleanup(void);

void setItem(struct Item *item,
            uint64_t cas_id,
            const char *key,
            int keylen,
            uint32_t flags,
            void *data,
            size_t size,
            size_t exptime);

#ifdef __cplusplus
}
#endif

#endif	/* LIBMEMCTEST_H */
