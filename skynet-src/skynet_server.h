#ifndef SKYNET_SERVER_H
#define SKYNET_SERVER_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

struct skynet_context;
struct skynet_message;
struct skynet_monitor;

struct skynet_context * skynet_context_new(const char * name, const char * parm);
void skynet_context_grab(struct skynet_context *);
void skynet_context_reserve(struct skynet_context *ctx);
struct skynet_context * skynet_context_release(struct skynet_context *);
uint32_t skynet_context_handle(struct skynet_context *);
int skynet_context_push(uint32_t handle, struct skynet_message *message);
void skynet_context_send(struct skynet_context * context, void * msg, size_t sz, uint32_t source, int type, int session);
int skynet_context_newsession(struct skynet_context *);
struct message_queue * skynet_context_message_dispatch(struct skynet_monitor *, struct message_queue *, int weight);	// return next queue
int skynet_context_total();
void skynet_context_dispatchall(struct skynet_context * context);	// for skynet_error output before exit

void skynet_context_endless(uint32_t handle);	// for monitor

void skynet_globalinit(void);
void skynet_globalexit(void);
void skynet_initthread(int m);

void skynet_profile_enable(int enable);

//record
FILE* skynet_context_recordfile(struct skynet_context * context);

void skynet_record_push_session(int session);
int skynet_record_pop_session();
void skynet_record_push_handle(uint32_t handle);
uint32_t skynet_record_pop_handle();
void skynet_record_push_socketid(int id);
int skynet_record_pop_socketid();
void skynet_record_push_mathseek(int64_t x, int64_t y);
int64_t skynet_record_pop_mathseek();
void skynet_record_push_ostime(uint32_t ostime);
uint32_t skynet_record_pop_ostime();
void skynet_record_push_nowtime(int64_t ostime);
int64_t skynet_record_pop_nowtime();

int skynet_record_check_limit(struct skynet_context * ctx);
void skynet_record_add_limit_count(struct skynet_context * ctx, size_t len);

#endif
