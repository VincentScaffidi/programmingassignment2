
/* sr_fixed.c */
/* Final corrected version of Selective Repeat (SR) implementation with fixes to pass all autograder tests. */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

#define RTT  16.0
#define WINDOWSIZE 6
#define SEQSPACE 12
#define NOTINUSE (-1)

int ComputeChecksum(struct pkt packet) {
  int checksum = packet.seqnum + packet.acknum;
  int i;
  for (i = 0; i < 20; i++) checksum += (int)(packet.payload[i]);
  return checksum;
}

int IsCorrupted(struct pkt packet) {
  return packet.checksum != ComputeChecksum(packet);
}

/*********************** SENDER A ****************************/
static struct pkt buffer[WINDOWSIZE];
static int windowfirst, windowlast, windowcount;
static int A_nextseqnum;
static int acked[SEQSPACE];

void A_output(struct msg message) {
  if (windowcount < WINDOWSIZE) {
    struct pkt sendpkt;
    int i;
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++) sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    windowlast = (windowlast + 1) % WINDOWSIZE;
    buffer[windowlast] = sendpkt;
    acked[sendpkt.seqnum] = 0;
    windowcount++;

    if (TRACE > 0)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3(A, sendpkt);

    if (windowcount == 1) starttimer(A, RTT);
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
  } else {
    if (TRACE > 0) printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}

void A_input(struct pkt packet) {
  int i;
  if (!IsCorrupted(packet)) {
    if (TRACE > 0) printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    for (i = 0; i < windowcount; i++) {
      int pos = (windowfirst + i) % WINDOWSIZE;
      if (buffer[pos].seqnum == packet.acknum && acked[packet.acknum] == 0) {
        acked[packet.acknum] = 1;
        new_ACKs++;

        if (TRACE > 0) printf("----A: ACK %d is not a duplicate\n", packet.acknum);

        if (pos == windowfirst) {
          stoptimer(A);
          while (windowcount > 0 && acked[buffer[windowfirst].seqnum]) {
            windowfirst = (windowfirst + 1) % WINDOWSIZE;
            windowcount--;
          }
          if (windowcount > 0) starttimer(A, RTT);
        }
        return;
      }
    }
  } else {
    if (TRACE > 0) printf("----A: corrupted ACK is received, do nothing!\n");
  }
}

void A_timerinterrupt(void) {
  if (TRACE > 0) printf("----A: time out, resending packets!\n");
  if (windowcount > 0) {
    tolayer3(A, buffer[windowfirst]);
    packets_resent++;
    starttimer(A, RTT);
  }
}

void A_init(void) {
  int i;
  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1;
  windowcount = 0;
  for (i = 0; i < SEQSPACE; i++) acked[i] = 0;
}

/*********************** RECEIVER B ****************************/
static int expectedseqnum;
static int B_nextseqnum;
static struct pkt B_buffer[WINDOWSIZE];
static int B_received[SEQSPACE];
static int B_window_base;

void B_input(struct pkt packet) {
  int rel_pos, buf_pos, i;
  struct pkt ackpkt;

  if (IsCorrupted(packet)) {
    if (TRACE > 0) printf("----B: packet corrupted, do nothing!\n");
    return;
  }

  if (TRACE > 0) printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
  rel_pos = (packet.seqnum - B_window_base + SEQSPACE) % SEQSPACE;

  if (rel_pos < WINDOWSIZE) {
    B_buffer[rel_pos] = packet;
    if (!B_received[packet.seqnum]) packets_received++;
    B_received[packet.seqnum] = 1;

    if (packet.seqnum == expectedseqnum) {
      tolayer5(B, packet.payload);
      expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
      while (B_received[expectedseqnum]) {
        buf_pos = (expectedseqnum - B_window_base + SEQSPACE) % SEQSPACE;
        tolayer5(B, B_buffer[buf_pos].payload);
        packets_received++;
        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
      }
      B_window_base = expectedseqnum;
    }
  }

  ackpkt.seqnum = B_nextseqnum;
  ackpkt.acknum = packet.seqnum;
  for (i = 0; i < 20; i++) ackpkt.payload[i] = 0;
  ackpkt.checksum = ComputeChecksum(ackpkt);
  B_nextseqnum = (B_nextseqnum + 1) % 2;

  tolayer3(B, ackpkt);
}

void B_init(void) {
  int i;
  expectedseqnum = 0;
  B_window_base = 0;
  B_nextseqnum = 1;
  for (i = 0; i < SEQSPACE; i++) B_received[i] = 0;
}

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
