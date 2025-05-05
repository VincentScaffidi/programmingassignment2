#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Selective Repeat protocol. Adapted from J.F.Kurose's Go-Back-N implementation
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications:
   - removed bidirectional GBN code and other code not used by prac.
   - fixed C style to adhere to current programming style
   - added Selective Repeat implementation
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet
                          MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE 12     /* the sequence space must be at least 2 * windowsize for SR */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet. Used by both sender and receiver
   the simulator will overwrite part of your packet with 'z's. It will not overwrite your
   original checksum. This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for (i = 0; i < 20; i++)
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

static struct pkt buffer[WINDOWSIZE];   /* array for storing packets waiting for ACK */
static int A_ack[WINDOWSIZE];           /* array to keep track of which packets have been ACKed */
static int A_timer[WINDOWSIZE];         /* array to keep track of which packets have timers running */
static int windowfirst, windowlast;     /* array indexes of the first/last packet awaiting ACK */
static int windowcount;                 /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;                /* the next sequence number to be used by the sender */

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  /* if not blocked waiting on full window */
  if (windowcount < WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++)
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    /* put packet in window buffer */
    i = (windowfirst + windowcount) % WINDOWSIZE;
    buffer[i] = sendpkt;
    A_ack[i] = 0;       /* mark as not ACKed */
    A_timer[i] = 0;     /* no timer running yet */
    windowcount++;
    
    /* If windowlast is -1, this is the first packet */
    if (windowlast == -1)
      windowlast = 0;
    else
      windowlast = (windowlast + 1) % WINDOWSIZE;

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3(A, sendpkt);
    
    /* start timer for this packet */
    starttimer(A, RTT);
    A_timer[i] = 1;     /* mark that timer is running */

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
  int i;
  int ack_index = -1;

  /* if received ACK is not corrupted */
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    /* check if new ACK or duplicate */
    if (windowcount != 0) {
      /* find the packet with this sequence number in our window */
      for (i = 0; i < windowcount; i++) {
        int index = (windowfirst + i) % WINDOWSIZE;
        if (buffer[index].seqnum == packet.acknum) {
          ack_index = index;
          break;
        }
      }

      /* if valid ACK in our window */
      if (ack_index != -1 && A_ack[ack_index] == 0) {
        if (TRACE > 0)
          printf("----A: ACK %d is not a duplicate\n", packet.acknum);
        new_ACKs++;

        /* mark packet as ACKed */
        A_ack[ack_index] = 1;

        /* stop timer for this packet if it's running */
        if (A_timer[ack_index]) {
          stoptimer(A);
          A_timer[ack_index] = 0;
        }

        /* Check if we can slide the window */
        while (windowcount > 0 && A_ack[windowfirst]) {
          /* slide window by one packet */
          A_ack[windowfirst] = 0; /* reset for future use */
          A_timer[windowfirst] = 0; /* reset for future use */
          windowfirst = (windowfirst + 1) % WINDOWSIZE;
          windowcount--;
          
          /* If window is now empty, reset windowlast */
          if (windowcount == 0) {
            windowlast = -1;
          }
        }
        
        /* Start a timer for the oldest unacked packet, if needed */
        if (windowcount > 0) {
          int found = 0;
          for (i = 0; i < windowcount; i++) {
            int idx = (windowfirst + i) % WINDOWSIZE;
            if (!A_ack[idx] && !A_timer[idx]) {
              starttimer(A, RTT);
              A_timer[idx] = 1;
              found = 1;
              break;
            }
          }
          
          /* If no timers are running but we still have unacked packets, start a timer for the first one */
          if (!found) {
            for (i = 0; i < windowcount; i++) {
              int idx = (windowfirst + i) % WINDOWSIZE;
              if (!A_ack[idx]) {
                starttimer(A, RTT);
                A_timer[idx] = 1;
                break;
              }
            }
          }
        }
      }
      else {
        if (TRACE > 0)
          printf("----A: duplicate or invalid ACK received, do nothing!\n");
      }
    }
    else {
      if (TRACE > 0)
        printf("----A: ACK received but window is empty, do nothing!\n");
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
  int timer_restarted = 0;

  if (TRACE > 0)
    printf("----A: time out, find and resend unacknowledged packet!\n");

  /* Find the packet whose timer expired and resend only that packet */
  for (i = 0; i < windowcount; i++) {
    int index = (windowfirst + i) % WINDOWSIZE;
    
    if (A_timer[index]) {
      /* This is the packet whose timer expired */
      A_timer[index] = 0;  /* Reset timer status */
      
      if (!A_ack[index]) {  /* If not yet acknowledged */
        if (TRACE > 0)
          printf("---A: resending packet %d\n", buffer[index].seqnum);
        
        tolayer3(A, buffer[index]);
        packets_resent++;
        
        /* Start a new timer for this packet */
        starttimer(A, RTT);
        A_timer[index] = 1;
        timer_restarted = 1;
        break;  /* Only one timer can expire at a time */
      }
    }
  }
  
  /* If no timer was restarted but we have unacked packets, start a timer for the first one */
  if (!timer_restarted && windowcount > 0) {
    for (i = 0; i < windowcount; i++) {
      int index = (windowfirst + i) % WINDOWSIZE;
      if (!A_ack[index]) {
        starttimer(A, RTT);
        A_timer[index] = 1;
        break;
      }
    }
  }
}

