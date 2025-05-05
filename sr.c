
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

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver */
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
  return (packet.checksum != ComputeChecksum(packet));
}

/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE];
static int windowfirst, windowlast;
static int windowcount;
static int A_nextseqnum;
static int acked[SEQSPACE];

/* called from layer 5 */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  if (windowcount < WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++)
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    windowlast = (windowlast + 1) % WINDOWSIZE;
    buffer[windowlast] = sendpkt;
    windowcount++;
    acked[sendpkt.seqnum] = 0;

    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3(A, sendpkt);

    if (windowcount == 1)
      starttimer(A, RTT);

    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
  } else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}

/* handle incoming ACKs */
void A_input(struct pkt packet)
{
  int i, pos;

  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    if (windowcount != 0) {
      bool found = false;
      for (i = 0; i < windowcount; i++) {
        pos = (windowfirst + i) % WINDOWSIZE;
        if (buffer[pos].seqnum == packet.acknum) {
          found = true;
          break;
        }
      }
      if (found && acked[packet.acknum] == 0) {
        if (TRACE > 0)
          printf("----A: ACK %d is not a duplicate\n", packet.acknum);
        new_ACKs++;
        acked[packet.acknum] = 1;

        if (pos == windowfirst) {
          stoptimer(A);
          while (windowcount > 0 && acked[buffer[windowfirst].seqnum] == 1) {
            windowfirst = (windowfirst + 1) % WINDOWSIZE;
            windowcount--;
          }
          if (windowcount > 0)
            starttimer(A, RTT);
        }
      }
    }
  } else if (TRACE > 0) {
    printf("----A: corrupted ACK is received, do nothing!\n");
  }
}

void A_timerinterrupt(void)
{
  if (TRACE > 0)
    printf("----A: time out, resending packets!\n");
  if (windowcount > 0) {
    if (TRACE > 0)
      printf("---A: resending packet %d\n", buffer[windowfirst].seqnum);
    tolayer3(A, buffer[windowfirst]);
    packets_resent++;
    starttimer(A, RTT);
  }
}

void A_init(void)
{
  int i;
  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1;
  windowcount = 0;
  for (i = 0; i < SEQSPACE; i++)
    acked[i] = 0;
}

/********* Receiver (B) ************/

static int expectedseqnum;
static int B_nextseqnum;
static struct pkt B_buffer[WINDOWSIZE];
static int B_received[SEQSPACE];
static int B_window_base;

void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i, relative_seq, old_relative_seq;

  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
    relative_seq = (packet.seqnum - B_window_base + SEQSPACE) % SEQSPACE;

    if (relative_seq < WINDOWSIZE) {
      B_buffer[relative_seq] = packet;
      B_received[packet.seqnum] = 1;
      if (packet.seqnum == expectedseqnum) {
        tolayer5(B, packet.payload);
        packets_received++;
        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
        while (B_received[expectedseqnum]) {
          int relative_pos = (expectedseqnum - B_window_base + SEQSPACE) % SEQSPACE;
          tolayer5(B, B_buffer[relative_pos].payload);
          packets_received++;
          expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
        }
        B_window_base = expectedseqnum;
      }
      sendpkt.acknum = packet.seqnum;
    } else {
      old_relative_seq = (packet.seqnum - B_window_base + SEQSPACE + WINDOWSIZE) % SEQSPACE;
      if (old_relative_seq < WINDOWSIZE) {
        sendpkt.acknum = packet.seqnum;
      } else {
        return;
      }
    }
  } else {
    if (TRACE > 0)
      printf("----B: packet corrupted, do nothing!\n");
    return;
  }

  sendpkt.seqnum = B_nextseqnum;
  B_nextseqnum = (B_nextseqnum + 1) % 2;
  for (i = 0; i < 20; i++)
    sendpkt.payload[i] = '0';
  sendpkt.checksum = ComputeChecksum(sendpkt);
  tolayer3(B, sendpkt);
}

void B_init(void)
{
  int i;
  expectedseqnum = 0;
  B_nextseqnum = 1;
  B_window_base = 0;
  for (i = 0; i < SEQSPACE; i++)
    B_received[i] = 0;
}

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
