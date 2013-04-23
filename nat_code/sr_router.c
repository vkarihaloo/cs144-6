/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_arpcache.h"
#include "sr_utils.h"
#include "sr_nat.h"
#include <stdbool.h>

#define MIN(A, B) (((A) < (B)) ? (A) : (B))

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr)
{
    /* REQUIRES */
    assert(sr);

    /* Initialize cache and cache cleanup thread */
    sr_arpcache_init(&(sr->cache));

    pthread_attr_init(&(sr->attr));
    pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_t thread;

    pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);
    
    /* Add initialization code here! */
} /* -- sr_init -- */

/* Perform sanity check */
bool check_sanity (sr_ip_hdr_t* ip_packet, unsigned int len) {
  
  /* check length */
  unsigned int pkt_len = ntohs(ip_packet->ip_len) + sizeof(sr_ethernet_hdr_t);
  if ( pkt_len != len) return false;
  /* checksum */
  uint16_t ori_cksum = ip_packet->ip_sum;
  ip_packet->ip_sum = 0;
  uint16_t cksum_num = cksum(ip_packet, ip_packet->ip_hl*4);
  ip_packet->ip_sum = ori_cksum;
  if (cksum_num != ori_cksum) return false;
  /* update ttl */
  ip_packet->ip_ttl--;
  
  /* recompute checksum */
  ip_packet->ip_sum = 0;  
  uint16_t new_cksum = cksum(ip_packet, ip_packet->ip_hl*4);
  ip_packet->ip_sum = new_cksum;

  return true;
}

/* check dest ip in routing table */
struct sr_rt* check_rtable(struct sr_instance* sr, uint32_t ip) {
  struct sr_rt* rt_pt = sr->routing_table;
  struct sr_rt* routing_index = NULL;
  bool is_matched = false;
  while (rt_pt) {
    if ( ntohl(rt_pt->dest.s_addr) == (ip & rt_pt->mask.s_addr)) {
      if (is_matched) {
        if (ntohl(rt_pt->mask.s_addr) > ntohl(routing_index->mask.s_addr)) {
          routing_index = rt_pt;
        }
      } else {
        is_matched = true;
        routing_index = rt_pt;
      }
    }
    rt_pt = rt_pt->next;
  } 
  return routing_index;
}

/* check to ME or not to ME */
char* should_process(struct sr_instance* sr, sr_ip_hdr_t * ip_packet) {
  struct sr_if* ifpt = sr->if_list;
  while(ifpt!=NULL) {
    if (ifpt->ip == ip_packet->ip_dst) {
      return ifpt->name;
    }
    ifpt = ifpt->next;
  }
  return NULL;
}

/* send ICMP */
void send_icmp(struct sr_instance* sr,
        uint8_t* packet, 
        unsigned int len, 
        char* interface,
        uint8_t type, 
        uint8_t code) {
 
  /* get ethernet header */
  sr_ethernet_hdr_t* eth_header = (sr_ethernet_hdr_t*)packet; 
  /* get ip header */
  sr_ip_hdr_t* ip_header = (sr_ip_hdr_t*)(eth_header+1); 
  /* get outgoing interface */
  struct sr_if* out_if = sr_get_interface(sr, interface); 
  
  uint8_t icmp_len;
  uint8_t payload_len;
  /* echo reply */
  if (type == 0) {
    icmp_len = ntohs(ip_header->ip_len) - sizeof(sr_ip_hdr_t );    
    payload_len = icmp_len - sizeof(sr_icmp_hdr_t) - 4;
  } else {
    icmp_len = sizeof(sr_icmp_t3_hdr_t);
    payload_len = ICMP_DATA_SIZE;
  }
  
  uint8_t total_len = sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t)+icmp_len;
  uint8_t* icmp_pkt = (uint8_t*)malloc(total_len); 
   
  /* generate ICMP header */  
  sr_icmp_t3_hdr_t* icmp_header = (sr_icmp_t3_hdr_t*)(icmp_pkt+sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t));
  
  memcpy(icmp_header, ip_header+1, icmp_len);
  icmp_header->icmp_type = type;
  icmp_header->icmp_code = code;    
  icmp_header->icmp_sum = 0;
  /* not echo reply */
  if (type!=0) {
    memcpy(icmp_header->data, ip_header, payload_len);
  }
  icmp_header->icmp_sum = cksum(icmp_header, sizeof(sr_icmp_t3_hdr_t)); 
  
  /* generate IP packet */
  sr_ip_hdr_t* new_ip_header = (sr_ip_hdr_t*)(icmp_pkt+sizeof(sr_ethernet_hdr_t));
  memcpy(new_ip_header, ip_header, sizeof(sr_ip_hdr_t));
  uint16_t pk_len = sizeof(sr_ip_hdr_t)+ icmp_len;
  new_ip_header->ip_len = htons(pk_len);
  new_ip_header->ip_src = out_if->ip;
  new_ip_header->ip_dst = ip_header->ip_src;
  new_ip_header->ip_ttl = 0x40;
  new_ip_header->ip_p = ip_protocol_icmp;
  
  new_ip_header->ip_sum = 0;
  new_ip_header->ip_sum = cksum(new_ip_header, sizeof(sr_ip_hdr_t));  

  /* generate Ethernet frame*/
  sr_ethernet_hdr_t* new_eth_header = (sr_ethernet_hdr_t*)icmp_pkt;
  new_eth_header->ether_type = htons(ethertype_ip);  
  
  /* send ICMP packet*/
  sr_ip_forward(sr, icmp_pkt, total_len, interface, true);
  /* free extra memory */
  free(icmp_pkt);
}

