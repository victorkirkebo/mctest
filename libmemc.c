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

#include "../config.h"
#include "libmemc.h"

#if HAVE_PROTOCOL_BINARY
#include "../protocol_binary.h"
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

struct Server {
   int sock;
   struct addrinfo *addrinfo;
   const char *errmsg;
   const char *peername;
   char *buffer;
   int buffersize;
};

enum StoreCommand {add, set, replace, cas};

enum IncrDecrCommand {incr, decr};

struct Memcache {
   struct Server** servers;
   enum Protocol protocol;
   int no_servers;
};

static struct Server* server_create(const char *name, in_port_t port);
static void server_destroy(struct Server *server);

static int textual_store(struct Server* server, enum StoreCommand cmd, 
                        struct Item *item);
static int textual_get(struct Server* server, struct Item* item);
static int binary_store(struct Server* server, enum StoreCommand cmd, 
                        struct Item *item);
static int binary_get(struct Server* server, struct Item* item);
static int libmemc_store(struct Memcache* handle, enum StoreCommand cmd, struct Item *item);

static int textual_gets(struct Server* server, struct Item item[], int items);
static int binary_gets(struct Server* server, struct Item item[], int items);

static struct Server *get_server(struct Memcache *handle, const char *key);
static int server_connect(struct Server *server);

static int textual_incr_decr(struct Server* server, enum IncrDecrCommand cmd, struct Item *item, uint64_t delta);
static int binary_incr_decr(struct Server* server, enum IncrDecrCommand cmd, struct Item *item, uint64_t delta);
static int libmemc_incr_decr(struct Memcache* handle, enum IncrDecrCommand cmd, struct Item *item, uint64_t delta);

static int textual_delete(struct Server* server, struct Item* item);
static int binary_delete(struct Server* server, struct Item* item);

static int textual_flush_all(struct Server *server, long exptime);
static int binary_flush_all(struct Server *server, long exptime);

static char* textual_stats(struct Server *server, const char* stats_type);
static char* binary_stats(struct Server *server, const char* stats_type);

/**
 * External interface
 */
struct Memcache* libmemc_create(enum Protocol protocol) {
   struct Memcache* ret = calloc(1, sizeof(struct Memcache));

   if (ret != NULL) {
      if (protocol == Automatic) {
         char *protocol = getenv("PROTOCOL");
         if ((protocol != NULL) && (!strcmp(protocol, "Textual")))
            ret->protocol = Textual;
         else
            ret->protocol = Binary;
      } else {
         ret->protocol = protocol;
      }
   }
   return ret;
}

void libmemc_destroy(struct Memcache* handle) {
   for (int ii = 0; ii < handle->no_servers; ++ii) {
      server_destroy(handle->servers[ii]);
   }
   free(handle);
}

int libmemc_add_server(struct Memcache *handle, const char *host, in_port_t port) {
   struct Server** servers = calloc(handle->no_servers + 1, sizeof(struct Server));
   struct Server** old = handle->servers;
    
   if (servers == 0) {
      return -1;
   }
    
   for (int ii = 0; ii < handle->no_servers; ++ii) {
      servers[ii] = handle->servers[ii];
   }
    
   handle->servers = servers;
   free(old);
    
   struct Server *server = server_create(host, port);
   if (server != NULL) {
      handle->servers[handle->no_servers++] = server;
   }
    
   return 0;
}

struct Server *libmemc_get_server_no(struct Memcache *handle, int server_no)
{
   if (handle->no_servers > server_no)
      return handle->servers[server_no];
   else
      return NULL;
}

int libmemc_get_socket(struct Server *server)
{
   return server->sock;
}

int libmemc_set_socket(struct Server *server, int socket)
{
   server->sock = socket;
}


int libmemc_add(struct Memcache *handle, struct Item *item) {
   return libmemc_store(handle, add, item);
}

int libmemc_set(struct Memcache *handle, struct Item *item) {
   return libmemc_store(handle, set, item);    
}

int libmemc_replace(struct Memcache *handle, struct Item *item) {
   return libmemc_store(handle, replace, item);        
}

int libmemc_cas(struct Memcache *handle, struct Item *item) {
   return libmemc_store(handle, cas, item);        
}

int libmemc_get(struct Memcache *handle, struct Item *item) {
   struct Server* server = get_server(handle, item->key);
   if (server == NULL) {
      return -1;
   } else {
      if (server->sock == -1) {
         if (server_connect(server) == -1) {
            fprintf(stderr, "%s\n", server->errmsg);
            fflush(stderr);
            return -1;
         }
      }
      if (handle->protocol == Binary) {
         return binary_get(server, item);
      } else {
         return textual_get(server, item);
      }
   }
}

int libmemc_gets(struct Server *server, enum Protocol protocol, struct Item item[], int items) {
   if (server == NULL) {
      return -1;
   } else {
      if (server->sock == -1) {
         if (server_connect(server) == -1) {
            fprintf(stderr, "%s\n", server->errmsg);
            fflush(stderr);
            return -1;
         }
      }

      if (protocol == Binary) {
         return binary_gets(server, item, items);
      } else {
         return textual_gets(server, item, items);
      }
   }
}

static struct addrinfo *lookuphost(const char *hostname, in_port_t port)
{
    struct addrinfo *ai = 0;
    struct addrinfo hints = {0};
    char service[NI_MAXSERV];
    int error;

    hints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_socktype = SOCK_STREAM;
    
    (void)snprintf(service, NI_MAXSERV, "%d", port);
    if ((error = getaddrinfo(hostname, service, &hints, &ai)) != 0) {
       if (error != EAI_SYSTEM) {
          fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(error));
       } else {
          perror("getaddrinfo()");
       }
    }

    return ai;
}

int libmemc_connect_server(const char *hostname, in_port_t port)
{
    struct addrinfo *ai = lookuphost(hostname, port);
    int sock = -1;
    if (ai != NULL) { 
       if ((sock = socket(ai->ai_family, ai->ai_socktype,
                          ai->ai_protocol)) != -1) {
          if (connect(sock, ai->ai_addr, ai->ai_addrlen) == -1) {
             fprintf(stderr, "Failed to connect socket: %s\n",
                     strerror(errno));
             close(sock);
             sock = -1;
          }
       } else {
          fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
       }
    
       freeaddrinfo(ai);
    }
    return sock;
}

/**
 * Internal functions used by both protocols
 */
