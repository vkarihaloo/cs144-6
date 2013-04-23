
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>

#include "rlib.h"
#include <stdbool.h>

#define READ_SIZE 500

bool can_destroy (rel_t *); 

struct reliable_state {
  rel_t *next;			/* Linked list for traversing all connections */
  rel_t **prev;

  conn_t *c;			/* This is the connection object */

  /* Add your own data fields below this */
  bool is_my_input_eof; // whether myself is eof
  bool is_yr_input_eof; // whether other side is eof
  bool is_all_acked; // whether all packets have been acked
  bool is_all_output; // whether all data has been output
  bool eof_acked; // whether eof has been acked
  uint32_t seqno; // as sender, the current packet seqno will send
  uint32_t ackno; // as receiver, the expecting ackno will send
  uint16_t interval; // counter for rel_timer 
  uint16_t destroy_wait; // counter for two retransmisstion intervals before connection destroy
  packet_t outputPacket; // as sender
  packet_t inputPacket; // as receiver
};
rel_t *rel_list;





/* Creates a new reliable protocol session, returns NULL on failure.
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
rel_t *
rel_create (conn_t *c, const struct sockaddr_storage *ss,
	    const struct config_common *cc)
{
  rel_t *r;

  r = xmalloc (sizeof (*r));
  memset (r, 0, sizeof (*r));

  if (!c) {
    c = conn_create (r, ss);
    if (!c) {
      free (r);
      return NULL;
    }
  }

  r->c = c;
  r->next = rel_list;
  r->prev = &rel_list;
  if (rel_list)
    rel_list->prev = &r->next;
  rel_list = r;

  /* Do any other initialization you need here */
  r->is_my_input_eof = false;
  r->is_yr_input_eof = false;
  r->is_all_acked = false;
  r->is_all_output = false;
  r->eof_acked = false;
  r->outputPacket.seqno = htonl(0);
  r->seqno = 1;
  r->ackno = 1;
  r->interval = 0;
  r->destroy_wait = 0;

  return r;
}

void
rel_destroy (rel_t *r)
{
  if (r->next)
    r->next->prev = r->prev;
  *r->prev = r->next;
  conn_destroy (r->c);
  /* Free any other allocated memory here */
  free(r);
}


/* This function only gets called when the process is running as a
 * server and must handle connections from multiple clients.  You have
 * to look up the rel_t structure based on the address in the
 * sockaddr_storage passed in.  If this is a new connection (sequence
 * number 1), you will need to allocate a new conn_t using rel_create
 * ().  (Pass rel_create NULL for the conn_t, so it will know to
 * allocate a new connection.)
 */
void
rel_demux (const struct config_common *cc,
	   const struct sockaddr_storage *ss,
	   packet_t *pkt, size_t len)
{
}

void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
  if(n != (size_t)ntohs(pkt->len)){
    return;
  }
  uint16_t ori_cksum = pkt->cksum;
  pkt->cksum = 0;
  uint16_t cksum_num = cksum(pkt, ntohs(pkt->len));
  if (cksum_num != ori_cksum) {
    return;
  }
  // check cksum, len
  if (ntohs(pkt->len) == 8) {
    // sender side
    // receive ack 
    if ((r->seqno+1) == ntohl(pkt->ackno)) {
      if (r->is_my_input_eof) {
        // receive ack of eof
        r->eof_acked = true;
      }
      r->seqno++;
      r->is_all_acked = true;
      rel_read(r);
    }
  } else if(ntohs(pkt->len) >= 12) {
    // receiver side
    if (ntohs(pkt->len) == 12) {
      // receive EOF 
      r->is_yr_input_eof = true; 
    } 
    r->inputPacket.cksum = pkt->cksum;
    r->inputPacket.len = ntohs(pkt->len);
    r->inputPacket.seqno = ntohl(pkt->seqno);
    r->inputPacket.ackno = ntohl(pkt->ackno);
    memset(r->inputPacket.data, 0, READ_SIZE);
    memcpy(r->inputPacket.data, pkt->data, r->inputPacket.len - 12);
    rel_output(r); 
  }
}


void
rel_read (rel_t *s)
{
  if (s->seqno > ntohl(s->outputPacket.seqno) && !s->eof_acked) {
    int count = conn_input(s->c, s->outputPacket.data, READ_SIZE);    
    if (count == 0) {
      // no input
      return;
    }
    if (count == -1) {
      // EOF 
      count = 0;
      memset(s->outputPacket.data, 0, READ_SIZE);
      s->is_my_input_eof = true;
    } 
    // send packtet
    s->outputPacket.cksum = 0;
    s->outputPacket.len = htons(count + 12);
    s->outputPacket.ackno = htonl(1); 
    s->outputPacket.seqno = htonl(s->seqno);
    s->outputPacket.cksum = cksum(&s->outputPacket, count+12);
    conn_sendpkt(s->c, &s->outputPacket, count+12);
    s->is_all_acked = false;
  }
  
}

void
rel_output (rel_t *r)
{ 
  size_t left = conn_bufspace(r->c);
  // check output buffer space
  if ((uint16_t)left >= (r->inputPacket.len-12) && r->inputPacket.seqno == r->ackno) {
    if ((r->inputPacket.len-12) == 0) {
      // EOF, all has been output
      r->is_all_output = true; 
    }
    conn_output (r->c, r->inputPacket.data, r->inputPacket.len-12);
    r->ackno++;
  }
  //send ack
  packet_t ackPacket;
  ackPacket.cksum = 0;
  ackPacket.len = htons(8);
  ackPacket.ackno = htonl(r->ackno);
  ackPacket.cksum = cksum(&ackPacket, 8);
  conn_sendpkt(r->c, &ackPacket, 8); 
}

void
rel_timer ()
{
  /* Retransmit any packets that need to be retransmitted */
  rel_list->interval++;
  if (rel_list->interval >= 5) {
    // reach one retransmission interval
    if ( rel_list->seqno <= ntohl(rel_list->outputPacket.seqno) ) {
      // resend packet
      conn_sendpkt(rel_list->c, &(rel_list->outputPacket), ntohs(rel_list->outputPacket.len));
    } else {
      if (can_destroy(rel_list) || rel_list->destroy_wait>0) {
        // destroy connection
        rel_list->destroy_wait++; // have to wait two retransmission intervals
        if (rel_list->destroy_wait >= 2) {
          rel_destroy(rel_list);
        }
      }
    }
    rel_list->interval = 0;
  } 
}

bool
can_destroy (rel_t *r) {
  // judge retransmit condition
  if (r->is_my_input_eof && r->is_yr_input_eof && r->is_all_acked && r->is_all_output) {
    return true;
  }
  return false;
}
