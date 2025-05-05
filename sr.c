#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"

#define RTT 16.0       /* round trip time. MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6   /* maximum number of buffered unacked packets, MUST BE SET TO 6 */
#define SEQSPACE 12    /* sequence space for SR, at least 2 * WINDOWSIZE */
#define NOTINUSE (-1)  /* used to fill header fields that are not being used */

/* Structure to hold buffered packets in the sender's window */
struct buffered_pkt {
    struct pkt packet;
    bool is_acked;
    bool is_sent;
};

/* Sender (A) variables */
static struct buffered_pkt buffer[WINDOWSIZE];
static int base;          /* smallest unacked sequence number */
static int nextseqnum;    /* next sequence number to send */
static int windowcount;   /* number of packets in the window */

/* Receiver (B) variables */
static struct pkt receiver_buffer[WINDOWSIZE];
static bool is_received[WINDOWSIZE];
static int rcv_base;      /* base of receiver window, next expected in-order seqnum */

/* Function to compute checksum */
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

/* Function to check if packet is corrupted */
bool IsCorrupted(struct pkt packet)
{
    return packet.checksum != ComputeChecksum(packet);
}

/* Helper function to check if a sequence number is within the window */
bool is_in_window(int seqnum, int base, int window_size, int seq_space) {
    int end = (base + window_size - 1) % seq_space;
    if (base <= end) {
        return seqnum >= base && seqnum <= end;
    } else {
        return seqnum >= base || seqnum <= end;
    }
}

/* Sender (A) functions */

/* Called from layer 5, passed the data to be sent to other side */
void A_output(struct msg message)
{
    if (windowcount < WINDOWSIZE) {
        struct pkt sendpkt;
        int i;
        sendpkt.seqnum = nextseqnum;
        sendpkt.acknum = NOTINUSE;
        for (i = 0; i < 20; i++)
            sendpkt.payload[i] = message.data[i];
        sendpkt.checksum = ComputeChecksum(sendpkt);

        /* Store in buffer at position windowcount */
        buffer[windowcount].packet = sendpkt;
        buffer[windowcount].is_acked = false;
        buffer[windowcount].is_sent = true;

        /* Send the packet */
        if (TRACE > 0)
            printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
        tolayer3(A, sendpkt);

        /* Start timer if this is the first packet in the window */
        if (windowcount == 0)
            starttimer(A, RTT);

        windowcount++;
        nextseqnum = (nextseqnum + 1) % SEQSPACE;
    } else {
        if (TRACE > 0)
            printf("----A: New message arrives, send window is full\n");
        window_full++;
    }
}

/* Called from layer 3, when a packet arrives for layer 4 */
void A_input(struct pkt packet)
{
    int i;
    if (!IsCorrupted(packet)) {
        int acknum = packet.acknum;
        /* Mark the packet as acknowledged if it’s in the window */
        for (i = 0; i < windowcount; i++) {
            if (buffer[i].packet.seqnum == acknum && !buffer[i].is_acked) {
                if (TRACE > 0)
                    printf("----A: ACK %d received, marking packet as acked\n", acknum);
                buffer[i].is_acked = true;
                new_ACKs++;
                total_ACKs_received++;
                break;
            }
        }

        /* Slide the window if possible */
        while (windowcount > 0 && buffer[0].is_acked) {
            for (i = 0; i < windowcount - 1; i++) {
                buffer[i] = buffer[i + 1];
            }
            windowcount--;
            base = (base + 1) % SEQSPACE;
        }

        /* Manage timer: stop if window is empty, else restart for the new base */
        if (windowcount == 0) {
            stoptimer(A);
        } else {
            stoptimer(A);
            starttimer(A, RTT);
        }
    } else {
        if (TRACE > 0)
            printf("----A: corrupted ACK received, ignored\n");
    }
}

/* Called when A's timer goes off */
void A_timerinterrupt(void)
{
    int i;
    if (TRACE > 0)
        printf("----A: time out, resend unacked packets!\n");
    for (i = 0; i < windowcount; i++) {
        if (!buffer[i].is_acked) {
            if (TRACE > 0)
                printf("---A: resending packet %d\n", buffer[i].packet.seqnum);
            tolayer3(A, buffer[i].packet);
            packets_resent++;
        }
    }
    if (windowcount > 0)
        starttimer(A, RTT);
}

/* Initialization for sender (A) */
void A_init(void)
{
    base = 0;
    nextseqnum = 0;
    windowcount = 0;
    int i;
    for (i = 0; i < WINDOWSIZE; i++) {
        buffer[i].is_sent = false;
        buffer[i].is_acked = false;
    }
}

/* Receiver (B) functions */

/* Called from layer 3, when a packet arrives for layer 4 at B */
void B_input(struct pkt packet)
{
    int i;
    if (!IsCorrupted(packet)) {
        int seqnum = packet.seqnum;
        int index = -1;

        /* Check if packet is within the receiver window */
        for (i = 0; i < WINDOWSIZE; i++) {
            int expected_seqnum = (rcv_base + i) % SEQSPACE;
            if (expected_seqnum == seqnum) {
                index = i;
                break;
            }
        }

        if (index != -1 && !is_received[index]) {
            struct pkt sendpkt;
            /* Buffer the packet and mark as received */
            receiver_buffer[index] = packet;
            is_received[index] = true;
            packets_received++;

            /* Send ACK for this packet */
            sendpkt.seqnum = NOTINUSE;
            sendpkt.acknum = seqnum;
            for (i = 0; i < 20; i++)
                sendpkt.payload[i] = '0';
            sendpkt.checksum = ComputeChecksum(sendpkt);
            if (TRACE > 0)
                printf("----B: packet %d received, sending ACK %d\n", seqnum, seqnum);
            tolayer3(B, sendpkt);
        }

        /* Deliver in-order packets */
        while (is_received[0]) {
            if (TRACE > 0)
                printf("----B: delivering packet %d to layer 5\n", receiver_buffer[0].seqnum);
            tolayer5(B, receiver_buffer[0].payload);

            /* Shift the buffer */
            for (i = 0; i < WINDOWSIZE - 1; i++) {
                receiver_buffer[i] = receiver_buffer[i + 1];
                is_received[i] = is_received[i + 1];
            }
            is_received[WINDOWSIZE - 1] = false;
            rcv_base = (rcv_base + 1) % SEQSPACE;
        }
    } else {
        if (TRACE > 0)
            printf("----B: packet corrupted, ignored\n");
    }
}

/* Initialization for receiver (B) */
void B_init(void)
{
    int i;
    rcv_base = 0;
    for (i = 0; i < WINDOWSIZE; i++)
        is_received[i] = false;
}

/* Functions for bidirectional communication (not used) */
void B_output(struct msg message)
{
}

void B_timerinterrupt(void)
{
}