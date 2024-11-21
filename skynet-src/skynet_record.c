#include "skynet_record.h"
#include "skynet_timer.h"
#include "skynet.h"
#include "skynet_socket.h"
#include "skynet_server.h"
#include "skynet_mq.h"

#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>   // 包含 mkdir 函数的声明
#include <sys/types.h>  // 包含类型定义

#define PROTOCOL_UDP 1
#define PROTOCOL_UDPv6 2

static inline uint64_t unpackNumberValue(FILE* f, size_t len) {
    uint64_t value;
    if (fread(&value, len, 1, f) != 1) {
        skynet_error(NULL, "read recordfile err");
        return 0;
    }
    return value;
}

static int create_dir(struct skynet_context* ctx, const char *path) {
    char buffer[256];
    char *pos = NULL;
    size_t len;

    // 复制路径并逐级创建
    snprintf(buffer, sizeof(buffer), "%s", path);
    len = strlen(buffer);

    // 寻找路径中的每个分隔符并逐个创建目录
    for (pos = buffer + 1; pos < buffer + len; pos++) {
        if (*pos == '/') {
            *pos = '\0'; // 将分隔符临时替换为空字符
            if (mkdir(buffer, 0777) == -1 && errno != EEXIST) {
                skynet_error(ctx, "Failed to create directory %s: %s\n", buffer, strerror(errno));
                return -1; // 返回错误
            }
            *pos = '/'; // 还原分隔符
        }
    }
    
    // 创建最后一级目录
    if (mkdir(buffer, 0777) == -1 && errno != EEXIST) {
        skynet_error(ctx, "Failed to create directory %s: %s\n", buffer, strerror(errno));
        return -1; // 返回错误
    }

    return 0; // 成功创建
}

FILE * 
skynet_record_open(struct skynet_context* ctx, uint32_t handle) {
	const char * recordpath = skynet_getenv("recordpath");
	if (recordpath == NULL)
		return NULL;

    if (create_dir(ctx, recordpath) != 0) {
        skynet_error(ctx, "Failed to create directory structure for %s", recordpath);
        return NULL;
    }

	size_t sz = strlen(recordpath);
	char tmp[sz + 32];
	sprintf(tmp, "%s/%08x.record", recordpath, handle);
	FILE *f = fopen(tmp, "w+b");
	if (f) {
		uint32_t starttime = skynet_starttime();
		uint64_t currenttime = skynet_now();
		skynet_error(NULL, "Open record file %s", tmp);
        fprintf(f, "%s", SKYNET_RECORD_VERSION);
        fprintf(f, "o");
        fwrite(&starttime, sizeof(starttime), 1, f);
        fwrite(&currenttime, sizeof(currenttime), 1, f);

        skynet_record_add_limit_count(ctx, sizeof(starttime) + sizeof(currenttime) + 1);
	} else {
		skynet_error(ctx, "Open record file %s fail %s", tmp, strerror(errno));
	}
    
	return f;
}

void 
skynet_record_parse_open(FILE *f) {
    uint32_t starttime = (uint32_t)unpackNumberValue(f, 4);
    uint64_t currenttime = unpackNumberValue(f, 8);
    skynet_timer_setstarttime(starttime);
	skynet_timer_setcurrent(currenttime);
    skynet_error(NULL ,"skynet_record_parse_open starttime[%d] currenttime[%llu]\n", starttime, currenttime);
}

void
skynet_record_close(struct skynet_context* ctx, FILE *f, uint32_t handle) {
	skynet_error(ctx, "Close record file :%08x", handle);
    uint64_t currenttime = skynet_now();
    fprintf(f, "c");
    fwrite(&currenttime, sizeof(currenttime), 1, f);
    fflush(f);
	fclose(f);
}

void 
skynet_record_parse_close(FILE *f) {
    uint64_t currenttime = unpackNumberValue(f, 8);
    skynet_error(NULL, "Close Time: %lu", currenttime);
}

