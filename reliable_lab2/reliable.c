
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

bool destroy_check (rel_t *); 

struct reliable_state {
  rel_t *next;			/* Linked list for traversing all connections */
  rel_t **prev;

  conn_t *c;			/* This is the connection object */

  /* Add your own data fields below this */
  struct sockaddr_storage *ss;

  bool SEND_EOF; // whether myself is eof
  bool RECV_EOF; // whether other side is eof
  bool EOF_ACKED; // whether eof has been acked
  uint16_t destroy_wait; // counter for two retransmisstion intervals before connection destroy

  packet_t * sendWindow; // input buffer
  packet_t * recvWindow; // output buffer
  int * sendState; // record send pkt state, 0 not acked, 1 acked
  int * recvState; // record recv pkt state, 0 not receive, 1 receive not output, 2 receive and output
  int * sendTimer; // timer for sent pkt
  int window_size; // window size
  
  /* send variable */
  int SWS; // Send window size
  int LAR; // Last acknowledgement received
  int LSS; // Last segment sent 

  /* recevier variable */
  int RWS; // Receive window size
  int LAS; // Last acceptable segment
  int LSR; // Last segment received 

};
rel_t *rel_list;


/* 
 * Helper functions 
 */

bool
destroy_check (rel_t *r) {
  return r->SEND_EOF && r->RECV_EOF;
}

bool 
check_valid (packet_t *pkt, size_t n) {
  //check length
  if (ntohs(pkt->len) != (uint16_t)n) return false;

  //check checksum
  uint16_t ori_cksum = pkt->cksum;
  pkt->cksum = 0;
  uint16_t cksum_num = cksum(pkt, ntohs(pkt->len));
  if (cksum_num != ori_cksum) return false;
  pkt->cksum = ori_cksum;

  return true;
}

int
send_ack (rel_t *r) {
  packet_t ackPacket;
  ackPacket.cksum = 0;
  ackPacket.len = htons(8);
  ackPacket.ackno = htonl(r->LSR+1);
  ackPacket.cksum = cksum(&ackPacket, 8);
  return conn_sendpkt(r->c, &ackPacket, 8);
}

bool 
send_check (rel_t *s) {
  //fprintf(stderr, "\n******************Send Check:LSS %d, LAR:%d !********************\n", s->LSS, s->LAR);
  return (s->LSS - s->LAR < s->SWS) && (!s->EOF_ACKED) ;
}

bool 
recv_check (rel_t *s, packet_t *pkt) {
  int seqno = ntohl(pkt->seqno);
  //fprintf(stderr, "\n******************Receive Check seqno:%d, LAS:%d !********************\n", seqno, s->LAS);
  return (seqno <= s->LAS);
}

int 
resend_pkt (rel_t *s, int index) {
  return conn_sendpkt(s->c, &(s->sendWindow[index]), ntohs(s->sendWindow[index].len));
}

bool 
check_pkt_acked (rel_t *r, int index) {
  //fprintf(stderr, "\n******************Resend Check seqno:%d, LAR:%d !********************\n", ntohl(r->sendWindow[index].seqno), r->LAR);
  return (ntohl(r->sendWindow[index].seqno) < r->LAR+1);
}

void
addPktToRWindow(rel_t *r, packet_t * pkt) {
  int index = ntohl(pkt->seqno) % r->window_size;
  r->recvState[index] = 1; // receive but not output

  r->recvWindow[index].len = pkt->len;
  r->recvWindow[index].cksum = pkt->cksum;
  r->recvWindow[index].ackno = pkt->ackno;
  r->recvWindow[index].seqno = pkt->seqno;
  memset(r->recvWindow[index].data, 0, READ_SIZE);
  memcpy(r->recvWindow[index].data, pkt->data, ntohs(pkt->len) - 12);
}

void
addPktToSWindow(rel_t *s, packet_t pkt) {
  int seqno = ntohl(pkt.seqno);
  int index = seqno % s->window_size;
  s->LSS = seqno;

  s->sendWindow[index].len = pkt.len;
  s->sendWindow[index].cksum = pkt.cksum;
  s->sendWindow[index].ackno = pkt.ackno;
  s->sendWindow[index].seqno = pkt.seqno;
  memset(s->sendWindow[index].data, 0, READ_SIZE);
  memcpy(s->sendWindow[index].data, pkt.data, ntohs(pkt.len) - 12);

  s->sendState[index] = 0; // not acked
  s->sendTimer[index] = 0;
}

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
  if (ss != NULL) {
    r->ss = xmalloc (sizeof (*r->ss));
    memcpy(r->ss, ss, sizeof (*r->ss));
  }

  r->SEND_EOF = false;
  r->RECV_EOF = false;
  r->EOF_ACKED = false;
  r->destroy_wait = 0;

  r->window_size = cc->window;
  r->SWS = cc->window;
  r->LAR = 0;
  r->LSS = 0;

  r->RWS = cc->window;
  r->LSR = 0;
  r->LAS = r->RWS + r->LSR;

  r->sendWindow = xmalloc(r->SWS * sizeof(packet_t));
  r->recvWindow = xmalloc(r->RWS * sizeof(packet_t));
  r->sendState = xmalloc(r->SWS * sizeof(int));
  r->recvState = xmalloc(r->RWS * sizeof(int));
  r->sendTimer = xmalloc(r->SWS * sizeof(int));
  int i = 0;
  for (i=0; i<r->SWS; i++) {
    r->sendState[i] = 1;
    r->sendTimer[i] = 0;
    r->recvState[i] = 0;
  }

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
  if (r->ss) {
    free(r->ss);
  }
  if (r->sendWindow) {
    free(r->sendWindow);
  }
  if (r->recvWindow) {
    free(r->recvWindow);
  }
  if (r->sendState) {
    free(r->sendState);
  }
  if (r->recvState) {
    free(r->recvState);
  }
  if (r->sendTimer) {
    free(r->sendTimer);
  }
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
  if (!check_valid(pkt, len)) return;

  rel_t * r = rel_list;
  
  while (r) {
    if (r->ss && addreq(ss, r->ss)) {
      break;
    }
    r = r->next; 
  }

  if (!r) {
    if (ntohl(pkt->seqno) == 1) {
      r = rel_create(NULL, ss, cc);
    } else {
      return;
    }
  }

  rel_recvpkt(r, pkt, len);
}