/* To me */
void sr_ip_process(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */) {
  sr_ethernet_hdr_t* eth_header = (sr_ethernet_hdr_t*)packet;
  sr_ip_hdr_t* ip_header = (sr_ip_hdr_t*)(eth_header+1);
  /* ICMP echo req*/
  if (ip_header->ip_p == ip_protocol_icmp) {
    sr_icmp_t3_hdr_t* icmp_header = (sr_icmp_t3_hdr_t*)(ip_header+1);
    uint8_t type = icmp_header->icmp_type;
    uint8_t code = icmp_header->icmp_code;
    if (type==8 && code==0) {
      /* send echo reply*/
      send_icmp(sr, packet, len, interface, 0, 0);      
    }  
  } else {
    /* TCP or UDP packet, send ICMP port unreachable */
    send_icmp(sr, packet, len, interface, 3, 3);
  } 
}

/* Not to me */
void sr_ip_forward(struct sr_instance* sr,
        uint8_t * packet,
        unsigned int len,
        char* interface/* lent */,
        bool ICMP) {
  sr_ethernet_hdr_t* eth_header = (sr_ethernet_hdr_t*)packet;  
  sr_ip_hdr_t* ip_header = (sr_ip_hdr_t*)(eth_header+1);  
  /* check dest IP in routing table */ 
  struct sr_rt* routing_index = check_rtable(sr, ntohl(ip_header->ip_dst));
  if (routing_index) {
    uint32_t next_hop_ip = ntohl(routing_index->gw.s_addr);
    char* next_hop_if = routing_index->interface;
    /* check ARP in cache */
    struct sr_arpentry *entry = sr_arpcache_lookup(&sr->cache, next_hop_ip);
    if (entry && entry->valid) {
      struct sr_if* out_if = sr_get_interface(sr, next_hop_if);
      /* update ethernet header */
      memcpy(eth_header->ether_shost, out_if->addr, ETHER_ADDR_LEN);
      memcpy(eth_header->ether_dhost, entry->mac, ETHER_ADDR_LEN); 
      /* send packet to next hop*/ 
      sr_send_packet(sr, packet, len, next_hop_if);
    } else {
      /* save packet in the request queue */
      struct sr_arpreq * req =  sr_arpcache_queuereq(&sr->cache, next_hop_ip, packet, len, next_hop_if);
      send_arp_request(sr, req);
    }
    free(entry);
  } else {
    /* ICMP destination unreachable*/
    if (!ICMP) {
      send_icmp(sr, packet, len, interface, 3, 0);
    }
  }
 
}

void handle_arp_reply (struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */) {
  sr_ethernet_hdr_t* eth_header = (sr_ethernet_hdr_t*)packet;
  sr_arp_hdr_t* arp_header = (sr_arp_hdr_t*)(eth_header+1);
  if (arp_header->ar_sha) {
    struct sr_arpreq* waiting_req = sr_arpcache_insert(&sr->cache, arp_header->ar_sha, ntohl(arp_header->ar_sip)); 
    if (waiting_req) {
      struct sr_packet* waiting_pkt = waiting_req->packets;
      while (waiting_pkt) {
        sr_ip_forward(sr, waiting_pkt->buf, waiting_pkt->len, waiting_pkt->iface, false); 
        waiting_pkt = waiting_pkt->next;
      }
      sr_arpreq_destroy(&sr->cache, waiting_req); 
    }
  }
}

