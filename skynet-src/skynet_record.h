#ifndef skynet_record_h
#define skynet_record_h

#include "skynet_env.h"
#include "skynet.h"

#include <stdio.h>
#include <stdint.h>

#define SKYNET_RECORD_VERSION "1.0.1"

struct record_intque {
	int cap;
	int head;
	int tail;
	int64_t *queue;
};

FILE * skynet_record_open(uint32_t handle);
void skynet_record_close(FILE *f, uint32_t handle);
void skynet_record_output(FILE *f, uint32_t source, int type, int session, void * buffer, size_t sz);
void skynet_record_start(FILE *f, const char* buffer);
void skynet_record_newsession(FILE *f, int session);
void skynet_record_handle(FILE *f, uint32_t handle);
void skynet_record_socketid(FILE *f, int id);
void skynet_record_randseed(FILE *f, int64_t x, int64_t y);
void skynet_record_ostime(FILE *f, uint32_t ostime);
void skynet_record_nowtime(FILE *f, int64_t now);

//parse_do
void skynet_record_parse_open(FILE *f);
void skynet_record_parse_close(FILE *f);
void skynet_record_parse_socket(FILE *f, uint32_t handle);
void skynet_record_parse_output(FILE *f, uint32_t handle);
void skynet_record_parse_newsession(FILE *f);
void skynet_record_parse_handle(FILE *f);
void skynet_record_parse_socketid(FILE *f);
void skynet_record_parse_randseed(FILE *f);
void skynet_record_parse_ostime(FILE *f);
void skynet_record_parse_now(FILE *f);

//队列
struct record_intque * skynet_record_mq_create();
void skynet_record_mq_push(struct record_intque * mq, int64_t v);
int64_t skynet_record_mq_pop(struct record_intque * mq);

#endif