#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "sr_utils.h"
#include "sr_rt.h"
#include "sr_nat.h"

int sr_nat_init(struct sr_nat *nat) { /* Initializes the nat */

  assert(nat);

  /* Acquire mutex lock */
  pthread_mutexattr_init(&(nat->attr));
  pthread_mutexattr_settype(&(nat->attr), PTHREAD_MUTEX_RECURSIVE);
  int success = pthread_mutex_init(&(nat->lock), &(nat->attr));

  /* Initialize timeout thread */

  pthread_attr_init(&(nat->thread_attr));
  pthread_attr_setdetachstate(&(nat->thread_attr), PTHREAD_CREATE_JOINABLE);
  pthread_attr_setscope(&(nat->thread_attr), PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setscope(&(nat->thread_attr), PTHREAD_SCOPE_SYSTEM);
  pthread_create(&(nat->thread), &(nat->thread_attr), sr_nat_timeout, nat);

  /* CAREFUL MODIFYING CODE ABOVE THIS LINE! */
  nat->unsol_pkt = NULL;
  nat->mappings = NULL;
  nat->id = ID_MIN;
  nat->port = PORT_MIN;

  return success;
}


int sr_nat_destroy(struct sr_nat *nat) {  /* Destroys the nat (free memory) */

  pthread_mutex_lock(&(nat->lock));

  /* free nat memory here */
  struct sr_nat_mapping* mapping = nat->mappings;
  while (mapping) {
    struct sr_nat_mapping *nextm = mapping->next;
    struct sr_nat_connection *conn = mapping->conns;
    while (conn) {
      struct sr_nat_connection *nextc = conn->next;
      free(conn);
      conn = nextc;
    }
    free(mapping);
    mapping = nextm;
  }  

  struct sr_unsolicited_packet* pkt = nat->unsol_pkt;
  while (pkt) {
    struct sr_unsolicited_packet* nextp = pkt->next;
    free(pkt);
    pkt = nextp;
  }

  pthread_kill(nat->thread, SIGKILL);
  return pthread_mutex_destroy(&(nat->lock)) &&
    pthread_mutexattr_destroy(&(nat->attr));

}

/* unsolicited SYN timeout handling */
void del_timeout_unsol(struct sr_nat *nat) {
  time_t curtime = time(NULL);

  struct sr_unsolicited_packet *iter = NULL;
  struct sr_unsolicited_packet *prev = NULL;
  struct sr_unsolicited_packet *next = NULL;
  for (iter = nat->unsol_pkt; iter != NULL; iter = iter->next) 
  {
    if (difftime(curtime, iter->last_updated) > UNSOLICITED_TIMEOUT) {
      if (prev) {
        next = iter->next;
        prev->next = next;
      } else {
        next = iter->next;
        nat->unsol_pkt = next;
      }
      send_icmp(nat->sr, iter->packet, iter->len, iter->interface, DESTINATION_UNREACHABLE, DESTINATION_PORT_UNREACHABLE);
      continue;
    }
    prev = iter;
  }
}

/* connection timeout handling */
void del_timeout_conn(struct sr_nat *nat, struct sr_nat_mapping *mapping) {
  time_t curtime = time(NULL);
  struct sr_nat_connection *conn = NULL;
  struct sr_nat_connection *prev = NULL;
  struct sr_nat_connection *next = NULL;
  for (conn = mapping->conns; conn != NULL; conn = conn->next) {
    /* 
       if connecion is established, use tcpEstTimeout. 
       if connecion is not established, use tcpTransTimeout.
    */
    if (((conn->src_state.state == established || conn->dst_state.state == established) && 
        difftime(curtime, mapping->last_updated) > nat->tcpEstTimeout) ||
        (!(conn->src_state.state == established || conn->dst_state.state == established) && 
        difftime(curtime, mapping->last_updated) > nat->tcpTransTimeout)) {
      if (prev) {
        next = conn->next;
        prev->next = next;
      } else {
        next = conn->next;
        mapping->conns = next;
      }
      continue;
    }
    prev = conn;
  } 
}

/* Periodic Timout handling */
void *sr_nat_timeout(void *nat_ptr) {  
  struct sr_nat *nat = (struct sr_nat *)nat_ptr;
  while (1) {
    sleep(1.0);
    pthread_mutex_lock(&(nat->lock));
    del_timeout_unsol(nat);
    time_t curtime = time(NULL);
    struct sr_nat_mapping *mapping = NULL;
    struct sr_nat_mapping *prev = NULL;
    struct sr_nat_mapping *next = NULL;
    int del_mapping_flag = 0; /* flag indicates whether to delete current mapping */

    /* loop through current mappings */
    for (mapping = nat->mappings; mapping != NULL; mapping = mapping->next) {
      del_mapping_flag = 0;
      if (mapping->type == nat_mapping_tcp) {
        del_timeout_conn(nat, mapping);
        if (mapping->conns==NULL) {
          del_mapping_flag = 1;
        }
      } else if (mapping->type == nat_mapping_icmp){
        if (difftime(curtime, mapping->last_updated) > nat->icmpQueryTimeout) {
          del_mapping_flag = 1;
        }
      }
      if (del_mapping_flag) {        
        if (prev) {
          next = mapping->next;
          prev->next = next;
        } else {
          next = mapping->next;
          nat->mappings = next;
        }
        free(mapping);
        continue;
      }
      prev = mapping; 
    }
    pthread_mutex_unlock(&(nat->lock));
  }
  return NULL;
}

/* calculate TCP checksum*/
uint16_t tcp_cksum(sr_ip_hdr_t *ip_header, sr_tcp_hdr_t *tcp_header, int len)
{
  uint16_t result = 0;
  int tcpLen = len - sizeof(sr_ip_hdr_t) - sizeof(sr_ethernet_hdr_t);
  uint8_t *buf = (uint8_t *)malloc(sizeof(sr_tcp_pseudo_hdr_t) + tcpLen);
  sr_tcp_pseudo_hdr_t *ph = (sr_tcp_pseudo_hdr_t *)buf;
  ph->src_ip = ip_header->ip_src;
  ph->dst_ip = ip_header->ip_dst;
  ph->reserved = 0;
  ph->protocol = ip_header->ip_p;
  ph->length = htons(tcpLen);
  memcpy(buf + sizeof(sr_tcp_pseudo_hdr_t), tcp_header, tcpLen);
  result = cksum(buf, sizeof(sr_tcp_pseudo_hdr_t) + tcpLen);
  free(buf);
  return result;
}

/* delete unsolicited SYN in the queue */
void del_unsolicited_syn(struct sr_nat *nat, uint16_t port) {
  pthread_mutex_lock(&(nat->lock));
  
  struct sr_unsolicited_packet* iter = NULL;
  struct sr_unsolicited_packet* prev = NULL;
  struct sr_unsolicited_packet* next = NULL;
  for(iter = nat->unsol_pkt; iter != NULL; iter = iter->next) {
    sr_tcp_hdr_t *tcp_header = (sr_tcp_hdr_t *)(iter->ip_header + 1);
    if (tcp_header->dst_port == port) {
      if (prev) {
        next = iter->next;
        prev->next = next;
      } else {
        next = iter->next;
        nat->unsol_pkt = next;
      }
      continue;
    }
    prev = iter;
  }

  pthread_mutex_unlock(&(nat->lock));
}

/* update mapping tcp connection state with incoming packet */
int connection_update(struct sr_nat *nat, struct sr_nat_mapping* mapping, struct sr_nat_connection *conn) {
  
  pthread_mutex_lock(&(nat->lock));
  
  struct sr_nat_connection* iter = mapping->conns;
  while(iter) {
    if (iter->src_ip == conn->src_ip &&
        iter->dst_ip == conn->dst_ip &&
        iter->src_port == conn->src_port &&
        iter->dst_port == conn->dst_port) {
      
      conn->last_updated = time(NULL);
      /* ack packet */
      if (conn->src_state.seqno > iter->src_state.seqno) {
        iter->src_state.seqno = conn->src_state.seqno;
      }

      if ((conn->flags & ACK_BIT) == ACK_BIT) {
        /*syn_received -> established */
        if(iter->dst_state.state == syn_received &&
          conn->src_state.ackno - iter->dst_state.seqno == 1) { 
          iter->dst_state.state = established;
        }
        /* fin1 -> fin2*/
        if(iter->dst_state.state == fin_wait1 &&
          conn->src_state.ackno - iter->dst_state.seqno >= 1) {
          iter->dst_state.state = fin_wait2;
        } 
        if(conn->src_state.ackno > iter->src_state.ackno) {
          iter->src_state.ackno = conn->src_state.ackno;
        } 
      } else if ((conn->flags & FIN_BIT) == FIN_BIT) {
        /* fin packet */
        if(iter->dst_state.state == fin_wait2) {
          iter->dst_state.state = closed;
        } else {
          iter->src_state.state = fin_wait1;
        }
      } else if ((conn->flags & SYN_BIT) == SYN_BIT) {
        /* syn packet */
        if(iter->dst_state.state == syn_sent) {
          iter->dst_state.state = syn_received;
        }
      }
      
      return 0;
    }
    iter = iter->next;
  }
  return -1;
  pthread_mutex_unlock(&(nat->lock));
} 

/* Get the mapping associated with given external port.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_lookup_external(struct sr_nat *nat,
    uint16_t aux_ext, sr_nat_mapping_type type, struct sr_nat_connection* conn ) {

  pthread_mutex_lock(&(nat->lock));

  /* handle lookup here, malloc and assign to copy */
  struct sr_nat_mapping *copy = NULL;
  struct sr_nat_mapping *cur_mapping = nat->mappings;
  while (cur_mapping) {
    if (cur_mapping->aux_ext == aux_ext && cur_mapping->type == type) {
      break;
    } 
    cur_mapping = cur_mapping->next; 
  }  
  
  if(cur_mapping) {
    cur_mapping->last_updated = time(NULL);
    copy = (struct sr_nat_mapping*)malloc(sizeof(struct sr_nat_mapping)); 
    memcpy(copy, cur_mapping, sizeof(struct sr_nat_mapping));
  }
  pthread_mutex_unlock(&(nat->lock));
  return copy;
}