bool check_my_if(struct sr_instance* sr, uint32_t req_ip) {
  struct sr_if* if_pt = sr->if_list;
  while (if_pt) {
    if (if_pt->ip == req_ip) return true;
    if_pt = if_pt->next;
  } 
  return false;
}

void send_arp_reply(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */) {
  sr_ethernet_hdr_t* eth_header = (sr_ethernet_hdr_t*)packet;
  sr_arp_hdr_t* arp_header = (sr_arp_hdr_t*)(eth_header+1);
  struct sr_if* if_pt = sr_get_interface(sr, interface);;
  /* generate arp reply header*/ 
  sr_arp_hdr_t* new_arp_header = (sr_arp_hdr_t*)malloc(sizeof(sr_arp_hdr_t));
  new_arp_header->ar_hrd = htons(arp_hrd_ethernet);  
  new_arp_header->ar_pro = htons(ethertype_ip); 
  new_arp_header->ar_hln = ETHER_ADDR_LEN; 
  new_arp_header->ar_pln = IPV4_HDR_LEN;
  
  new_arp_header->ar_op = htons(2);
  memcpy(new_arp_header->ar_sha, if_pt->addr, ETHER_ADDR_LEN);
  new_arp_header->ar_sip = arp_header->ar_tip;
  memcpy(new_arp_header->ar_tha, arp_header->ar_sha, ETHER_ADDR_LEN);
  new_arp_header->ar_tip = arp_header->ar_sip;
   
  /* generate ethernet frame*/
  sr_ethernet_hdr_t *new_eth_header = (sr_ethernet_hdr_t*)malloc(sizeof(sr_ethernet_hdr_t));
  memcpy(new_eth_header, eth_header, sizeof(sr_ethernet_hdr_t));
  memcpy(new_eth_header->ether_shost, if_pt->addr, ETHER_ADDR_LEN);
  memcpy(new_eth_header->ether_dhost, eth_header->ether_shost, ETHER_ADDR_LEN); 
  
  /* generate arp packet*/
  uint8_t* new_arp_packet = (uint8_t*)malloc(sizeof(sr_ethernet_hdr_t)+sizeof(sr_arp_hdr_t));
  memcpy(new_arp_packet, new_eth_header, sizeof(sr_ethernet_hdr_t));
  memcpy(new_arp_packet+sizeof(sr_ethernet_hdr_t), new_arp_header, sizeof(sr_arp_hdr_t)); 

  /* send arp reply */
  sr_send_packet(sr, new_arp_packet, sizeof(sr_arp_hdr_t)+sizeof(sr_ethernet_hdr_t), interface);
  free(new_arp_packet);
  free(new_arp_header);
  free(new_eth_header);
  return; 
}

/* IP packet */
void sr_handle_ip(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */) {
  sr_ip_hdr_t *iphdr = (sr_ip_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));
  if (!check_sanity(iphdr, len)) return;
  /* check ttl */
  if (iphdr->ip_ttl == 0) {
    send_icmp(sr, packet, len, interface, 11, 0);
    return;
  }

  if (sr->nat) {
    if (translate_packet(sr, packet, len, interface) == -1) {
      return;
    }
  }

  char* dest_if = NULL;
  if ((dest_if = should_process(sr, iphdr))) {
    sr_ip_process(sr, packet, len, dest_if); /*To me*/
  } else {
    sr_ip_forward(sr, packet, len, interface, false); /*Not to me */
  }

}
/* ARP packet */
void sr_handle_arp(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface) {
  sr_arp_hdr_t* arp_header = (sr_arp_hdr_t*)(packet+sizeof(sr_ethernet_hdr_t));
  if (ntohs(arp_header->ar_op) == 1) {
    /* arp request to me */ 
    send_arp_reply(sr, packet, len, interface);
  } else if (ntohs(arp_header->ar_op) == 2) {
    /* arp reply to me*/
    handle_arp_reply(sr, packet, len, interface);
  }
}

/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{
  /* REQUIRES */
  assert(sr);
  assert(packet);
  assert(interface);

  printf("*** -> Received packet of length %d \n",len);
  /* fill in code here */
  if (ethertype(packet) == ethertype_ip) {
    sr_handle_ip(sr, packet, len, interface);  
  } else if (ethertype(packet) == ethertype_arp) {
    sr_handle_arp(sr, packet, len, interface);
  }

}/* end sr_ForwardPacket */











