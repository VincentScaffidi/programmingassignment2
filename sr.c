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
static int timer_status[WINDOWSIZE];   /* array for tracking timer status for each packet */
static int windowfirst, windowlast;    /* array indexes of the first/last packet awaiting ACK */
static int windowcount;                /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;               /* the next sequence number to be used by the sender */
static int acked[SEQSPACE];            /* array to track which packets have been ACKed */

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
    
    /* track that this packet has not been ACKed yet */
    acked[sendpkt.seqnum] = 0;
    
    /* set timer status for this packet */
    timer_status[windowlast] = 1;

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3(A, sendpkt);

    /* start timer for this packet */
    if (windowcount == 1) {
      starttimer(A, RTT);
    }

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
  int i, pos;
  
  /* if received ACK is not corrupted */
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    /* check if ACK is within current window */
    if (windowcount != 0) {
      /* find if the ACK is for a packet in our window */
      bool found = false;
      for (i = 0; i < windowcount; i++) {
        pos = (windowfirst + i) % WINDOWSIZE;
        if (buffer[pos].seqnum == packet.acknum) {
          found = true;
          break;
        }
      }
      
      /* if ACK is in our window and not already ACKed */
      if (found && acked[packet.acknum] == 0) {
        if (TRACE > 0)
          printf("----A: ACK %d is not a duplicate\n", packet.acknum);
        new_ACKs++;
        
        /* Mark packet as ACKed */
        acked[packet.acknum] = 1;
        
        /* Stop timer for this packet */
        if (pos == windowfirst) {
          stoptimer(A);
        
          /* If this is the base (windowfirst), move window forward over all consecutive ACKed packets */
          while (windowcount > 0 && acked[buffer[windowfirst].seqnum] == 1) {
            windowfirst = (windowfirst + 1) % WINDOWSIZE;
            windowcount--;
          }
          
          /* If we still have packets, restart timer for the first unacked packet */
          if (windowcount > 0) {
            starttimer(A, RTT);
          }
        }
      }
      else if (TRACE > 0) {
        printf("----A: duplicate ACK or ACK outside window received, do nothing!\n");
      }
    }
    else if (TRACE > 0) {
      printf("----A: ACK received but window is empty, do nothing!\n");
    }
  }
  else if (TRACE > 0) {
    printf("----A: corrupted ACK is received, do nothing!\n");
  }
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
  if (TRACE > 0)
    printf("----A: time out, resending packets!\n");
  
  /* Find the oldest unacked packet - should be at windowfirst */
  /* In SR, we only resend the specific timed-out packet */
  if (windowcount > 0) {
    if (TRACE > 0)
      printf("---A: resending packet %d\n", buffer[windowfirst].seqnum);
    
    tolayer3(A, buffer[windowfirst]);
    packets_resent++;
    
    /* Restart timer for this packet */
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
  
  /* Initialize timer status and acked arrays */
  for (i = 0; i < WINDOWSIZE; i++) {
    timer_status[i] = 0;
  }
  
  for (i = 0; i < SEQSPACE; i++) {
    acked[i] = 0;
  }
}

/********* Receiver (B) variables and procedures ************/

static int expectedseqnum;       /* the sequence number expected next by the receiver */
static int B_nextseqnum;         /* the sequence number for the next packets sent by B */
static struct pkt B_buffer[WINDOWSIZE]; /* buffer for out-of-order packets */
static int B_received[SEQSPACE]; /* tracks which packets have been received */
static int B_window_base;        /* base sequence number of receiver window */

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;
  int relative_seq;
  int old_relative_seq;
  
  /* if not corrupted */
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
    
    /* Calculate relative position to see if in window */
    relative_seq = (packet.seqnum - B_window_base + SEQSPACE) % SEQSPACE;
    
    /* Check if packet is within the receiver window */
    if (relative_seq < WINDOWSIZE) {
      /* Valid packet within window */
      
      /* Store the packet and mark it as received */
      B_buffer[relative_seq] = packet;
      B_received[packet.seqnum] = 1;
      
      /* If this is the expected packet, deliver it and any consecutive buffered packets */
      if (packet.seqnum == expectedseqnum) {
        /* Deliver this packet */
        tolayer5(B, packet.payload);
        packets_received++;
        
        /* Update expected sequence number */
        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
        
        /* Check for consecutive packets that can now be delivered */
        while (B_received[expectedseqnum] == 1) {
          /* Deliver buffered packet */
          int relative_pos = (expectedseqnum - B_window_base + SEQSPACE) % SEQSPACE;
          tolayer5(B, B_buffer[relative_pos].payload);
          packets_received++;
          
          /* Update expected sequence number */
          expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
        }
        
        /* Slide window to new base */
        B_window_base = expectedseqnum;
      }
      
      /* Send ACK for the received packet */
      sendpkt.acknum = packet.seqnum;
    }
    else {
      /* Packet is outside the window */
      
      /* If it's an old packet that we've already ACKed, resend the ACK */
      old_relative_seq = (packet.seqnum - B_window_base + SEQSPACE + WINDOWSIZE) % SEQSPACE;
      if (old_relative_seq < WINDOWSIZE) {
        sendpkt.acknum = packet.seqnum;
      }
      else {
        /* Otherwise, just discard */
        return;
      }
    }
  }
  else {
    /* packet is corrupted */
    if (TRACE > 0)
      printf("----B: packet corrupted, do nothing!\n");
    return;
  }
  
  /* create ACK packet */
  sendpkt.seqnum = B_nextseqnum;
  B_nextseqnum = (B_nextseqnum + 1) % 2;
  
  /* we don't have any data to send. fill payload with 0's */
  for (i = 0; i < 20; i++)
    sendpkt.payload[i] = '0';
  
  /* compute checksum */
  sendpkt.checksum = ComputeChecksum(sendpkt);
  
  /* send out packet */
  tolayer3(B, sendpkt);
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  int i;
  
  expectedseqnum = 0;
  B_nextseqnum = 1;
  B_window_base = 0;
  
  /* Initialize receiver buffer status */
  for (i = 0; i < SEQSPACE; i++) {
    B_received[i] = 0;
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