
#ifndef SR_NAT_TABLE_H
#define SR_NAT_TABLE_H

#include <inttypes.h>
#include <time.h>
#include <pthread.h>
#include "sr_router.h"
#include "sr_if.h"

/* ICMP message codes */
#define ACK_BIT 16
#define SYN_BIT 2
#define FIN_BIT 1
#define DESTINATION_UNREACHABLE 3
#define DESTINATION_PORT_UNREACHABLE 3
#define PORT_MIN  1024
#define ID_MIN  1
#define UNSOLICITED_TIMEOUT 6

typedef enum {
  nat_mapping_icmp,
  nat_mapping_tcp
  /* nat_mapping_udp, */
} sr_nat_mapping_type;

typedef enum {
  syn_sent,
  syn_received,
  fin_wait1,
  fin_wait2,
  established,
  closed
} sr_tcp_state;

struct sr_unsolicited_packet {
  uint8_t *packet;
  unsigned int len;
  char* interface;
  sr_ip_hdr_t *ip_header;
  time_t last_updated;
  struct sr_unsolicited_packet *next;
};

struct sr_nat_state {
  uint32_t seqno;
  uint32_t ackno;
  sr_tcp_state state;
};

struct sr_nat_connection {
  /* add TCP connection state data members here */
  uint8_t established;
  struct sr_nat_state src_state;
  struct sr_nat_state dst_state;
  time_t last_updated;
  uint32_t src_ip;
  uint32_t dst_ip;
  uint32_t src_port;
  uint32_t dst_port;
  uint8_t flags;
  struct sr_nat_connection *next;
};

struct sr_nat_mapping {
  sr_nat_mapping_type type;
  uint32_t ip_int; /* internal ip addr */
  uint32_t ip_ext; /* external ip addr */
  uint16_t aux_int; /* internal port or icmp id */
  uint16_t aux_ext; /* external port or icmp id */
  time_t last_updated; /* use to timeout mappings */
  struct sr_nat_connection *conns; /* list of connections. null for ICMP */
  struct sr_nat_mapping *next;
};

struct sr_nat {
  /* add any fields here */
  struct sr_nat_mapping *mappings;
  uint16_t id;
  uint16_t port;
  unsigned int icmpQueryTimeout;
  unsigned int tcpEstTimeout;
  unsigned int tcpTransTimeout;
  /* threading */
  pthread_mutex_t lock;
  pthread_mutexattr_t attr;
  pthread_attr_t thread_attr;
  pthread_t thread;
  struct sr_instance* sr;
  struct sr_unsolicited_packet* unsol_pkt;
};


int translate_packet(struct sr_instance* sr, uint8_t* packet, unsigned int len, char* interface);
int   sr_nat_init(struct sr_nat *nat);     /* Initializes the nat */
int   sr_nat_destroy(struct sr_nat *nat);  /* Destroys the nat (free memory) */
void *sr_nat_timeout(void *nat_ptr);  /* Periodic Timout */

/* Get the mapping associated with given external port.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_lookup_external(struct sr_nat *nat, uint16_t aux_ext, sr_nat_mapping_type type, struct sr_nat_connection* conn);

/* Get the mapping associated with given internal (ip, port) pair.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_lookup_internal(struct sr_nat *nat, uint32_t ip_int, uint16_t aux_int, sr_nat_mapping_type type, struct sr_nat_connection* conn);

/* Insert a new mapping into the nat's mapping table.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_insert_mapping(struct sr_nat *nat,
uint32_t ip_int, uint16_t aux_int, uint32_t ip_ext, sr_nat_mapping_type type, struct sr_nat_connection* conn);

#endif