/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  /* initialise A's window, buffer and sequence number */
  A_nextseqnum = 0;  /* A starts with seq num 0, do not change this */
  windowfirst = 0;
  windowlast = -1;   /* windowlast is where the last packet sent is stored.
                     new packets are placed in winlast + 1
                     so initially this is set to -1
                   */
  windowcount = 0;
  
  /* Initialize ACK and timer status arrays */
  int i;
  for (i = 0; i < WINDOWSIZE; i++) {
    A_ack[i] = 0;     /* No packets ACKed yet */
    A_timer[i] = 0;   /* No timers running yet */
  }
}

/********* Receiver (B) variables and procedures ************/

static int expectedseqnum;             /* the sequence number expected next by the receiver */
static int B_nextseqnum;               /* the sequence number for the next packets sent by B */
static struct pkt B_buffer[SEQSPACE];   /* buffer for out-of-order packets */
static int B_received[SEQSPACE];       /* track which packets have been received */
static int B_window_start;              /* start of receiver's window */

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;
  int in_window;
  
  /* Check if the packet sequence number is within the receive window */
  if (((B_window_start <= (B_window_start + WINDOWSIZE - 1) % SEQSPACE) && 
       (packet.seqnum >= B_window_start && packet.seqnum <= (B_window_start + WINDOWSIZE - 1) % SEQSPACE)) ||
      ((B_window_start > (B_window_start + WINDOWSIZE - 1) % SEQSPACE) && 
       (packet.seqnum >= B_window_start || packet.seqnum <= (B_window_start + WINDOWSIZE - 1) % SEQSPACE))) {
    in_window = 1;
  } else {
    in_window = 0;
  }

  /* If not corrupted and within receive window */
  if (!IsCorrupted(packet) && in_window) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
    packets_received++;

    /* buffer this packet */
    B_buffer[packet.seqnum] = packet;
    B_received[packet.seqnum] = 1;

    /* send an ACK for the received packet */
    sendpkt.acknum = packet.seqnum;
    
    /* Try delivering in-order packets to the application */
    while (B_received[B_window_start]) {
      /* deliver to receiving application */
      tolayer5(B, B_buffer[B_window_start].payload);
      
      /* mark as not received for future use */
      B_received[B_window_start] = 0;
      
      /* Advance the window */
      B_window_start = (B_window_start + 1) % SEQSPACE;
    }
  }
  else if (!IsCorrupted(packet) && !in_window) {
    /* Packet is outside window but not corrupted - likely an old packet we've already ACKed */
    if (TRACE > 0)
      printf("----B: packet %d outside receive window, send ACK anyway!\n", packet.seqnum);
    
    /* ACK it again to prevent sender retransmission */
    sendpkt.acknum = packet.seqnum;
  }
  else {
    /* packet is corrupted, don't send ACK */
    if (TRACE > 0)
      printf("----B: packet corrupted, don't send ACK!\n");
    return;
  }

  /* create ACK packet */
  sendpkt.seqnum = B_nextseqnum;
  B_nextseqnum = (B_nextseqnum + 1) % 2;  /* Alternating bit for ACK seq num */

  /* we don't have any data to send. fill payload with 0's */
  for (i = 0; i < 20; i++)
    sendpkt.payload[i] = '0';

  /* compute checksum */
  sendpkt.checksum = ComputeChecksum(sendpkt);

  /* send ACK packet */
  tolayer3(B, sendpkt);
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  /* Initialize expected sequence number and acknowledgment number */
  B_window_start = 0;
  B_nextseqnum = 1;
  
  /* Initialize receiver buffer */
  int i;
  for (i = 0; i < SEQSPACE; i++) {
    B_received[i] = 0;  /* Mark all packets as not received */
  }
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}