/* Get the mapping associated with given internal (ip, port) pair.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_lookup_internal(struct sr_nat *nat,
  uint32_t ip_int, uint16_t aux_int, sr_nat_mapping_type type, struct sr_nat_connection* conn) {

  pthread_mutex_lock(&(nat->lock));

  /* handle lookup here, malloc and assign to copy. */
  struct sr_nat_mapping *copy = NULL;
  struct sr_nat_mapping *cur_mapping = nat->mappings;
  while (cur_mapping) {
    if (cur_mapping->aux_int == aux_int && cur_mapping->ip_int == ip_int && cur_mapping->type == type) {
      break;
    } 
    cur_mapping = cur_mapping->next; 
  }
  
  if (cur_mapping) {
    if (type == nat_mapping_tcp) {
      /* if there is no matched connection, insert a new connection */
      if (connection_update(nat, cur_mapping, conn) == -1) {
        struct sr_nat_connection *newConn = (struct sr_nat_connection *)malloc(sizeof(struct sr_nat_connection));
        memcpy(newConn, conn, sizeof(struct sr_nat_connection));
        newConn->last_updated = time(NULL);

        /* check syn */
        if ((conn->flags & SYN_BIT) == SYN_BIT) {
          newConn->src_state.seqno = conn->src_state.seqno;
          newConn->src_state.state = syn_sent;
        } else {
          newConn->src_state.seqno = 0;
          newConn->src_state.state = closed;
        }
        newConn->dst_state.state = closed; 
        newConn->next = cur_mapping->conns;
        cur_mapping->conns = newConn;
      }
    }   
    cur_mapping->last_updated = time(NULL);
    
    /* copy to return */
    copy = (struct sr_nat_mapping*)malloc(sizeof(struct sr_nat_mapping));  
    memcpy(copy, cur_mapping, sizeof(struct sr_nat_mapping));

    if (cur_mapping->type == nat_mapping_tcp) {
      copy->conns = (struct sr_nat_connection *)malloc(sizeof(struct sr_nat_connection));
      memcpy(copy->conns, cur_mapping->conns, sizeof(struct sr_nat_connection));
    } 
  } 

  pthread_mutex_unlock(&(nat->lock));
  return copy;
}

