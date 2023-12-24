// Link layer header.
// NOTE: This file must not be changed.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <signal.h>
#include <unistd.h>

#ifndef _LINK_LAYER_H_
#define _LINK_LAYER_H_

#define FLAG 0x7E
#define ADDRESS_SET 0x03
#define CONTROL_SET 0x03

#define ADDRESS_UA 0x03
#define CONTROL_UA 0x07

#define ADDRESS_TX 0X03
#define ADDRESS_RX 0x01

#define C_FRAME0 0x00
#define C_FRAME1 0x40

#define DISC_VAL 0x0B
#define ESCAPE_VAL 0x7D

#define RR0 0x05
#define RR1 0x85
#define RREJ0 0X01
#define RREJ1 0x81

#define BUF_SIZE 5

#define SIGALRM 14

#define SHIFT_LEFT_6(Ns) (Ns << 6)

#define SET_PACKET {FLAG, ADDRESS_SET, CONTROL_SET, ADDRESS_SET ^ CONTROL_SET, FLAG};
#define UA_PACKET {FLAG, ADDRESS_UA, CONTROL_UA, ADDRESS_UA ^ CONTROL_UA, FLAG};

#define RR0 0x05
#define RR1 0x85
#define REJ0 0x01
#define REJ1 0x81
#define DISC 0x0B

typedef enum
{
    LlTx,
    LlRx,
} LinkLayerRole;

typedef struct
{
    const char serialPort[50];
    LinkLayerRole role;
    int baudRate;
    int nRetransmissions;
    int timeout;
} LinkLayer;

typedef enum
{
    ESTABLISH_CONNECTION,
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK,
    STOP_,
    DATA_PACKET,
    RR,
    RREJ,
    ESCAPE_FOUND,
    END_CONNECTION,
} LinkLayerState;

// SIZE of maximum acceptable payload.
// Maximum number of bytes that application layer should send to link layer
#define MAX_PAYLOAD_SIZE 1000

// MISC
#define FALSE 0
#define TRUE 1

// Open a connection using the "port" parameters defined in struct linkLayer.
// Return "1" on success or "-1" on error.
int llopen(LinkLayer connectionParameters);

// Send data in buf with size bufSize.
// Return number of chars written, or "-1" on error.
int llwrite(const unsigned char *buf, int bufSize);

// Receive data in packet.
// Return number of chars read, or "-1" on error.
int llread(unsigned char *packet);

// Close previously opened connection.
// if showStatistics == TRUE, link layer should print statistics in the console on close.
// Return "1" on success or "-1" on error.
int llclose(int showStatistics);

int connect(LinkLayer connectionParameters);

#endif // _LINK_LAYER_H_