static uint32_t simplehash(const char *key) {
   if (key == 0) {
      return 0;
   }
   uint32_t ret = 0;
   for (ret = *key; *key != 0; ++key) {
      ret = (ret << 4) + *key;
   }
   return ret;
}

static struct Server *get_server(struct Memcache *handle, const char *key) {
   if (handle->no_servers == 1) {
      return handle->servers[0];
   } else if (handle->no_servers > 0) {
      int idx = simplehash(key) % handle->no_servers;
      return handle->servers[idx];
   } else {
      return NULL;
   }
}

static int libmemc_store(struct Memcache* handle, enum StoreCommand cmd, 
                         struct Item *item) {
   struct Server* server = get_server(handle, item->key);
   if (server == NULL) {
      return -1;
   } else {
      if (server->sock == -1) {
         if (server_connect(server) == -1) {
            return -1;
         }
      }
      
      if (handle->protocol == Binary) {
         return binary_store(server, cmd, item);
      } else {
         return textual_store(server, cmd, item);
      }
   }
}

static size_t server_receive(struct Server* server, char* data, size_t size, int line);
static int server_sendv(struct Server* server, struct iovec *iov, int iovcnt);
static int server_send(struct Server* server, const void *data, size_t size);
static int server_connect(struct Server *server);
static void server_disconnect(struct Server *server);

void server_destroy(struct Server *server) {
   if (server != NULL) {
      if (server->sock != -1) {
         close(server->sock);
      }
      free(server->buffer);
      free(server);
   }
}

struct Server* server_create(const char *name, in_port_t port) {
   struct addrinfo* ai = lookuphost(name, port);
   struct Server* ret = NULL;
   if (ai != NULL) {
      ret = calloc(1, sizeof(struct Server));
      if (ret != 0) {
         char buffer[1024];         
         ret->sock = -1;
         ret->errmsg = 0;
         ret->addrinfo = ai;
         sprintf(buffer, "%s:%d", name, port);
         ret->peername = strdup(buffer);
         ret->buffer = malloc(65 * 1024);
         ret->buffersize = 65 * 1024;
         server_connect(ret);
         if (ret->buffer == NULL) {
            server_destroy(ret);
            ret = 0;
         }
      }
   }
    
   return ret;
}

static void server_disconnect(struct Server *server) {
   if (server->sock != -1) {
      (void)close(server->sock);
      server->sock = -1;
   }
}

static int server_connect(struct Server *server)
{
   int flag = 1;
   
   if ((server->sock = socket(server->addrinfo->ai_family,
                              server->addrinfo->ai_socktype,
                              server->addrinfo->ai_protocol)) == -1) {
      char errmsg[1024];
      sprintf(errmsg, "Failed to create socket: %s", strerror(errno));
      server->errmsg = strdup(errmsg);
      return -1;
   }

   if (setsockopt(server->sock, IPPROTO_TCP, TCP_NODELAY,
                  &flag, sizeof(flag)) == -1) {
      perror("Failed to set TCP_NODELAY");
   }
   
   if (connect(server->sock, server->addrinfo->ai_addr,
               server->addrinfo->ai_addrlen) == -1) {
      char errmsg[1024];
      sprintf(errmsg, "Failed to connect socket: %s", strerror(errno));
      server->errmsg = strdup(errmsg);
      server_disconnect(server);
      return -1;
   }
    
   return 0;
}

static int server_send(struct Server* server, const void *data, size_t size) {
   size_t offset = 0;
   do {
      ssize_t sent = send(server->sock, ((const char*)data) + offset, size - offset, 0);
      if (sent == -1) {
         if (errno != EINTR) {
            char errmsg[1024];
            sprintf(errmsg, "Failed to send data to server: %s", strerror(errno));
            server->errmsg = strdup(errmsg);
            server_disconnect(server);
            return -1;
         }
      } else {
         offset += sent;
      }
   } while (offset < size);
    
   return 0;
}

static int server_sendv(struct Server* server, struct iovec *iov, int iovcnt) {
#ifdef WIN32
   // @todo I might have a scattered IO function on windows...
   for (int ii = 0; ii < iovcnt; ++ii) {
      if (send(server, iov[ii].iov_base, iov[ii].iov_len) != 0) {
         return -1;
      }
   }
#else
   // @todo Verify implementation if the writev returns with partitial
   // writes!
   size_t size = 0;
   for (int ii = 0;  ii < iovcnt; ++ ii) {
      size += iov[ii].iov_len;
   }

   do {
      ssize_t sent = writev(server->sock, iov, iovcnt);

      if (sent == -1) {
         if (errno != EINTR) {
            char errmsg[1024];
            sprintf(errmsg, "Failed to send data to server: %s", strerror(errno));
            server->errmsg = strdup(errmsg);
            fprintf(stderr, "%s%s", server->errmsg,"\n");
            fflush(stderr);
            server_disconnect(server);
            return -1;
         }
      } else {
         if (sent == size) {
            return 0;
         }
            
         for (int ii = 0; ii < iovcnt && sent > 0; ++ii) {
            if (iov[ii].iov_len < sent) {
               size -= iov[ii].iov_len;
               sent -= iov[ii].iov_len;
               iov[ii].iov_len = 0;
            } else {
#ifdef __sun
               iov[ii].iov_base += sent;
#else
               // iov_base is a void pointer...
               iov[ii].iov_base = ((char*)iov[ii].iov_base) + sent;
#endif
               iov[ii].iov_len -= sent;
               size -= sent;
               break;
            }
         }
      }
   } while (size > 0);
#endif
   return 0;
}

