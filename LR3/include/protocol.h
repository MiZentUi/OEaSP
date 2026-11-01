#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#ifndef STUDENT_ID
#define STUDENT_ID 103
#endif

#define VARIANT_ID 26
#define MSG_VERSION 1U
#define BENCH_SMALL_SIZE 64
#define BENCH_LARGE_SIZE 4096
#define MAX_DGRAM BENCH_LARGE_SIZE
#define LOG_TEXT_MAX 128
#define LOG_TEXT_LEN (32 + STUDENT_ID % 32)
#define CLIENT_MESSAGES (10 + STUDENT_ID % 20)
#define BENCH_COUNT (1000 + STUDENT_ID)
#define SERVER_TIMEOUT_SEC 10
#define CLIENT_COUNT 3

enum MessageType {
    MSG_LOG = 1,
    MSG_STOP = 2,
    MSG_BENCH = 3,
    MSG_BENCH_END = 4
};

typedef struct {
    uint32_t version;
    uint32_t messageType;
    uint32_t sourcePid;
    uint32_t requestId;
    uint32_t payloadSize;
} MessageHeader;

typedef struct {
    int64_t sentSec;
    int64_t sentNsec;
    uint32_t seq;
    uint32_t textLen;
    char text[LOG_TEXT_MAX];
} LogPayload;

typedef struct {
    uint32_t smallSent;
    uint32_t largeSent;
    uint64_t smallNs;
    uint64_t largeNs;
} BenchEndPayload;

#endif
