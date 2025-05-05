#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Selective Repeat protocol.  Adapted from J.F.Kurose and GBN implementation
   
   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for SR), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications:
   - Converted from Go-Back-N to Selective Repeat
   - Individual packet timers instead of a single timer for first unacked packet
   - Receiver buffers out-of-order packets
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet
                          MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE 12     /* the sequence space for SR must be at least 2 * windowsize */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* Function prototypes to prevent nested function warnings */
void A_timerinterrupt(void);
void A_init(void);
void B_input(struct pkt packet);
void B_init(void);
void B_output(struct msg message);
void B_timerinterrupt(void);

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ )
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}

/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE];  /* array for storing packets waiting for ACK */
static int timer_active[WINDOWSIZE];   /* array for tracking which timers are active */
static int windowfirst, windowlast;    /* array indexes of the first/last packet awaiting ACK */
static int windowcount;                /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;               /* the next sequence number to be used by the sender */

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  /* if not blocked waiting on ACK */
  if (windowcount < WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i=0; i<20; i++)
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    /* put packet in window buffer */
    windowlast = (windowlast + 1) % WINDOWSIZE;
    buffer[windowlast] = sendpkt;
    windowcount++;

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3(A, sendpkt);
    
    /* start timer for this packet */
    timer_active[windowlast] = 1;
    starttimer(A, RTT);

    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
  }
  /* if blocked, window is full */
  else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}

/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
  int i, position = -1;
  
  /* if received ACK is not corrupted */
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    /* find the position of the ACKed packet in the window */
    for (i = 0; i < windowcount; i++) {
      int idx = (windowfirst + i) % WINDOWSIZE;
      if (buffer[idx].seqnum == packet.acknum) {
        position = idx;
        break;
      }
    }

    /* if we found the packet in our window */
    if (position != -1) {
      /* this is a new ACK */
      if (TRACE > 0)
        printf("----A: ACK %d is not a duplicate\n", packet.acknum);
      new_ACKs++;

      /* stop timer for this packet */
      if (timer_active[position]) {
        stoptimer(A);
        timer_active[position] = 0;
      }

      /* mark this packet as acknowledged by setting it to NOTINUSE */
      buffer[position].seqnum = NOTINUSE;

      /* if this was the first packet in the window, slide window forward */
      if (position == windowfirst) {
        while (windowcount > 0 && buffer[windowfirst].seqnum == NOTINUSE) {
          windowfirst = (windowfirst + 1) % WINDOWSIZE;
          windowcount--;
        }
      }

      /* restart timer for the next unacknowledged packet if there are any */
      if (windowcount > 0) {
        /* Find the next packet that needs a timer */
        for (i = 0; i < windowcount; i++) {
          int idx = (windowfirst + i) % WINDOWSIZE;
          if (buffer[idx].seqnum != NOTINUSE && !timer_active[idx]) {
            timer_active[idx] = 1;
            starttimer(A, RTT);
            break;
          }
        }
      }
    }
    else {
      /* ACK for packet not in window - probably a duplicate */
      if (TRACE > 0)
        printf("----A: duplicate ACK received, do nothing!\n");
    }
  }
  else {
    if (TRACE > 0)
      printf("----A: corrupted ACK is received, do nothing!\n");
  }
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
  int i;

  if (TRACE > 0)
    printf("----A: time out, resending packets!\n");

  /* Resend the first unacknowledged packet */
  if (windowcount > 0 && buffer[windowfirst].seqnum != NOTINUSE) {
    if (TRACE > 0)
      printf("---A: resending packet %d\n", buffer[windowfirst].seqnum);
    
    tolayer3(A, buffer[windowfirst]);
    packets_resent++;
    
    /* Restart timer for this packet */
    timer_active[windowfirst] = 1;
    starttimer(A, RTT);
  }
}

/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  int i;
  
  /* initialise A's window, buffer and sequence number */
  A_nextseqnum = 0;  /* A starts with seq num 0, do not change this */
  windowfirst = 0;
  windowlast = -1;   /* windowlast is where the last packet sent is stored.
                     new packets are placed in winlast + 1
                     so initially this is set to -1
                   */
  windowcount = 0;
  
  /* Initialize timer status array */
  for (i = 0; i < WINDOWSIZE; i++) {
    timer_active[i] = 0;
    buffer[i].seqnum = NOTINUSE;
  }
}

/********* Receiver (B) variables and procedures ************/

static int expectedseqnum;           /* the sequence number expected next by the receiver */
static int B_nextseqnum;             /* the sequence number for the next packets sent by B */
static int rcv_base;                 /* base of the receiver window */
static int B_buffer_valid[SEQSPACE]; /* tracks which packets have been received */
static struct pkt B_buffer[SEQSPACE]; /* buffer for out-of-order packets */

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;
  
  /* if not corrupted */
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);

    /* check if packet is within the receive window */
    int distance = (packet.seqnum - rcv_base + SEQSPACE) % SEQSPACE;
    if (distance < WINDOWSIZE) {
      /* packet is within receive window, buffer it */
      B_buffer[packet.seqnum] = packet;
      B_buffer_valid[packet.seqnum] = 1;
      
      /* if this is the expected packet, deliver it and any consecutive buffered packets */
      if (packet.seqnum == expectedseqnum) {
        /* deliver this packet and any consecutive buffered packets */
        while (B_buffer_valid[expectedseqnum]) {
          tolayer5(B, B_buffer[expectedseqnum].payload);
          packets_received++;
          
          /* mark this packet as delivered */
          B_buffer_valid[expectedseqnum] = 0;
          
          /* advance expected sequence number */
          expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
          rcv_base = expectedseqnum;
        }
      }
    }
    
    /* send ACK for this packet */
    sendpkt.acknum = packet.seqnum;
    sendpkt.seqnum = B_nextseqnum;
    B_nextseqnum = (B_nextseqnum + 1) % 2;

    /* we don't have any data to send. fill payload with 0's */
    for (i = 0; i < 20; i++)
      sendpkt.payload[i] = '0';
      
    /* compute checksum */
    sendpkt.checksum = ComputeChecksum(sendpkt);
      
    /* send ACK */
    tolayer3(B, sendpkt);
  }
  else {
    /* packet is corrupted, do nothing */
    if (TRACE > 0)
      printf("----B: packet corrupted, do nothing!\n");
  }
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  int i;
  
  expectedseqnum = 0;
  B_nextseqnum = 1;
  rcv_base = 0;
  
  /* initialize receiver buffer */
  for (i = 0; i < SEQSPACE; i++) {
    B_buffer_valid[i] = 0;
  }
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
  /* Not needed for this assignment, but implementation is required to avoid warnings */
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
  /* Not needed for this assignment, but implementation is required to avoid warnings */
}