static size_t server_receive(struct Server* server, char* data, size_t size, int line) {
   size_t offset = 0;
   int stop = 0;
   do {
      ssize_t nread = recv(server->sock, data + offset, size - offset, 0);
      if (nread == -1) {
         if (errno != EINTR) {
            char errmsg[1024];
            sprintf(errmsg, "Failed to receive data from server: %s", strerror(errno));
            server->errmsg = strdup(errmsg);
            server_disconnect(server);
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
      server->errmsg = strdup("Protocol error");
      server_disconnect(server);
      return -1;
   }
    
   return offset;
}

/* Byte swap a 64-bit number */
static int64_t swap64(int64_t in) {
#ifndef __sparc
    /* Little endian, flip the bytes around until someone makes a faster/better
    * way to do this. */
    int64_t rv = 0;
    int i = 0;
     for(i = 0; i < 8; i++) {
        rv = (rv << 8) | (in & 0xff);
        in >>= 8;
     }
    return rv;
#else
    /* big-endian machines don't need byte swapping */
    return in;
#endif
}

/**
 * Implementation of the Binary protocol
 */
static int binary_get(struct Server* server, struct Item* item) 
{
#if HAVE_PROTOCOL_BINARY
   uint16_t keylen = item->keylen;
   uint32_t bodylen = keylen;

   protocol_binary_request_get request = { .bytes = {0} };
   request.message.header.request.magic = PROTOCOL_BINARY_REQ;
   request.message.header.request.opcode = PROTOCOL_BINARY_CMD_GET;
   request.message.header.request.keylen = htons(keylen);
   request.message.header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
   request.message.header.request.bodylen = htonl(bodylen);
   
   struct iovec iovec[2];
   iovec[0].iov_base = (void*)&request;
   iovec[0].iov_len = sizeof(protocol_binary_request_header);
   iovec[1].iov_base = (void*)item->key;
   iovec[1].iov_len = keylen;

   server_sendv(server, iovec, 2);

   protocol_binary_response_no_extras response;
   size_t nread = server_receive(server, (char*)response.bytes,
                                 sizeof(response.bytes), 0);
   if (nread != sizeof(response)) {
      server->errmsg = strdup("Protocol error");
      server_disconnect(server);      
      return -1;
   }

   bodylen = ntohl(response.message.header.response.bodylen);
   if (response.message.header.response.status == 0) {
      if (item->data != NULL) {
         if ((bodylen-response.message.header.response.extlen) > item->size) {
            free(item->data);
            item->data = NULL;
         }
      }

      item->size = bodylen - response.message.header.response.extlen;

      if (item->data == NULL) {
         item->data = malloc(item->size);
         if (item->data == NULL) {
            server->errmsg = strdup("failed to allocate memory\n");
            server_disconnect(server);      
            return -1;
         }
      }

      if (response.message.header.response.extlen != 0) {
         assert(response.message.header.response.extlen == 4);
         uint32_t flags;
         struct iovec iovec[2];
         iovec[0].iov_base = (void*)&flags;
         iovec[0].iov_len = sizeof(flags);
         iovec[1].iov_base = item->data;
         iovec[1].iov_len = item->size;

         ssize_t nread = readv(server->sock, iovec, 2);
         if (nread < bodylen) {
             // partial read.. read the rest!
             nread -= 4;
             size_t left = item->size - nread;
             if (server_receive(server, item->data + nread, left, 0) != left) {
                 abort();
             }
         }
         item->flags = ntohl(flags);
      } else {
         size_t nread = server_receive(server, item->data, item->size, 0);
         assert(nread == item->size);
      }
      
      item->cas_id = swap64(response.message.header.response.cas);
   } else {
      char *buffer = malloc(bodylen + 1);
      if (buffer == NULL) {
         server->errmsg = strdup("failed to allocate memory\n");
         server_disconnect(server);      
         return -1;
      }
      buffer[bodylen] = '\0';
      server_receive(server, buffer, bodylen, 0);
      server->errmsg = buffer;
      return -1;
   }

   return 0;
#else
   return -1;
#endif
}

static int binary_store(struct Server* server, 
                         enum StoreCommand cmd, 
                         struct Item *item)  
{
#if HAVE_PROTOCOL_BINARY
   protocol_binary_request_set request = { .bytes = {0} };
   request.message.header.request.magic = PROTOCOL_BINARY_REQ;

   switch (cmd) {
   case add :
      request.message.header.request.opcode = PROTOCOL_BINARY_CMD_ADD; break;
   case set :
      request.message.header.request.opcode = PROTOCOL_BINARY_CMD_SET; break;
   case replace :
      request.message.header.request.opcode = PROTOCOL_BINARY_CMD_REPLACE; break;
   case cas :
      request.message.header.request.opcode = PROTOCOL_BINARY_CMD_SET; break;
   default:
      abort();
   }

   uint16_t keylen = item->keylen;
   request.message.header.request.keylen = htons(keylen);
   request.message.header.request.extlen = 8;
   request.message.header.request.datatype = 0;
   request.message.header.request.reserved = 0;
   request.message.header.request.bodylen = htonl(keylen + item->size + 8);
   request.message.header.request.opaque = 0;
   request.message.header.request.cas = swap64(item->cas_id);
   request.message.body.flags = htonl(item->flags);
   request.message.body.expiration = htonl(item->exptime);
   
   struct iovec iovec[3];
   iovec[0].iov_base = (void*)&request;
   iovec[0].iov_len = sizeof(protocol_binary_request_header) +
                      sizeof(request.message.body.flags) +
                      sizeof(request.message.body.expiration);
   iovec[1].iov_base = (void*)item->key;
   iovec[1].iov_len = keylen;
   iovec[2].iov_base = item->data;
   iovec[2].iov_len = item->size;

   server_sendv(server, iovec, 3);

   protocol_binary_response_set response;
   size_t nread = server_receive(server, (char*)response.bytes,
                                 sizeof(response.bytes), 0);

   if (nread != sizeof(response)) {
      item->errmsg = strdup("Protocol error");
      server->errmsg = strdup(item->errmsg);
      fprintf(stderr, item->errmsg);
      fflush(stderr);
      server_disconnect(server);      
      return -1;
   }

   if (response.message.header.response.status == 0 &&
       response.message.header.response.bodylen != 0) {
      item->errmsg = strdup("Unexpected data returned");
      server->errmsg = strdup(item->errmsg);
      fprintf(stderr, item->errmsg);
      fflush(stderr);
      server_disconnect(server);      
      return -1;
   } else if (response.message.header.response.bodylen != 0) {
      uint32_t len = ntohl(response.message.header.response.bodylen);
      char* buffer = malloc(len + 1);
      if (buffer == 0) {
         item->errmsg = strdup("failed to allocate memory");
         server->errmsg = strdup(item->errmsg);
         fprintf(stderr, item->errmsg);
         fflush(stderr);
         server_disconnect(server);      
         return -1;
      }

      size_t nread = server_receive(server, buffer, len, 0);
      memset(buffer + len, 0, 1);
      item->errmsg = strdup(buffer);
      free(buffer);
   } else {
      item->errmsg = strdup("Stored");
   }

   return (response.message.header.response.status == 0) ? 0 : -1;
#else
  return -1;
#endif
}

/**
 * Implementation of the Textual protocol
 */
static int parse_value_line(char *header, uint32_t* flag, size_t* size, uint64_t* cas_id, char** data) {
   char *end = strchr(header, ' ');
   if (end == 0) {
      return -1;
   }
   char *start = end + 1;
   *flag = (uint32_t)strtoul(start, &end, 10);
   if (start == end) {
      return -1;
   }
   start = end + 1;
   *size = (size_t)strtoul(start, &end, 10);
   if (start == end) {
      return -1;
   }
   start = end + 1;
   *cas_id = (uint64_t)strtoull(start, &end, 10);
   if (start == end) {
      return -1;
   }
   if (strstr(end, "\r\n") != end) {
      return -1;
   }
    
   *data = end + 2;
   return 0;
}

/**
 * Implementation of the Textual protocol
 */
static int parse_value_line_gets(char *header, uint32_t* flag, size_t* size, uint64_t* cas_id, char** data) {
   char *end = strchr(header, ' ');
   if (end == 0) {
      return -1;
   }
   char *start = end + 1;
   *flag = (uint32_t)strtoul(start, &end, 10);
   if (start == end) {
      return -1;
   }
   start = end + 1;
   *size = (size_t)strtoul(start, &end, 10);
   if (start == end) {
      return -1;
   }
   start = end + 1;
   *cas_id = (uint64_t)strtoull(start, &end, 10);
   if (start == end) {
      return -1;
   }
   if (strstr(end, "\r\n") != end) {
      return -1;
   }
    
   *data = end + 2;
   return 0;
}

static int textual_get(struct Server* server, struct Item* item) {    
   uint32_t flag;
   uint64_t cas_id;

   struct iovec iovec[3];
   iovec[0].iov_base = (char*)"gets ";
   iovec[0].iov_len = 5;
   iovec[1].iov_base = (char*)item->key;
   iovec[1].iov_len = item->keylen;
   iovec[2].iov_base = (char*)"\r\n";
   iovec[2].iov_len = 2;
   server_sendv(server, iovec, 3);

   size_t nread = server_receive(server, server->buffer,server->buffersize, 1);

   // Split the header line
   if (strstr(server->buffer, "VALUE ") == server->buffer) {
      size_t elemsize;
      char *ptr;
      
      if (parse_value_line(server->buffer + 6, &flag, &elemsize, &cas_id, &ptr) == -1){
         server->errmsg = strdup("Protocol error");
         server_disconnect(server);
         return -1;
      }
#ifndef __sun
      typedef size_t ptrdiff_t;
#endif
      ptrdiff_t headsize = ptr - server->buffer;
      size_t chunk = nread - headsize;

      if (chunk < (elemsize + 7)) {
         // I don't have all of the data.. keep on reading
         if (server->buffersize < (headsize + elemsize + 7)) {
             server->buffer = realloc(server->buffer, (headsize + elemsize + 7));
             if (server->buffer == 0) {
                server->errmsg = strdup("failed to allocate memory\n");
                server_disconnect(server);
                return -1;                    
             }
             server->buffersize = (headsize + elemsize + 7);
         }
         server_receive(server, server->buffer + nread,
                        (elemsize - chunk) + 7, 0);
      }

      if (elemsize > item->size) {
         if (item->data != 0) {
            free(item->data);
         }
         item->size = elemsize;
         item->data = malloc(item->size);
         if (item->data == 0) {
            item->size = 0;
            server->errmsg = strdup("failed to allocate memory\n");
            server_disconnect(server);
            return -1;
         }
      } else {
         item->size = elemsize;
      }
      item->flags = flag;
      item->cas_id = cas_id;
      memcpy(item->data, server->buffer + headsize, item->size);
      return 0;
   } else if (strstr(server->buffer, "END") == server->buffer) {
      return -1;
   } else {
      server->errmsg = strdup("Protocol error");
      server_disconnect(server);
      return -1;
   }
}

static int textual_gets(struct Server* server, struct Item item[], int items) {
   uint32_t flag;
   int length = sprintf(server->buffer, "gets");
   for (int i=0; i<items; i++) {
      length += sprintf(server->buffer + length, " %s", item[i].key);
   }
   length += sprintf(server->buffer + length, "\r\n");
   server_send(server, (void*)server->buffer, length);

   size_t nread = server_receive(server, server->buffer, server->buffersize, 1);

   char *buffer_ptr = server->buffer;
   char *data_ptr;
   size_t elemsize;
#ifndef __sun
   typedef size_t ptrdiff_t;
#endif
   ptrdiff_t headsize;

   for (int i=0; i<items; i++) {
      // Split the header line
      if (strstr(buffer_ptr, "VALUE ") == buffer_ptr) {
         char *key = buffer_ptr + 6;
         char *end = strchr(key, ' ');
         int keylen = end - key;
         uint64_t cas_id;
         if (parse_value_line_gets(buffer_ptr + 6, &flag, &elemsize, &cas_id, &data_ptr) == -1) {
            server->errmsg = strdup("Protocol error");
            server_disconnect(server);
            return -1;
         }
         
         // Find item
         struct Item *curr_item = 0;
         for (int j=0; j<items; j++) {
             if ((item[j].keylen == keylen) && 
                 (!strncmp(item[j].key, key, keylen))) {
                 curr_item = &item[j];
                 break;
             }
         }
         if (curr_item == 0) {
            server->errmsg = strdup("Protocol error");
            server_disconnect(server);
            return -1;
         }
         
         headsize = data_ptr - buffer_ptr;
         size_t chunk = nread - headsize;
        
         if (chunk < (elemsize + 2)) {
            // I don't have all of the data.. keep on reading
            if (server->buffersize < (headsize + elemsize + 2)) {
               server->buffer = realloc(server->buffer, (headsize + elemsize + 2));
               if (server->buffer == 0) {
                  server->errmsg = strdup("failed to allocate memory\n");
                  server_disconnect(server);
                  return -1;                    
               }
               buffer_ptr = server->buffer;
               server->buffersize = (headsize + elemsize + 2);
            }
            nread += server_receive(server, buffer_ptr + nread, (elemsize - chunk) + 2, 0);
         }

         if (elemsize > curr_item->size) {
            if (curr_item->data != 0) {
               free(curr_item->data);
            }
            curr_item->size = elemsize;
            curr_item->data = malloc(curr_item->size);
            if (curr_item->data == 0) {
               curr_item->size = 0;
               server->errmsg = strdup("failed to allocate memory\n");
               server_disconnect(server);
               return -1;
            }
         } else {
            curr_item->size = elemsize;
         }
         memcpy(curr_item->data, buffer_ptr + headsize, curr_item->size);
         curr_item->flags = flag;
         curr_item->cas_id = cas_id;
         
      } else if (strstr(server->buffer, "END") == server->buffer) {
         return 0;
      } else {
        server->errmsg = strdup("Protocol error");
        server_disconnect(server);
        return -1;
      }

      //prepare for reading the next item
      int processed = headsize + elemsize + 2;
      buffer_ptr = server->buffer + processed;
      nread -= processed;
      memmove(server->buffer, buffer_ptr, nread);
      buffer_ptr = server->buffer;
      // Make sure "\r\n" has been read
      if ((nread < 2) || (strncmp(buffer_ptr + nread - 2, "\r\n", 2) != 0)) {
          nread += server_receive(server, buffer_ptr + nread, 
                  server->buffersize - nread, 1);
      }
   }

   return 0;
}

static int binary_gets(struct Server* server, struct Item item[], int items) {
#if HAVE_PROTOCOL_BINARY

   // send all the item requests as "get key quiet"
   for (int i=0; i<items; i++) {
      uint16_t keylen = item[i].keylen;
      uint32_t bodylen = keylen;

      protocol_binary_request_get request = { .bytes = {0} };
      request.message.header.request.magic = PROTOCOL_BINARY_REQ;
      request.message.header.request.opcode = PROTOCOL_BINARY_CMD_GETKQ;
      request.message.header.request.keylen = htons(keylen);
      request.message.header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
      request.message.header.request.bodylen = htonl(bodylen);
   
      struct iovec iovec[2];
      iovec[0].iov_base = (void*)&request;
      iovec[0].iov_len = sizeof(protocol_binary_request_header);
      iovec[1].iov_base = (void*)item[i].key;
      iovec[1].iov_len = keylen;

      server_sendv(server, iovec, 2);
   }

   // flush with noop
   protocol_binary_request_noop noopreq = { .bytes = {0} };
   noopreq.message.header.request.magic = PROTOCOL_BINARY_REQ;
   noopreq.message.header.request.opcode = PROTOCOL_BINARY_CMD_NOOP;
   struct iovec iovec_noop[2];
   iovec_noop[0].iov_base = (void*)&noopreq;
   iovec_noop[0].iov_len = sizeof(noopreq);
   server_sendv(server, iovec_noop, 1);

   for (int i=0; i<items; i++)
   {
      free(item[i].data);
      item[i].size = 0;
   }

   // receive the items that were found
   while (1) {
      protocol_binary_response_no_extras response;
      size_t nread = server_receive(server, (char*)response.bytes,
                                 sizeof(response.bytes), 0);
      if (nread != sizeof(response)) {
         server->errmsg = strdup("Protocol error");
         server_disconnect(server);      
         return -1;
      }
      
      uint8_t extlen = response.message.header.response.extlen; // flags
      uint16_t keylen = ntohs(response.message.header.response.keylen);
      uint32_t bodylen = ntohl(response.message.header.response.bodylen);
      uint32_t datalen = bodylen - extlen - keylen;
      if (response.message.header.response.opcode == PROTOCOL_BINARY_CMD_NOOP) {
         // no more items
         break;
      } else if ((response.message.header.response.opcode != PROTOCOL_BINARY_CMD_GETKQ) ||
                (extlen != sizeof(uint32_t))) {
         server->errmsg = strdup("Protocol error");
         server_disconnect(server);      
         return -1;
      }

      // read flags, key
      uint32_t flags;
      char key[256];

      struct iovec iovec_flagskey[2];
      iovec_flagskey[0].iov_base = (void*)&flags;
      iovec_flagskey[0].iov_len = sizeof(flags);
      iovec_flagskey[1].iov_base = key;
      iovec_flagskey[1].iov_len = keylen;
      nread = readv(server->sock, iovec_flagskey, 2);
      if (nread < (extlen + keylen)) {
         // partial read.. read the rest!
         nread -= 4;
         size_t left = (extlen + keylen) - nread;
         if (server_receive(server, key + nread, left, 0) != left) {
            abort();
         }
      }

      // data
      char *data = malloc(datalen);

      if (data == NULL) {
         server->errmsg = strdup("failed to allocate memory\n");
         server_disconnect(server);      
         return -1;
      }
      struct iovec iovecdata[1];
      iovecdata[0].iov_base = data;
      iovecdata[0].iov_len = datalen;
      nread = readv(server->sock, iovecdata, 1);
      if (nread < datalen) {
         // partial read.. read the rest!
         size_t left = datalen - nread;
         if (server_receive(server, data + nread, left, 0) != left) {
            abort();
         }
      }
      
      //find the item
      int found = 0;
      for (int i=0; i<items; i++)
      {
         if ((keylen == item[i].keylen) && 
             (!memcmp(item[i].key, key, keylen))) {
            item[i].flags = ntohl(flags);
            item[i].cas_id = swap64(response.message.header.response.cas);
            item[i].data = data;
            item[i].size = datalen;
            found = 1;
            break;
         }
      }
      if (!found)
          free(data);
   }
   return 0;
#else
   return -1;
#endif
}

static int textual_store(struct Server* server, 
                         enum StoreCommand cmd, 
                         struct Item *item)  {
   static const char* const commands[] = { "add ", "set ", "replace ", "cas " };

   uint32_t flags = item->flags;
   const void *dta = item->data;
   size_t size = item->size;
   ssize_t len;
   if (cmd == cas)
      len = sprintf(server->buffer, " %d %ld %ld %lld\r\n", 
           flags, (long)item->exptime, (long)item->size, item->cas_id);
   else
      len = sprintf(server->buffer, " %d %ld %ld\r\n", 
           flags, (long)item->exptime, (long)item->size);
   
   struct iovec iovec[5];
   iovec[0].iov_base = (char*)commands[cmd];
   iovec[0].iov_len = strlen(commands[cmd]);
   iovec[1].iov_base = (char*)item->key;
   iovec[1].iov_len = item->keylen;
   iovec[2].iov_base = server->buffer;
   iovec[2].iov_len = len;
   iovec[3].iov_base = (char*)dta;
   iovec[3].iov_len = size;
   iovec[4].iov_base = (char*)"\r\n";
   iovec[4].iov_len = 2;

   server_sendv(server, iovec, 5);

   size_t offset = 0;
   do {
       len = recv(server->sock, (void*)(server->buffer + offset),
               server->buffersize - offset, 0);

      if (len == -1) {
         if (errno != EINTR) {
            char errmsg[1024];
            sprintf(errmsg, "Failed to receive data from server: %s",
                    strerror(errno));
            server->errmsg = strdup(errmsg);
            fprintf(stderr, "%s\n",server->errmsg);
            fflush(stderr);
            server_disconnect(server);
            return -1;
         }
      } else if (len == 0) {
         server->errmsg = strdup("Lost contact with server");
         fprintf(stderr, "%s\n",server->errmsg);
         fflush(stderr);
         server_disconnect(server);
         return -1;
      } else {
         offset += len;
         if (strchr(server->buffer, '\r') != 0) {
            if (strstr(server->buffer, "STORED\r\n") == server->buffer) {
               item->errmsg = strdup("STORED");
               return 0;
            } else if (strstr(server->buffer,
                              "NOT_STORED\r\n") == server->buffer) {
               item->errmsg = strdup("NOT_STORED");
               server->errmsg = strdup(item->errmsg);
               return -1;
            } else if (strstr(server->buffer,
                              "SERVER_ERROR out of memory storing object\r\n") == server->buffer) {
               item->errmsg = strdup("SERVER_ERROR out of memory storing object");
               server->errmsg = strdup(item->errmsg);
               return -1;
            } else if (strstr(server->buffer,
                              "SERVER_ERROR object too large for cache\r\n") == server->buffer) {
               item->errmsg = strdup("SERVER_ERROR object too large for cache");
               server->errmsg = strdup(item->errmsg);
               return -1;
            } else if (strstr(server->buffer,
                              "EXISTS\r\n") == server->buffer) {
               item->errmsg = strdup("EXISTS");
               server->errmsg = strdup("Item NOT stored - wrong cas id");
               return -1;
            } else if (strstr(server->buffer,
                              "NOT_FOUND\r\n") == server->buffer) {
               item->errmsg = strdup("NOT_FOUND");
               server->errmsg = strdup(item->errmsg);
               return -1;
            }
         }
      }
      if (offset == server->buffersize) {
         server->errmsg = strdup("Out of sync with server...");
         server_disconnect(server);
         return -1;
      }
   } while (1);
}

int libmemc_incr(struct Memcache *handle, struct Item *item, uint64_t delta) {
   return libmemc_incr_decr(handle, incr, item, delta);
}

int libmemc_decr(struct Memcache *handle, struct Item *item, uint64_t delta) {
   return libmemc_incr_decr(handle, decr, item, delta);
}

static int libmemc_incr_decr(struct Memcache *handle,
                        enum IncrDecrCommand cmd, 
                        struct Item *item,
                        uint64_t delta)
{
   struct Server* server = get_server(handle, item->key);
   if (server == NULL) {
      return -1;
   } else {
      if (server->sock == -1) {
         if (server_connect(server) == -1) {
            return -1;
         }
      }
      
      if (handle->protocol == Binary) {
         return binary_incr_decr(server, cmd, item, delta);
      } else {
         return textual_incr_decr(server, cmd, item, delta);
      }
   }
}

static int textual_incr_decr(struct Server* server, 
                            enum IncrDecrCommand cmd, 
                            struct Item *item,
                            uint64_t delta) {
   static const char* const commands[] = { "incr ", "decr " };
   ssize_t len = sprintf(server->buffer, " %ld\r\n", delta);
    
   struct iovec iovec[3];
   iovec[0].iov_base = (char*)commands[cmd];
   iovec[0].iov_len = strlen(commands[cmd]);
   iovec[1].iov_base = (char*)item->key;
   iovec[1].iov_len = item->keylen;
   iovec[2].iov_base = server->buffer;
   iovec[2].iov_len = len;
   server_sendv(server, iovec, 3);

   size_t offset = 0;
   do {
      len = recv(server->sock, (void*)(server->buffer + offset),
                 server->buffersize - offset, 0);
      if (len == -1) {
         if (errno != EINTR) {
            char errmsg[1024];
            sprintf(errmsg, "Failed to receive data from server: %s",
                    strerror(errno));
            server->errmsg = strdup(errmsg);
            server_disconnect(server);
            return -1;
         }
      } else if (len == 0) {
         server->errmsg = strdup("Lost contact with server");
         server_disconnect(server);
         return -1;
      } else {
         offset += len;
         if (strchr(server->buffer, '\r') != 0) {
            } if (strstr(server->buffer,
                              "NOT_FOUND\r\n") == server->buffer) {
               server->errmsg = strdup("Item NOT found");
               return -1;
            } else if (strstr(server->buffer, "\r\n") != NULL) {
               if (item->data != 0) {
                  free(item->data);
               }
               item->data = malloc(offset - 2);
               memcpy(item->data, server->buffer, offset - 2);
			   item->size = offset - 2;

               return 0;
         }
      }
      if (offset == server->buffersize) {
         server->errmsg = strdup("Out of sync with server...");
         server_disconnect(server);
         return -1;
      }
   } while (1);  
}

static int binary_incr_decr(struct Server* server,
                           enum IncrDecrCommand cmd,
                           struct Item *item,
                           uint64_t delta)
{
#if HAVE_PROTOCOL_BINARY
   protocol_binary_request_incr request = {.bytes= {0}};

   request.message.header.request.magic = PROTOCOL_BINARY_REQ;

   switch (cmd) {
   case incr :
      request.message.header.request.opcode = PROTOCOL_BINARY_CMD_INCREMENT; break;
   case decr :
      request.message.header.request.opcode = PROTOCOL_BINARY_CMD_DECREMENT; break;
   default:
      abort();
   }

   uint16_t keylen = item->keylen;
   request.message.header.request.keylen = htons(keylen);
   request.message.header.request.extlen = 20;
   request.message.header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
   request.message.header.request.bodylen = htonl(keylen + request.message.header.request.extlen);
   request.message.body.delta = swap64(delta);

   if ((item->data != NULL) && (item->size > 0)) {
      char *tmp = malloc(item->size + 1);
      memcpy(tmp, item->data, item->size);
      memset(tmp + item->size, 0, 1);
      request.message.body.initial = swap64(strtoull(tmp, NULL, 10));
      free(tmp);
   } else {
       request.message.body.initial = 0;
   }
   request.message.body.expiration = htonl(item->exptime);

   struct iovec iovec[2];
   iovec[0].iov_base = (void*)&request;
   iovec[0].iov_len = sizeof(protocol_binary_request_header) +
                      sizeof(request.message.body.delta) +
                      sizeof(request.message.body.initial) +
                      sizeof(request.message.body.expiration);
   iovec[1].iov_base = (void*)item->key;
   iovec[1].iov_len = keylen;
   server_sendv(server, iovec, 2);

   protocol_binary_response_incr response;
   size_t nread = server_receive(server, (char*)response.bytes,
                                 sizeof(protocol_binary_response_header), 0);

   if (nread != sizeof(protocol_binary_response_header)) {
      server->errmsg = strdup("Protocol error");
      server_disconnect(server);      
      return -1;
   }
   uint32_t bodylen = ntohl(response.message.header.response.bodylen);
   char *buffer = malloc(bodylen + 1);
   if (buffer == NULL) {
       server->errmsg = strdup("failed to allocate memory\n");
       server_disconnect(server);      
       return -1;
    }
    buffer[bodylen] = '\0';
    server_receive(server, buffer, bodylen, 0);

   if (response.message.header.response.status == 0) {
      memcpy(&response.message.body.value, buffer, 8);

      if (item->data != NULL) {
         free(item->data);
         item->data = NULL;   
      }

      if (item->data == NULL) {
         char tmp[50];
         sprintf(tmp, "%llu", swap64(response.message.body.value));
         item->size = strlen(tmp);
         item->data = malloc(item->size);
         if (item->data == NULL) {
            server->errmsg = strdup("failed to allocate memory\n");
            server_disconnect(server);      
            return -1;
         }
         memcpy(item->data, tmp, item->size);
      }
   } else {
       server->errmsg = buffer;
   }

   return (response.message.header.response.status == 0) ? 0 : -1;
#else
  return -1;
#endif
}

int libmemc_delete(struct Memcache *handle, struct Item *item) {
   struct Server* server = get_server(handle, item->key);
   if (server == NULL) {
      return -1;
   } else {
      if (server->sock == -1) {
         if (server_connect(server) == -1) {
            fprintf(stderr, "%s\n", server->errmsg);
            fflush(stderr);
            return -1;
         }
      }

      if (handle->protocol == Binary) {
         return binary_delete(server, item);
      } else {
         return textual_delete(server, item);
      }
   }
}

static int textual_delete(struct Server* server, struct Item* item)
{
   struct iovec iovec[3];
   iovec[0].iov_base = (char*)"delete ";
   iovec[0].iov_len = 7;
   iovec[1].iov_base = (char*)item->key;
   iovec[1].iov_len = item->keylen;
   iovec[2].iov_base = (char*)"\r\n";
   iovec[2].iov_len = 2;
   server_sendv(server, iovec, 3);

   size_t offset = 0;
   do {
      size_t len = recv(server->sock, (void*)(server->buffer + offset),
                 server->buffersize - offset, 0);
      if (len == -1) {
         if (errno != EINTR) {
            char errmsg[1024];
            sprintf(errmsg, "Failed to receive data from server: %s",
                    strerror(errno));
            server->errmsg = strdup(errmsg);
            item->errmsg = strdup(errmsg);
            server_disconnect(server);
            return -1;
         }
      } else if (len == 0) {
          item->errmsg = strdup("Lost contact with server");
          server->errmsg = strdup(item->errmsg);
         server_disconnect(server);
         return -1;
      } else {
         offset += len;
         if (strchr(server->buffer, '\r') != 0) {
            if (strstr(server->buffer, "DELETED\r\n") == server->buffer) {
               item->errmsg = strdup("DELETED");
               server->errmsg = strdup(item->errmsg);
               return 0;
            } else if (strstr(server->buffer,
                              "NOT_FOUND\r\n") == server->buffer) {
               item->errmsg = strdup("NOT_FOUND");
               server->errmsg = strdup(item->errmsg);
               return 0;
            } else {
               return -1;
            }
         }
      }
      if (offset == server->buffersize) {
         item->errmsg = strdup("Out of sync with server...");
         server->errmsg = strdup(item->errmsg);
         server_disconnect(server);
         return -1;
      }
   } while (1);
}

static int binary_delete(struct Server* server, struct Item* item)
{
#if HAVE_PROTOCOL_BINARY
   protocol_binary_request_delete request = {.bytes= {0}};

   request.message.header.request.magic = PROTOCOL_BINARY_REQ;
   request.message.header.request.opcode = PROTOCOL_BINARY_CMD_DELETE;

   uint16_t keylen = item->keylen;
   request.message.header.request.keylen = htons(keylen);
   request.message.header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
   request.message.header.request.bodylen = htonl(keylen);

   struct iovec iovec[2];
   iovec[0].iov_base = (void*)&request;
   iovec[0].iov_len = sizeof(protocol_binary_request_header);
   iovec[1].iov_base = (void*)item->key;
   iovec[1].iov_len = keylen;
   server_sendv(server, iovec, 2);

   protocol_binary_response_delete response;
   size_t nread = server_receive(server, (char*)response.bytes,
                                 sizeof(protocol_binary_response_delete), 0);

   if (nread != sizeof(protocol_binary_response_delete)) {
      item->errmsg = strdup("Protocol error");
      server->errmsg = strdup(item->errmsg);
      server_disconnect(server);      
      return -1;
   }

   uint32_t bodylen = ntohl(response.message.header.response.bodylen);
   char *buffer = malloc(bodylen + 1);
   if (buffer == NULL) {
       item->errmsg = strdup("failed to allocate memory");
       server->errmsg = strdup(item->errmsg);
       server_disconnect(server);      
       return -1;
   }
   buffer[bodylen] = '\0';
   if (bodylen>0)
      server_receive(server, buffer, bodylen, 0);
   item->errmsg = strdup(buffer);
   server->errmsg = strdup(item->errmsg);

   return (response.message.header.response.status == 0) || 
          (ntohs(response.message.header.response.status) == 1) ? 0 : -1;
#else
  return -1;
#endif
}

int libmemc_flush_all(struct Memcache *handle, long exptime) {
   for (int i=0; i<handle->no_servers; i++) {
      if (handle->protocol == Textual) {
         return textual_flush_all(handle->servers[i], exptime);
      } else {	 
         return binary_flush_all(handle->servers[i], exptime);
      }
   }
}

static int textual_flush_all(struct Server *server, long exptime) {
   char sendbuffer[50];
   if (exptime < 0)
      sprintf(sendbuffer, "flush_all\r\n");
   else
      sprintf(sendbuffer, "flush_all %d\r\n", exptime);
   ssize_t sent = send(server->sock, sendbuffer, strlen(sendbuffer), 0);
   if (sent == -1) {
      if (errno != EINTR) {
         char errmsg[1024];
         sprintf(errmsg, "Failed to send data to server: %s", strerror(errno));
         server->errmsg = strdup(errmsg);
         server_disconnect(server);
         return -1;
      }
   } else {
      char recvdata[4];
      ssize_t nread = recv(server->sock, recvdata, 4, 0);
      if (nread == -1) {
         if (errno != EINTR) {
            char errmsg[1024];
            sprintf(errmsg, "Failed to receive data from server: %s", strerror(errno));
            server->errmsg = strdup(errmsg);
            server_disconnect(server);
            return -1;
         }
      }
      if (memcmp(recvdata, "OK\r\n", 4) != 0)
         return -1;
   }

  return 0;
}

static int binary_flush_all(struct Server *server, long exptime)
{
#if HAVE_PROTOCOL_BINARY
   if (exptime < 0)
        exptime = 0;
   protocol_binary_request_flush request = { .bytes = {0} };
   request.message.header.request.magic = PROTOCOL_BINARY_REQ;
   request.message.header.request.opcode = PROTOCOL_BINARY_CMD_FLUSH;
   request.message.header.request.extlen = sizeof(request.message.body.expiration);
   request.message.header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
   request.message.header.request.bodylen = htonl(request.message.header.request.extlen);
   request.message.body.expiration = htonl(exptime);
   
   struct iovec iovec[1];
   iovec[0].iov_base = (void*)&request;
   // don't use sizeof(request) here. struct alignment might give the wrong number of bytes
   iovec[0].iov_len = sizeof(protocol_binary_request_header) 
                    + sizeof(request.message.body.expiration);
   server_sendv(server, iovec, 1);

   protocol_binary_response_flush response;
   size_t nread = server_receive(server, (char*)response.bytes,
                                 sizeof(response.bytes), 0);
   if (nread != sizeof(response)) {
      server->errmsg = strdup("Protocol error");
      server_disconnect(server);      
      return -1;
   }
   if (response.message.header.response.status == 0 &&
       response.message.header.response.bodylen != 0) {
      server->errmsg = strdup("Unexpected data returned\n");
      server_disconnect(server);      
      return -1;
   }

   return (response.message.header.response.status == 0) ? 0 : -1;
#else
  return -1;
#endif
}

enum Protocol libmemc_get_protocol(struct Memcache *handle)
{
    return handle->protocol;
}

char* libmemc_stats(struct Server *server, enum Protocol protocol, const char* stats_type)
{
    if (protocol == Textual) {
        return textual_stats(server, stats_type);
    } else {	 
        return binary_stats(server, stats_type);
    }
}

static char* textual_stats(struct Server *server, const char* stats_type)
{
   char sendbuffer [100];
   if (stats_type != NULL)
       sprintf(sendbuffer, "stats %s\r\n", stats_type);
   else
       sprintf(sendbuffer, "stats\r\n");

   struct iovec iovec[3];
   iovec[0].iov_base = sendbuffer;
   iovec[0].iov_len = strlen(sendbuffer);
   server_sendv(server, iovec, 1);
   
   char* recvbuffer = malloc(8192);
   size_t nread = server_receive(server, recvbuffer, 8192, 1);
   recvbuffer[nread] = 0;

   return recvbuffer;
}

static char* binary_stats(struct Server *server, const char* stats_type)
{
#if HAVE_PROTOCOL_BINARY
   protocol_binary_request_stats request= {.bytes = {0}};
   request.message.header.request.magic= PROTOCOL_BINARY_REQ;
   request.message.header.request.opcode= PROTOCOL_BINARY_CMD_STAT;
   request.message.header.request.datatype= PROTOCOL_BINARY_RAW_BYTES;

   if (stats_type != NULL) 
   {
      int len= strlen(stats_type);
      request.message.header.request.keylen= htons((uint16_t)len);
      request.message.header.request.bodylen= htonl(len);
      
      struct iovec iovec[2];
      iovec[0].iov_base = (void*)&request;
      iovec[0].iov_len = sizeof(protocol_binary_request_header);
      iovec[1].iov_base = (void*)stats_type;
      iovec[1].iov_len = len;

      server_sendv(server, iovec, 2);
   } else {
      struct iovec iovec[1];
      iovec[0].iov_base = (void*)&request;
      iovec[0].iov_len = sizeof(protocol_binary_request_header);

      server_sendv(server, iovec, 1);
   }

   protocol_binary_response_stats response;

   size_t keylen;
   char* retvalue = malloc(8192);
   memset(retvalue, 0, 8192);
   char* ptr = retvalue;
   do {
      size_t nread = server_receive(server, (char*)response.bytes,
                                 sizeof(response.bytes), 0);

      if (nread != sizeof(response)) {
         server->errmsg = strdup("Protocol error");
         server_disconnect(server);      
         return NULL;
      }

      keylen = ntohs(response.message.header.response.keylen);
      int bodylen = ntohl(response.message.header.response.bodylen);
      char *stats = malloc(bodylen);

      struct iovec iovecrecv[1];
      iovecrecv[0].iov_base = stats;
      iovecrecv[0].iov_len = bodylen;
      nread = readv(server->sock, iovecrecv, 1);

      if (nread < bodylen) {
         // partial read.. read the rest!
         size_t left = bodylen - nread;
         if (server_receive(server, stats + nread, left, 0) != left) {
            abort();
         }
      }

      if (keylen > 0) {
         memcpy(ptr, stats, keylen);
	     memcpy(ptr + keylen, " ", 1);
         memcpy(ptr + keylen + 1, stats + keylen, bodylen - keylen);
	     memcpy(ptr + bodylen + 1, "\r\n", 2);
	     ptr += bodylen + 3;
      }

      free(stats);
   } while (keylen > 0);

   return retvalue;
#else
  return NULL;
#endif
}