static void
record_socket(struct skynet_context* ctx, FILE * f, struct skynet_socket_message * message, size_t sz) {
    uint64_t ti = skynet_now();
    const char *buffer = NULL;
    if (message->buffer == NULL) {
        buffer = (const char *)(message + 1);
        sz -= sizeof(*message);
        const char * eol = memchr(buffer, '\0', sz);
        if (eol) {
            sz = eol - buffer;
        }
    } else {
        sz = message->ud;
        buffer = message->buffer;
    }

    if (message->type == SKYNET_SOCKET_TYPE_UDP) {
        uint8_t protocol = buffer[message->ud];
        if (protocol == PROTOCOL_UDP) {
            sz = message->ud + 1 + 2 + 4;
        } else if (protocol == PROTOCOL_UDPv6) {
            sz = message->ud + 1 + 2 + 16;
        } else {
            skynet_error(NULL, "record_socket err protocol [%d]", protocol);
        }
    }

    fprintf(f, "a");
    fwrite(&message->type, sizeof(message->type), 1, f);
    fwrite(&message->id, sizeof(message->id), 1, f);
    fwrite(&message->ud, sizeof(message->ud), 1, f);
    fwrite(&ti, sizeof(ti), 1, f);
    fwrite(&sz, sizeof(sz), 1, f);
    fwrite(buffer, sz, 1, f);

    skynet_record_add_limit_count(ctx, sizeof(message->type) + sizeof(message->id) + sizeof(message->ud) + sizeof(ti) + sizeof(sz) + sz + 1);
}

void 
skynet_record_parse_socket(FILE *f, uint32_t handle) {
    int type = (int)unpackNumberValue(f, 4);
    int id = (int)unpackNumberValue(f, 4);
    int ud = (int)unpackNumberValue(f, 4);
    uint64_t ti = unpackNumberValue(f, 8);
    size_t bufsz = (size_t)unpackNumberValue(f, 8);
    char *buffer = skynet_malloc(bufsz);
    if (bufsz > 0 && fread(buffer, bufsz, 1, f) != 1) {
        skynet_free(buffer);
        skynet_error(NULL, "Error record socket buffer %d", bufsz);
        return;
    }

    struct skynet_socket_message *sm;
    size_t smsz = sizeof(*sm);
    if (type == SKYNET_SOCKET_TYPE_DATA || type == SKYNET_SOCKET_TYPE_CLOSE || type == SKYNET_SOCKET_TYPE_UDP || type == SKYNET_SOCKET_TYPE_WARNING) {
        
    } else {
        size_t msg_sz = strlen(buffer);
        if (msg_sz > 128) {
            msg_sz = 128;
        }
        smsz += msg_sz;	
    }

    sm = (struct skynet_socket_message *)skynet_malloc(smsz);
    sm->type = type;
    sm->id = id;
    sm->ud = ud;
    if (type == SKYNET_SOCKET_TYPE_DATA || type == SKYNET_SOCKET_TYPE_CLOSE || type == SKYNET_SOCKET_TYPE_UDP || type == SKYNET_SOCKET_TYPE_WARNING) {
        sm->buffer = buffer;
    } else {
        sm->buffer = NULL;
        memcpy(sm+1, buffer, smsz - sizeof(*sm));
        skynet_free(buffer);
    }
    
    struct skynet_message message;
    message.source = 0;
    message.session = 0;
    message.data = sm;
    message.sz = smsz | ((size_t)PTYPE_SOCKET << MESSAGE_TYPE_SHIFT);

    skynet_timer_setcurrent(ti);
    skynet_context_push(handle, &message);
}

void 
skynet_record_output(struct skynet_context* ctx, FILE *f, uint32_t source, int type, int session, void * buffer, size_t sz) {
    if (!skynet_record_check_limit(ctx)) {
        return;
    }

    if (type == PTYPE_SOCKET) {
        record_socket(ctx, f, buffer, sz);
        return;
    }
    uint64_t ti = skynet_now();
    fprintf(f,"m");
    fwrite(&source, sizeof(source), 1, f);
    fwrite(&type, sizeof(type), 1, f);
    fwrite(&session, sizeof(session), 1, f);
    fwrite(&ti, sizeof(ti), 1, f);
    fwrite(&sz, sizeof(sz), 1, f);
    fwrite(buffer, sz, 1, f);

    skynet_record_add_limit_count(ctx, sizeof(source) + sizeof(type) + sizeof(session) + sizeof(ti) + sizeof(sz) + sz + 1);
}

void
skynet_record_parse_output(FILE *f, uint32_t handle) {
    int source = (uint32_t)unpackNumberValue(f, 4);
    int type = (int)unpackNumberValue(f, 4);
    int session = (int)unpackNumberValue(f, 4);
    uint64_t ti = unpackNumberValue(f, 8);
    size_t bufsz = (size_t)unpackNumberValue(f, 8);
    char *buffer = skynet_malloc(bufsz);
    if (bufsz > 0 && fread(buffer, bufsz, 1, f) != 1) {
        skynet_free(buffer);
        skynet_error(NULL, "Error record socket buffer %d", bufsz);
        return;
    }

    struct skynet_message message;
    message.source = source;
    message.session = session;
    message.data = buffer;					
    message.sz = bufsz | ((size_t)type << MESSAGE_TYPE_SHIFT);

    skynet_timer_setcurrent(ti);
    skynet_context_push(handle, &message);
}