/* Insert a new mapping into the nat's mapping table.
   Actually returns a copy to the new mapping, for thread safety.
 */
struct sr_nat_mapping *sr_nat_insert_mapping(struct sr_nat *nat,
  uint32_t ip_int, uint16_t aux_int, uint32_t ip_ext, sr_nat_mapping_type type,
  struct sr_nat_connection* conn) {

  pthread_mutex_lock(&(nat->lock));

  /* handle insert here, create a mapping, and then return a copy of it */
  struct sr_nat_mapping *mapping = NULL;
  /* double check there is no mapping exist*/
  struct sr_nat_mapping *cur_mapping = nat->mappings;
  while (cur_mapping) {
    if (cur_mapping->aux_int == aux_int && cur_mapping->ip_int == ip_int && cur_mapping->type == type) {
      mapping = (struct sr_nat_mapping*)malloc(sizeof(struct sr_nat_mapping));
      memcpy(mapping, cur_mapping, sizeof(struct sr_nat_mapping));
      return mapping;
    } 
    cur_mapping = cur_mapping->next; 
  } 

  /* if mapping doesn't exist */
  struct sr_nat_mapping* new_mapping = (struct sr_nat_mapping*)malloc(sizeof(struct sr_nat_mapping));
  new_mapping->ip_int = ip_int;
  new_mapping->aux_int = aux_int;
  new_mapping->ip_ext = ip_ext;

  /* if it's a icmp packet */
  if (type == nat_mapping_icmp) {
    new_mapping->aux_ext = nat->id;
    nat->id++;
  } else if (type == nat_mapping_tcp) {
    /* if it's a tcp packet */
    new_mapping->aux_ext = htons(nat->port);
    nat->port++;

    /* create a connection list */ 
    struct sr_nat_connection* newConn = (struct sr_nat_connection*)malloc(sizeof(struct sr_nat_connection));
    memcpy(newConn, conn, sizeof(struct sr_nat_connection));
    newConn->last_updated = time(NULL);
    if ((conn->flags & SYN_BIT) == SYN_BIT) {
      newConn->src_state.seqno = conn->src_state.seqno;
      newConn->src_state.state = syn_sent;
    } else {
      newConn->src_state.seqno = 0;
      newConn->src_state.state = closed;
    }
    newConn->dst_state.state = closed; 
    newConn->next = NULL;
    new_mapping->conns = newConn;  
  }

  /* insert mapping to the mapping table */
  new_mapping->last_updated = time(NULL);
  new_mapping->type = type;
  new_mapping->next = nat->mappings;
  nat->mappings = new_mapping;
  
  /* copy to return */
  mapping = (struct sr_nat_mapping*)malloc(sizeof(struct sr_nat_mapping));
  memcpy(mapping, new_mapping, sizeof(struct sr_nat_mapping));
  if (type == nat_mapping_tcp) {
    mapping->conns = (struct sr_nat_connection *)malloc(sizeof(struct sr_nat_connection));
    memcpy(mapping->conns, new_mapping->conns, sizeof(struct sr_nat_connection));
  }

  pthread_mutex_unlock(&(nat->lock));
  return mapping;
}

