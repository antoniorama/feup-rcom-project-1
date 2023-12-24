//* Application layer protocol implementation

#include "application_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_PACKET_SIZE 24
#define PACKET_C_START 0x02
#define PACKET_C_END 0x03

// TODO - todas as funções de parsing têm de ser passadas para o link layer

unsigned char *createControlPacket(const unsigned int c, const char* filename, long int filesize, unsigned int* controlPacketSize) {
    
    if (c < 2 || c > 3) {
        printf("c must be 2 or 3");
        return NULL;
    }

    //* bytes necessarios para guardar o tamanho do ficheiro
    int L1;
    float log2_value = log2_custom(filesize);
    if (log2_value != (int)log2_value) log2_value = (int)(log2_value + 1);
    L1 = (int)(log2_value / 8.0);
    if (L1 * 8.0 != log2_value) L1++;

    int L2 = strlen(filename); //* tamanho do nome do ficheiro

    *controlPacketSize = 3 + L1 + 2 + L2;

    unsigned char *controlPacket = (unsigned char *)malloc(*controlPacketSize);

    if (controlPacket == NULL) {
        printf("Memory allocation failed!\n");
        return NULL;
    }

    int index = 0;
    controlPacket[index++] = c; //* C
    controlPacket[index++] = 0; //* T1
    controlPacket[index++] = L1; //* L1
    
    //* v1
    for (int i = 0; i < L1; i++) {
        controlPacket[index + L1 - i - 1] = filesize & 0xFF;
        filesize >>= 8;
    }
    index += L1;

    controlPacket[index++] = 1; //* T2 
    controlPacket[index++] = L2;

    memcpy(&controlPacket[index], filename, L2);

    return controlPacket;
}

unsigned char *createDataPacket(const unsigned char *data, unsigned int dataSize, unsigned int *dataPacketSize) {

    //* calcular l1 e l2 de acordo com dataSize
    unsigned int L2 = dataSize >> 8 & 0xFF;
    unsigned int L1 = dataSize & 0xFF;

    //* cacular o tamanho do data packet
    *dataPacketSize = 4 + dataSize;

    unsigned char *dataPacket = (unsigned char *)malloc(*dataPacketSize);
    if (dataPacket == NULL) {
        printf("Memory allocation failed for data packet\n");
        return NULL;
    }

    int index = 0;
    dataPacket[index++] = 1; //* C
    dataPacket[index++] = L2;
    dataPacket[index++] = L1;

    memcpy(&dataPacket[index], data, dataSize);

    return dataPacket;
}

unsigned char* readControlPacket(unsigned char * packet){
    unsigned char t1_size = packet[2]; 
    unsigned char t2_size = packet[4+t1_size];
    unsigned char* filename = (unsigned char *) malloc(t2_size);
    memcpy(filename, packet+4+t1_size+1, t2_size);
    
    return filename;
    
}

void readDataPacket(unsigned char* packet, unsigned int packet_size, unsigned char* buffer){
    memcpy(buffer, packet + 3, packet_size - 4);
    buffer += packet_size+4;
}

//* Calculate log2
int log2_custom(int value) {
    int count = 0;
    while (value > 1) {
        value /= 2;
        count++;
    }
    return count;
}


unsigned char* readFileToBuffer(FILE* file, long int fileSize) {
    if (!file) {
        printf("File pointer is NULL\n");
        return NULL;
    }
    fseek(file, 0, SEEK_SET);
    unsigned char* buffer = (unsigned char*)malloc(sizeof(unsigned char) * fileSize);
    fread(buffer, sizeof(unsigned char), fileSize, file);
    fseek(file, 0, SEEK_SET);
    return buffer;
}

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer conParam;
    conParam.role = strcmp(role, "tx") ? LlRx : LlTx;
    strcpy(conParam.serialPort, serialPort);
    conParam.baudRate = baudRate;
    conParam.nRetransmissions = nTries;
    conParam.timeout = timeout;

    if (llopen(conParam) < 0) {
        printf("Error llopen\n");
        exit(1);
    }

    switch (conParam.role) {
        case LlTx:;
            FILE *file = fopen(filename, "rb");
            if (!file) {
                perror("Unable to open file");
                return -1;
            }

            
            fseek(file, 0, SEEK_END);
            long int filesize = ftell(file);

            //* criação do pacote START
            unsigned int startPacketSize;
            unsigned char *startPacket = createControlPacket(2, filename, filesize, &startPacketSize);

            //* invocação do llwrite passando esse pacote
            if (llwrite(startPacket, startPacketSize) == -1) {
                printf("Error llwrite with start packet\n");
                exit(1);
            }


            //** se llwrite retorna com sucesso
            //*** ciclo até chegar ao fum do ficheiro a transferir (termina se llwrite retorna erro)
            rewind(file);
            unsigned char* buffer = readFileToBuffer(file, filesize);

            int packet_size = MAX_PACKET_SIZE;
            int packets_number = filesize / packet_size;
            int index = 0;
            while(packets_number > index++) {
                //* lê um segmento de K bytes
                unsigned char* holdPacket = (unsigned char*)malloc(packet_size);
                memcpy(holdPacket, buffer, packet_size);
                
                //* cria pacote de dados
                unsigned int dataPacketSize;
                unsigned char *dataPacket = createDataPacket(holdPacket, packet_size, &dataPacketSize);

                //* invoca llwrite passando pacote de dados
                if (llwrite(dataPacket, dataPacketSize) == -1) {
                    printf("llwrite error");
                    free(holdPacket);
                    break;
                } 

                free(holdPacket); 
                
                buffer += packet_size;
            }


            //* criar pacote END e invocar llwrite com esse pacote
            unsigned int endPacketSize;
            unsigned char *endPacket = createControlPacket(3, filename, filesize, &endPacketSize);

            if (llwrite(endPacket, endPacketSize) == -1) {
                printf("llwrite error - end packet\n");
                exit(1);
            }

            fclose(file);
            llclose(0);
            break;
            
        case LlRx:;
            unsigned char *packet = (unsigned char *)malloc(MAX_PACKET_SIZE);
            int packetSize = 0;

            while ((packetSize = llread(packet)) <= 0);

            unsigned char *file_name = readControlPacket(packet);

            FILE* resultFile = fopen((char *) file_name, "wb+");
            int packet_num=1;
            while(TRUE){
                while ((packetSize = llread(packet)) < 0);
                if (packetSize == 0 || packetSize == 1) break;
                else if (packet[0] != PACKET_C_END && packet[0] != PACKET_C_START ){
                    unsigned char *filebuffer = (unsigned char *)malloc(packetSize);
                    readDataPacket(packet, packetSize, filebuffer);
                    fwrite(filebuffer, sizeof(unsigned char), packetSize, resultFile);
                    free(filebuffer);
                }
                else if (packet[0] == PACKET_C_END){
                    fclose(resultFile);
                }
                
            }
            break;
        default:
            break;
    }
}
