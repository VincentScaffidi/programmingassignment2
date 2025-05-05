#ifndef SR_H
#define SR_H

/* function prototypes */
void A_output(struct msg message);
void A_input(struct pkt packet);
void A_timerinterrupt(void);
void A_init(void);
void B_input(struct pkt packet);
void B_init(void);
void B_output(struct msg message);
void B_timerinterrupt(void);

/* other function prototypes */
int ComputeChecksum(struct pkt packet);
bool IsCorrupted(struct pkt packet);

#endif