/* Check the direction of packet, inbound or outbound */
int check_bound(struct sr_rt* rt, char* interface) {
  if (memcmp(interface, "eth1", 4) == 0 && memcmp(rt->interface, "eth2", 4)==0) {
    /* outbound */
    return 1; 
  } else if (memcmp(interface, "eth2", 4) == 0 && memcmp(rt->interface, "eth1", 4)==0) {
    /* inbound */
    return 0;
  } else {
    return -1;  
  }
}

int translate_icmp(struct sr_instance* sr,                                                 
        uint8_t * packet/*len*/,                                                  
        unsigned int len,           
        char* interface/*len*/) {
  sr_ethernet_hdr_t* eth_header = (sr_ethernet_hdr_t*)packet;
  sr_ip_hdr_t* ip_header = (sr_ip_hdr_t*)(eth_header+1); 
  sr_icmp_hdr_t* icmp_header = (sr_icmp_hdr_t*)(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
  
  uint8_t type = icmp_header->icmp_type;
  /* If packet is echo request or reply, do the translation. If not, do nothing.*/
  if (type == 0 || type==8) {
    int outbound = 0;
    struct sr_rt* routing_index = check_rtable(sr, ntohl(ip_header->ip_dst));  
    if (routing_index) {
      outbound = check_bound(routing_index, interface);
    } else {
      return -1;
    }
    
    uint16_t* id = (uint16_t*)(packet+sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t)); 
    struct sr_nat_mapping* nat_mapping = NULL;

    if (outbound == 1) {
      /* Outbound packet, look up with src ip and src port*/
      nat_mapping = sr_nat_lookup_internal(sr->nat, ip_header->ip_src, *id, nat_mapping_icmp, NULL);  

      /* If mapping is not found, insert a new mapping to the mapping table */
      if (nat_mapping == NULL) {
        struct sr_if* eth2_if = sr_get_interface(sr, "eth2"); 
        nat_mapping = sr_nat_insert_mapping(sr->nat, ip_header->ip_src, *id, eth2_if->ip, nat_mapping_icmp, NULL);
      }
      
      /* update packet headers */
      struct sr_if* out_if = sr_get_interface(sr, routing_index->interface); 
      memcpy(eth_header->ether_shost, out_if->addr, ETHER_ADDR_LEN);  
   
      ip_header->ip_src = nat_mapping->ip_ext;
      *id = nat_mapping->aux_ext;

      ip_header->ip_sum = 0;
      ip_header->ip_sum = cksum(ip_header, sizeof(sr_ip_hdr_t));
      icmp_header->icmp_sum = 0; 
      icmp_header->icmp_sum = cksum(icmp_header, len - sizeof(sr_ethernet_hdr_t) - sizeof(sr_ip_hdr_t));

    } else if (outbound == 0) {
      /* inbound packet, look up with dest port */
      nat_mapping = sr_nat_lookup_external(sr->nat, *id, nat_mapping_icmp, NULL);
      
      /* mapping not found, do nothing */
      if (!nat_mapping) {
        return -1;
      }
      
      /* update packet headers */
      memset(eth_header->ether_dhost, 0, ETHER_ADDR_LEN);
      ip_header->ip_dst = nat_mapping->ip_int;
      *id = nat_mapping->aux_int;
      
      ip_header->ip_sum = 0;
      ip_header->ip_sum = cksum(ip_header, sizeof(sr_ip_hdr_t));
      icmp_header->icmp_sum = 0;
      icmp_header->icmp_sum = cksum(icmp_header, len - sizeof(sr_ethernet_hdr_t) - sizeof(sr_ip_hdr_t)); 
    }
    free(nat_mapping); 
  }
  return 0;
}

