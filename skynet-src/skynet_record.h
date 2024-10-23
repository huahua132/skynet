#ifndef skynet_record_h
#define skynet_record_h

#include "skynet_env.h"
#include "skynet.h"

#include <stdio.h>
#include <stdint.h>

#define SKYNET_RECORD_VERSION "1.0.0"

struct record_intque {
	int cap;
	int head;
	int tail;
	int64_t *queue;
};

FILE * skynet_record_open(struct skynet_context * ctx, uint32_t handle);
void skynet_record_close(struct skynet_context * ctx, FILE *f, uint32_t handle);
void skynet_record_output(struct skynet_context *ctx, FILE *f, uint32_t source, int type, int session, void * buffer, size_t sz);
void skynet_record_start(FILE *f, const char* buffer);
void skynet_record_newsession(struct skynet_context *ctx, FILE *f, int session);
void skynet_record_handle(struct skynet_context *ctx, FILE *f, uint32_t handle);
void skynet_record_socketid(struct skynet_context *ctx, FILE *f, int id);
void skynet_record_randseed(struct skynet_context *ctx, FILE *f, int64_t x, int64_t y);
void skynet_record_ostime(struct skynet_context *ctx, FILE *f, uint32_t ostime);
void skynet_record_nowtime(struct skynet_context *ctx, FILE *f, int64_t now);

//队列
struct record_intque * skynet_record_mq_create();
void skynet_record_mq_push(struct record_intque * mq, int64_t v);
int64_t skynet_record_mq_pop(struct record_intque * mq);

#endif