void
skynet_record_start(struct skynet_context* ctx, FILE *f, const char* buffer) {
    size_t len = strlen(buffer);
    fprintf(f,"b");
    fwrite(&len, sizeof(len), 1, f);
    fwrite(buffer, len, 1, f);

    skynet_record_add_limit_count(ctx, sizeof(len) + len + 1);
}

void 
skynet_record_newsession(struct skynet_context* ctx, FILE *f, int session) {
    if (!skynet_record_check_limit(ctx)) {
        return;
    }
    fprintf(f,"s");
    fwrite(&session, sizeof(session), 1, f);
    skynet_record_add_limit_count(ctx, 5);
}

void 
skynet_record_parse_newsession(FILE *f) {
    int session = (int)unpackNumberValue(f, 4);
    skynet_record_push_session(session);
}

void 
skynet_record_handle(struct skynet_context* ctx, FILE *f, uint32_t handle) {
    if (!skynet_record_check_limit(ctx)) {
        return;
    }
    fprintf(f,"h");
    fwrite(&handle, sizeof(handle), 1, f);
    skynet_record_add_limit_count(ctx, 5);
}

void 
skynet_record_parse_handle(FILE *f) {
    uint32_t handle = (uint32_t)unpackNumberValue(f, 4);
    skynet_record_push_handle(handle);
}

void 
skynet_record_socketid(struct skynet_context* ctx, FILE *f, int id) {
    if (!skynet_record_check_limit(ctx)) {
        return;
    }
    fprintf(f,"k");
    fwrite(&id, sizeof(id), 1, f);
    skynet_record_add_limit_count(ctx, 5);
}

void 
skynet_record_parse_socketid(FILE *f) {
    int id = (int)unpackNumberValue(f, 4);
    skynet_record_push_socketid(id);
}

void 
skynet_record_randseed(struct skynet_context* ctx, FILE *f, int64_t x, int64_t y) {
    if (!skynet_record_check_limit(ctx)) {
        return;
    }
    fprintf(f,"r");
    fwrite(&x, sizeof(x), 1, f);
    fwrite(&y, sizeof(y), 1, f);
    skynet_record_add_limit_count(ctx, 17);
}

void 
skynet_record_parse_randseed(FILE *f) {
    int64_t x = unpackNumberValue(f, 8);
    int64_t y = unpackNumberValue(f, 8);
    skynet_record_push_mathseek(x, y);
}

void 
skynet_record_ostime(struct skynet_context* ctx, FILE *f, uint32_t ostime) {
    if (!skynet_record_check_limit(ctx)) {
        return;
    }
    fprintf(f,"t");
    fwrite(&ostime, sizeof(ostime), 1, f);
    skynet_record_add_limit_count(ctx, 5);
}

void 
skynet_record_parse_ostime(FILE *f) {
    uint32_t ostime = (uint32_t)unpackNumberValue(f, 4);
    skynet_record_push_ostime(ostime);
}

void 
skynet_record_nowtime(struct skynet_context* ctx, FILE *f, int64_t now) {
    if (!skynet_record_check_limit(ctx)) {
        return;
    }
    fprintf(f,"n");
    fwrite(&now, sizeof(now), 1, f);
    skynet_record_add_limit_count(ctx, 9);
}

void 
skynet_record_parse_now(FILE *f) {
    uint64_t now = unpackNumberValue(f, 8);
    skynet_record_push_nowtime(now);
}

//mq
struct record_intque * 
skynet_record_mq_create() {
    struct record_intque *q = skynet_malloc(sizeof(*q));
    q->cap = 4;
	q->head = 0;
	q->tail = 0;
	q->queue = skynet_malloc(sizeof(int64_t) * q->cap);
    return q;
}

void 
skynet_record_mq_push(struct record_intque * mq, int64_t v) {
	mq->queue[mq->tail] = v;
	mq->tail++;
	if (mq->tail >= mq->cap) {
		mq->tail = 0;
	}

	if (mq->head == mq->tail) {
		int64_t *new_queue = skynet_malloc(sizeof(int64_t) * mq->cap * 2);
		int i;
		for (i = 0; i < mq->cap; i++) {
			new_queue[i] = mq->queue[(mq->head + i) % mq->cap];
		}
		mq->head = 0;
		mq->tail = mq->cap;
		mq->cap *= 2;

		skynet_free(mq->queue);
		mq->queue = new_queue;
	}
}

int64_t 
skynet_record_mq_pop(struct record_intque * mq) {
	if (mq->head != mq->tail) {
		int64_t v = mq->queue[mq->head++];
		int head = mq->head;
		int cap = mq->cap;

		if (head >= cap) {
			mq->head = 0;
		}
		
		return v;
	}
	
	return 0;
}