void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
  //fprintf(stderr, "\n******************receive!********************\n");
  if (!check_valid(pkt, n)) return;
  //fprintf(stderr, "\n******************pass check!********************\n");
  int i = 0;
  if (ntohs(pkt->len) == 8) {
    // sender side
    // receive ack 
    //fprintf(stderr, "\n******************Receive Ack ack:%d, LAR:%d!********************\n", ntohl(pkt->ackno), r->LAR);
    if (ntohl(pkt->ackno) > r->LAR) {
      for (i=r->LAR; i<ntohl(pkt->ackno); i++) {
        r->sendState[i% r->window_size] = 1; // got ack
      }
      r->LAR = ntohl(pkt->ackno) - 1;
      if (r->SEND_EOF) {
        // receive ack of eof
        r->EOF_ACKED = true;
      }
      rel_read(r);
    }
  } else if (ntohs(pkt->len) >= 12) {
    // receiver side
    if (recv_check(r, pkt)) {
      if (ntohl(pkt->seqno) == r->LSR+1) {
        // right seqno, output buffer and change LSR, LAS and recvState
        if (ntohs(pkt->len) == 12) {
          // receive EOF 
          r->RECV_EOF = true; 
        } 
        addPktToRWindow(r, pkt); // add to buffer first 
        rel_output(r);
      } else if (ntohl(pkt->seqno) <= r->LSR) {
        // old seqno, send current ack
        send_ack(r);
      } else {
        addPktToRWindow(r, pkt); // add to buffer
      }
    } else {
      send_ack(r);
    }
  }
}


void
rel_read (rel_t *s)
{
  if (send_check(s)) {
    packet_t out_pkt;
    int count = conn_input(s->c, out_pkt.data, READ_SIZE);    
    if (count == 0) {
      // no input
      return;
    } else {
      if (count == -1) {
        // EOF 
        count = 0;
        s->SEND_EOF = true;
      } 
      // send 
      out_pkt.cksum = 0;
      out_pkt.len = htons(count + 12);
      out_pkt.ackno = htonl(1); 
      out_pkt.seqno = htonl(s->LSS+1);
      out_pkt.cksum = cksum(&out_pkt, count+12);
      conn_sendpkt(s->c, &out_pkt, count+12);
      addPktToSWindow(s, out_pkt); 
    }
  }
  
}

void
rel_output (rel_t *r)
{ 
  size_t left = conn_bufspace(r->c);
  int index = (r->LSR + 1) % r->window_size;
  packet_t * cur = &r->recvWindow[index];
  int data_len = ntohs(cur->len) - 12;

  while ((uint16_t)left >= data_len) { 
    conn_output (r->c, cur->data, data_len);
    r->recvState[index] = 2;
    r->LSR ++ ;
    r->LAS = r->LSR + r->RWS;
    index = (r->LSR + 1) % r->window_size;
    if (r->recvState[index] == 1) {
      cur = &r->recvWindow[index];
    } else {
      break;
    }
    data_len = ntohs(cur->len) - 12;
    left = conn_bufspace(r->c);
  }
  send_ack(r);
}

void
rel_timer ()
{
  /* Retransmit any packets that need to be retransmitted */
  rel_t *r = rel_list;
  int i = 0;
  while (r != NULL) {
    rel_t *next = r->next;
    if (destroy_check(r) || r->destroy_wait>0) {
      r->destroy_wait++;
      if (r->destroy_wait > 2) {
        rel_destroy(r);
        continue;
      }
    } else {
      for (i=0; i < r->window_size; i++) {
        if (r->sendTimer[i] >= 5) {
          if (!check_pkt_acked(r, i)) {
            resend_pkt(r, i);
          }
          r->sendTimer[i] = 0;
        } else {
          r->sendTimer[i]++;
        }
      }
    }
    r = next;
  }
}

