/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * See LICENSE.txt included in this distribution for the specific
 * language governing permissions and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at LICENSE.txt.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#ifndef LIBMEMC_LIBMEMC_H
#define	LIBMEMC_LIBMEMC_H

#include <sys/types.h>
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C"  {
#endif

struct Item {
   uint64_t cas_id;
   const char *key;
   int keylen;
   uint32_t flags;
   void *data;
   size_t size;
   size_t exptime;
   const char *errmsg;
};

enum Protocol { Automatic = 0, Binary = 1, Textual = 2 };

struct Memcache* libmemc_create(enum Protocol protocol);
void libmemc_destroy(struct Memcache* handle);
int libmemc_add_server(struct Memcache *handle, const char *host, in_port_t port);
struct Server* libmemc_get_server_no(struct Memcache *handle, int server_no);
int libmemc_get_socket(struct Server *server);
int libmemc_set_socket(struct Server *server, int socket);
enum Protocol libmemc_get_protocol(struct Memcache *handle);
int libmemc_add(struct Memcache *handle, struct Item *item);
int libmemc_set(struct Memcache *handle, struct Item *item);
int libmemc_replace(struct Memcache *handle, struct Item *item);
int libmemc_cas(struct Memcache *handle, struct Item *item);
int libmemc_get(struct Memcache *handle, struct Item *item);
int libmemc_gets(struct Server *server, enum Protocol protocol, struct Item item[], int items);
int libmemc_incr(struct Memcache *handle, struct Item *item, uint64_t delta);
int libmemc_decr(struct Memcache *handle, struct Item *item, uint64_t delta);
int libmemc_delete(struct Memcache *handle, struct Item *item);
int libmemc_flush_all(struct Memcache *handle, long exptime);
char* libmemc_stats(struct Server *server, enum Protocol protocol, const char* stats_type);
int libmemc_connect_server(const char *hostname, in_port_t port);

#ifdef __cplusplus
}
#endif

#endif	/* LIBMEMC_LIBMEMC_H */