int translate_tcp (struct sr_instance* sr,
        uint8_t * packet,
        unsigned int len,
        char* interface) {
  sr_ethernet_hdr_t* eth_header = (sr_ethernet_hdr_t*)packet;
  sr_ip_hdr_t* ip_header = (sr_ip_hdr_t*)(eth_header+1); 
  sr_tcp_hdr_t* tcp_header = (sr_tcp_hdr_t*)(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
  
  int outbound = 0;
  struct sr_rt* routing_index = check_rtable(sr, ntohl(ip_header->ip_dst));  
  if (routing_index) {
    outbound = check_bound(routing_index, interface);  
  } else { 
    return -1;
  }
  struct sr_nat_mapping* nat_mapping = NULL; 
  if (outbound ==1) {
    /*outbound packet, look up with src ip and src port*/  
    struct sr_nat_connection* conn = (struct sr_nat_connection*)malloc(sizeof(struct sr_nat_connection));
    conn->src_ip = ip_header->ip_src;
    conn->dst_ip = ip_header->ip_dst; 
    conn->src_port = tcp_header->src_port;
    conn->dst_port = tcp_header->dst_port;
    conn->flags = tcp_header->flags;
    conn->src_state.seqno = tcp_header->seqno;
    conn->src_state.ackno = tcp_header->ackno;
    
    nat_mapping = sr_nat_lookup_internal(sr->nat, ip_header->ip_src, 
      tcp_header->src_port, nat_mapping_tcp, conn);    
     
    if (nat_mapping == NULL) {
      struct sr_if* eth2_if = sr_get_interface(sr, "eth2");
      nat_mapping = sr_nat_insert_mapping(sr->nat, ip_header->ip_src, 
        tcp_header->src_port, eth2_if->ip, nat_mapping_tcp, conn);  
    }
    /* handle solicite packet queue */
    if ((tcp_header->flags & SYN_BIT) == SYN_BIT) {
      del_unsolicited_syn(sr->nat, nat_mapping->aux_ext);     
    }
  
    /* update headers */
    struct sr_if* out_if = sr_get_interface(sr, routing_index->interface);
    memcpy(eth_header->ether_shost, out_if->addr, ETHER_ADDR_LEN); 
    
    ip_header->ip_src = nat_mapping->ip_ext;
    tcp_header->src_port = nat_mapping->aux_ext;
    
    ip_header->ip_sum = 0;
    ip_header->ip_sum = cksum(ip_header, sizeof(sr_ip_hdr_t));  
    tcp_header->tcp_sum = 0;
    tcp_header->tcp_sum = tcp_cksum(ip_header, tcp_header, len); 
    
  } else if (outbound == 0) {
     /*inbound packet, look up with dest port */
    nat_mapping = sr_nat_lookup_external(sr->nat, tcp_header->dst_port, nat_mapping_tcp, NULL); 
    if (nat_mapping ==NULL) {
      /*handle unsolicited syn*/
      struct sr_unsolicited_packet* newPkt = (struct sr_unsolicited_packet *)malloc(sizeof(struct sr_unsolicited_packet));
      newPkt->last_updated = time(NULL);
      newPkt->packet = packet;
      newPkt->len = len;
      newPkt->interface = interface;
      newPkt->ip_header = (sr_ip_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));;

      pthread_mutex_lock(&(sr->nat->lock));
      newPkt->next = sr->nat->unsol_pkt;
      sr->nat->unsol_pkt = newPkt;
      pthread_mutex_unlock(&(sr->nat->lock));
      return -1;
    }

    /* update headers */
    memset(eth_header->ether_dhost, 0, ETHER_ADDR_LEN);
    ip_header->ip_dst = nat_mapping->ip_int;
    tcp_header->dst_port = nat_mapping->aux_int;

    ip_header->ip_sum = 0;
    ip_header->ip_sum = cksum(ip_header, sizeof(sr_ip_hdr_t));
    tcp_header->tcp_sum = 0;
    tcp_header->tcp_sum = tcp_cksum(ip_header, tcp_header, len); 
 
    /* create a new conn */
    struct sr_nat_connection* conn = (struct sr_nat_connection*)malloc(sizeof(struct sr_nat_connection));
    /* for inbound packet, switch src and dst when generate new connection*/
    conn->dst_ip = ip_header->ip_src;
    conn->dst_port = tcp_header->src_port;
    conn->src_ip = ip_header->ip_dst;
    conn->src_port = tcp_header->dst_port;
    conn->flags = tcp_header->flags;
    conn->src_state.seqno = tcp_header->seqno;
    conn->src_state.ackno = tcp_header->ackno; 
    /* update connection state */
    connection_update(sr->nat, nat_mapping, conn);      
  }
  return 0;
}

int translate_packet(struct sr_instance* sr,
        uint8_t * packet,
        unsigned int len,
        char* interface) {
  sr_ethernet_hdr_t* eth_header = (sr_ethernet_hdr_t*)packet;
  sr_ip_hdr_t* ip_header = (sr_ip_hdr_t*)(eth_header+1);    
  /* icmp packet */
  if (ip_header->ip_p == ip_protocol_icmp) {
    return translate_icmp(sr, packet, len, interface); 
  } else {
  /* TCP/UDP packet*/
    return translate_tcp(sr, packet, len, interface);
  }
}

