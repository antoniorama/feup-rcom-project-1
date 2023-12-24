// Link layer protocol implementation

#include "link_layer.h"

int alarmEnabled = TRUE;
int alarmCount;
unsigned char currentFrameNum = 1;
unsigned char controlTx = 0;
unsigned char controlRx = 1;
int timeout = 0;
int nRetransmissions = 0;
int fd;

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source


void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d\n", alarmCount);
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    LinkLayerState state;
    (void)signal(SIGALRM, alarmHandler);
    state = START;

    timeout = connectionParameters.timeout;
    nRetransmissions = connectionParameters.nRetransmissions;

    if (connectionParameters.role == LlTx)
    {
        fd = connect(connectionParameters);
        unsigned char buf[5] = SET_PACKET;
        while (nRetransmissions > 0 && state != STOP_)
        {
            int bytes = write(fd, buf, BUF_SIZE);
            alarm(timeout);
            alarmEnabled = TRUE;

            unsigned char read_buf[BUF_SIZE + 1] = {0};
            int opened_success = FALSE;

            while (alarmEnabled && state != STOP_)
            {

                if (read(fd, read_buf, 1) > 0)
                {
                    /* é sempre buf[0] pois o array elimina o primeiro elemento */
                    switch (state)
                    {
                    case START:
                    
                        if (read_buf[0] == FLAG) state = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (read_buf[0] == FLAG) state = FLAG_RCV;
                        else if (read_buf[0] == ADDRESS_UA) state = A_RCV;
                        else state = START;
                        break;
                    case A_RCV:
                        if (read_buf[0] == FLAG) state = FLAG_RCV;
                        else if (read_buf[0] == CONTROL_UA) state = C_RCV;
                        else state = START;
                        break;
                    case C_RCV:
                        if (read_buf[0] == FLAG) state = FLAG_RCV;
                        else if (read_buf[0] == (ADDRESS_UA ^ CONTROL_UA)) state = BCC_OK;
                        else state = START;
                        break;
                    case BCC_OK:
                        if (read_buf[0] == FLAG)
                        {
                            printf("Connection Done!\n");
                            alarm(0);
                            alarmEnabled = FALSE;
                            state = STOP_;
                            opened_success = TRUE;
                            
                        }
                        else
                            state = START;
                        break;
                    default:
                        continue;
                    }
                }
            }
            if (!opened_success) nRetransmissions--;
        }
    }
    else if (connectionParameters.role == LlRx)
    {
        fd = connect(connectionParameters);
        unsigned char buf[BUF_SIZE + 1] = {0}; // +1: Save space for the final '\0' char
        
        while (state != STOP_)
        {
            if (read(fd, buf, 1) > 0) {

                switch (state)
                {
                case START:
                    if (buf[0] == FLAG) state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (buf[0] == ADDRESS_SET) state = A_RCV;
                    else if (buf[0] == FLAG) break;
                    else state = START;
                    break;
                case A_RCV:
                    if (buf[0] == CONTROL_SET) state = C_RCV;
                    else if (buf[0] == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case C_RCV:
                    if (buf[0] == (ADDRESS_SET ^ CONTROL_SET)) state = BCC_OK;
                    else if (buf[0] == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case BCC_OK:
                    if (buf[0] == FLAG) state = STOP_;
                    else state = START;
                    break;
                default:
                    break;
                }
            }
            }
        printf("Received SET succesfully\n ");
        unsigned char buf_write[5] = UA_PACKET;

        int bytes = write(fd, buf_write, BUF_SIZE);
        printf("Wrote UA (%d bytes)\n", bytes);
    }

    if (nRetransmissions == 0) return -1;
    return 1;
}

int readSingleFrame() {

    LinkLayerState state = START;
    unsigned char sv_frame[BUF_SIZE + 1] = {0};
    unsigned char c = 0;

    while (state != STOP_) {
        if (read(fd, sv_frame, 1) > 0) {
            switch(state) {
                case START:
                    if (sv_frame[0] == FLAG) state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (sv_frame[0] == ADDRESS_RX) state = A_RCV;
                    else if (sv_frame[0] == FLAG) break;
                    else state = START;
                    break; 
                case A_RCV:
                    if (sv_frame[0] == ADDRESS_SET || sv_frame[0] == CONTROL_UA || sv_frame[0] == RR0 || sv_frame[0] == RR1 || sv_frame[0] == REJ0 || sv_frame[0] == REJ1 || sv_frame[0] == DISC) {
                        state = C_RCV;
                        c = sv_frame[0];
                    }
                    else if (sv_frame[0] == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case C_RCV:
                    if (sv_frame[0] == (ADDRESS_RX ^ c)) state = BCC_OK;
                    else if (sv_frame[0] == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case BCC_OK:
                    if (sv_frame[0] == FLAG) state = STOP_;
                    else state = START;
                    break;
                default:
                    break;
            }
        }
    }

    return c;
}
////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // trama : F A N(s) BCC1 dados(buf) BCC2 F
    int frameISize = 6+bufSize;
    unsigned char *frameI = (unsigned char *)malloc(frameISize);
    frameI[0] = FLAG;
    frameI[1] = ADDRESS_SET;
    frameI[2] = SHIFT_LEFT_6(controlTx); // 1
    frameI[3] = frameI[1] ^ frameI[2];
    memcpy(&frameI[4], buf, bufSize);

    // recebe N bytes e calcula o XOR de todos
    unsigned char BCC2 = buf[0];
    for (unsigned int i = 1; i < bufSize; i++) BCC2 ^= buf[i];

    // stuffing
    // percorre array de N+1 bytes
    // se encontra o valor 0x7E (FLAG) substitui por 0x7D 0x5E
    // se encontra o valor 0x7D substitui por 0x7D 0x5D
    int next = 4;

    for (unsigned int i = 0; i < bufSize; i++) {
        if (buf[i] == FLAG) {
            frameI = realloc(frameI, ++frameISize);
            frameI[next++] = ESCAPE_VAL;
            frameI[next++] = 0x5E;
        }
        else if (buf[i] == ESCAPE_VAL) {
            frameI = realloc(frameI, ++frameISize);
            frameI[next++] = ESCAPE_VAL;
            frameI[next++] = 0x5D;
        } 
        else frameI[next++] = buf[i];
    }

    // bcc2 stuffing
    if (BCC2 == FLAG) {
        frameI = realloc(frameI, ++frameISize);
        frameI[next++] = ESCAPE_VAL;
        frameI[next++] = 0x5E;
        }
    else if (BCC2 == ESCAPE_VAL) {
        frameI = realloc(frameI, ++frameISize);
        frameI[next++] = ESCAPE_VAL;
        frameI[next++] = 0x5D;
    } 
    else frameI[next++] = BCC2;

    frameI[next++] = FLAG;

    int transmissionCount = 0;
    int accept_or_reject = 0; // 1 -> accepted ; 2 -> rejected

    while (transmissionCount < nRetransmissions) {
        alarm(timeout);
        alarmEnabled = TRUE;

        while (alarmEnabled && accept_or_reject == 0) {

            write(fd, frameI, next + 1);
            unsigned char c_return = readSingleFrame();

            if (!c_return) continue;
            else if (c_return == RR0 || c_return == RR1) { // accept
                accept_or_reject = 1; 
                // swap the value
                if (controlTx == 0) controlTx = 1;
                else if (controlTx == 1) controlTx = 0;
            }
            else if (c_return == REJ0 || c_return == REJ1) accept_or_reject = 2; // reject
            else c_return = 0; // fica igual
        }

        if (accept_or_reject == 1) break;
        transmissionCount++;
    }

    if (accept_or_reject == 1) return frameISize;
    return -1;
}

////////////////////////////////////////////////
// LLREAD
int sendSingleFrame(unsigned char A, unsigned char C){
    unsigned char frame[5] = {FLAG, A, C, A^C, FLAG};
    write(fd, frame, 5);
    if (C == DISC_VAL) return 1;
    return 0;
}
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    unsigned char rByte, c_value;
    int packetPos = 0;
    int found_flag_after_escape = FALSE;
    LinkLayerState state = START;

    while(state != STOP_){
        if (read(fd, &rByte, 1) > 0){
            switch (state)
            {
            case START:
                if (rByte == FLAG) state = FLAG_RCV;
                break;
            case FLAG_RCV:
                if (rByte == ADDRESS_TX) state = A_RCV;
                else if (rByte != FLAG) state = START;
                break;
            case A_RCV:
                if (rByte == C_FRAME0 || rByte == C_FRAME1){
                    if ((rByte == C_FRAME0 & currentFrameNum == 0) || (rByte == C_FRAME1 & currentFrameNum == 1)){
                        state = RR;
                        break;
                    } 
                    state = C_RCV;
                    c_value = rByte;
                } 
                else if (rByte == FLAG) state = FLAG_RCV;
                else if (rByte == DISC_VAL) state = END_CONNECTION;
                else state = START;
                break;
            case C_RCV:
                if (rByte == (ADDRESS_TX ^ c_value)) state = DATA_PACKET;
                else if (rByte == FLAG) state = FLAG_RCV;
                else state = START;
                break;
            case DATA_PACKET:
                if (rByte == ESCAPE_VAL) state = ESCAPE_FOUND;
                else if (rByte == FLAG){
                    unsigned char bcc2 = packet[packetPos - 1];
                    packet[--packetPos] = '\0';
                    unsigned char bcc2_test = packet[0];

                    for (int i = 1; i < packetPos; i++) bcc2_test ^= packet[i];
          
                    if (bcc2 == bcc2_test){
                        state = RR;
                        if (currentFrameNum == 0) currentFrameNum = 1; else currentFrameNum = 0;
                        break;
                    }
                    else{
                        state = RREJ;
                        break;
                    }
                }
                else packet[packetPos++] = rByte;
                break;
            case ESCAPE_FOUND:
                if (rByte == FLAG){
                    unsigned char bcc2 = ESCAPE_VAL;

                    packet[--packetPos+1] = '\0';
                    unsigned char bcc2_test = packet[0];

                    for (int i = 1; i < packetPos; i++) bcc2_test ^= packet[i];

                    if (bcc2 == bcc2_test){
                        state = RR;
                        if (currentFrameNum == 0) currentFrameNum = 1; else currentFrameNum = 0;
                        break;
                    }
                    else{
                        state = RREJ;
                        break;
                    }
                }
                packet[packetPos++] = rByte == 0x5E ? FLAG : ESCAPE_VAL;
                state = DATA_PACKET;
                break;
            case RR:
                //printf("RR \n");
                sendSingleFrame(ADDRESS_RX, currentFrameNum==0 ? RR1 : RR0);
                return packetPos - 4; //* FLAG + PACKET_C_DATA + BCC2 + FLAG
                break;
            case RREJ:
                sendSingleFrame(ADDRESS_RX, currentFrameNum==0 ? RREJ1 : RREJ0);
                return -1;
                break;
            case END_CONNECTION:
                return sendSingleFrame(ADDRESS_TX, DISC_VAL);
                break;
            default:
                break;
            }
        }
    }

    return c_value;
}


////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    LinkLayerState state = START;
    unsigned char rByte;
    int transmissionCount = 0;
    sendSingleFrame(ADDRESS_TX, DISC_VAL);

    while (transmissionCount < nRetransmissions && state != STOP_){
        alarm(timeout);
        alarmEnabled = TRUE;

        while(alarmEnabled && state != STOP_){
            if (read(fd, &rByte, 1) > 0){
                switch (state)
                {
                case START:
                    if (rByte == FLAG) state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (rByte == ADDRESS_TX) state = A_RCV;
                    else if (rByte != FLAG) state = START;
                    break;
                case A_RCV:
                    if (rByte == DISC_VAL) state = C_RCV;
                    else if (rByte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case C_RCV:
                    if (rByte == ADDRESS_TX ^ DISC_VAL) state = BCC_OK;
                    else if (rByte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case BCC_OK:
                    if (rByte == FLAG) state = STOP_;
                    else state = START;
                    break;
                default:
                    break;
                }
            }
        }
        transmissionCount++;
    }

    if (state != STOP_) return -1;
    sendSingleFrame(ADDRESS_TX, ADDRESS_UA);
    return close(fd);
}


int connect(LinkLayer connectionParameters)
{
    int fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(connectionParameters.serialPort);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 0;

    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    return fd;
}
