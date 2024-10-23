#include "skynet_record.h"
#include "skynet_timer.h"
#include "skynet.h"
#include "skynet_socket.h"

#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>   // 包含 mkdir 函数的声明
#include <sys/types.h>  // 包含类型定义

int create_dir(struct skynet_context * ctx, const char *path) {
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
skynet_record_open(struct skynet_context * ctx, uint32_t handle) {
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
	FILE *f = fopen(tmp, "w+");
	if (f) {
		uint32_t starttime = skynet_starttime();
		uint64_t currenttime = skynet_now();
		skynet_error(ctx, "Open record file %s", tmp);
        fprintf(f, "%s", SKYNET_RECORD_VERSION);
        size_t len = 4 + 8;
        fwrite(&len, sizeof(len), 1, f);
        fprintf(f, "o");
        fwrite(&starttime, sizeof(starttime), 1, f);
        fwrite(&currenttime, sizeof(currenttime), 1, f);
		fflush(f);
	} else {
		skynet_error(ctx, "Open record file %s fail %s", tmp, strerror(errno));
	}
    
	return f;
}

void
skynet_record_close(struct skynet_context * ctx, FILE *f, uint32_t handle) {
	skynet_error(ctx, "Close record file :%08x", handle);
    uint64_t currenttime = skynet_now();
    size_t len = 8;
    fwrite(&len, sizeof(len), 1, f);
    fprintf(f, "c");
    fwrite(&currenttime, sizeof(currenttime), 1, f);
	fclose(f);
}

static void
record_socket(struct skynet_context *ctx, FILE * f, struct skynet_socket_message * message, size_t sz) {
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

    size_t len = 20 + sz;
     if (sz > SIZE_MAX - 20) {
        skynet_error(ctx, "record msg so large");
        return;
    }
    fwrite(&len, sizeof(len), 1, f);
    fprintf(f, "a");
    fwrite(&message->type, sizeof(message->type), 1, f); 
    fwrite(&message->id, sizeof(message->id), 1, f);
    fwrite(&message->ud, sizeof(message->ud), 1, f);
    fwrite(&ti, sizeof(ti), 1, f);           // 写入 ti
    fwrite(buffer, sz, 1, f);
    fflush(f);
    //skynet_error(NULL, "socket msg >>> len[%u] sz[%u], id[%d], ud[%d] type[%d]", len, sz, message->id, message->ud, message->type);
}

void 
skynet_record_output(struct skynet_context *ctx, FILE *f, uint32_t source, int type, int session, void * buffer, size_t sz) {
    if (type == PTYPE_SOCKET) {
        record_socket(ctx, f, buffer, sz);
        return;
    }
    uint64_t ti = skynet_now();
    size_t len = 20 + sz;
    if (sz > SIZE_MAX - 20) {
        skynet_error(ctx, "record msg so large");
        return;
    }
    fwrite(&len, sizeof(len), 1, f);
    fprintf(f,"m");
    fwrite(&source, sizeof(source), 1, f);   // 写入 source
    fwrite(&type, sizeof(type), 1, f);       // 写入 type
    fwrite(&session, sizeof(session), 1, f); // 写入 session
    fwrite(&ti, sizeof(ti), 1, f);           // 写入 ti
    fwrite(buffer, sz, 1, f);
    fflush(f);
    //skynet_error(ctx, "msg >>> len[%u] sz[%u], source[%u], type[%d] session[%d]", len, sz, source, type, session);
}

void
skynet_record_start(FILE *f, const char* buffer) {
    size_t len = strlen(buffer);
    fwrite(&len, sizeof(len), 1, f);
    fprintf(f,"b");
    fwrite(buffer, len, 1, f);
    fflush(f);
    //skynet_error(NULL, "skynet_record_start >>> len[%d]", len);
}

void 
skynet_record_newsession(struct skynet_context *ctx, FILE *f, int session) {
    size_t len = 4;
    fwrite(&len, sizeof(len), 1, f);
    fprintf(f,"s");
    fwrite(&session, sizeof(session), 1, f); // 写入 session
    fflush(f);
    //skynet_error(ctx, "new_session >>> session[%d]", session);
}

void 
skynet_record_handle(struct skynet_context *ctx, FILE *f, uint32_t handle) {
    size_t len = 4;
    fwrite(&len, sizeof(len), 1, f);
    fprintf(f,"h");
    fwrite(&handle, sizeof(handle), 1, f); // 写入 handle
    fflush(f);
    //skynet_error(ctx, "handle >>> handle[%d]", handle);
}

void 
skynet_record_socketid(struct skynet_context *ctx, FILE *f, int id) {
    size_t len = 4;
    fwrite(&len, sizeof(len), 1, f);
    fprintf(f,"k");
    fwrite(&id, sizeof(id), 1, f); // 写入 id
    fflush(f);
}

void 
skynet_record_randseed(struct skynet_context *ctx, FILE *f, int64_t x, int64_t y) {
    size_t len = 16;
    fwrite(&len, sizeof(len), 1, f);
    fprintf(f,"r");
    fwrite(&x, sizeof(x), 1, f);
    fwrite(&y, sizeof(y), 1, f);
    fflush(f);
}

void 
skynet_record_ostime(struct skynet_context *ctx, FILE *f, uint32_t ostime) {
    size_t len = 4;
    fwrite(&len, sizeof(len), 1, f);
    fprintf(f,"t");
    fwrite(&ostime, sizeof(ostime), 1, f);
    fflush(f);
}

void 
skynet_record_nowtime(struct skynet_context *ctx, FILE *f, int64_t now) {
    size_t len = 8;
    fwrite(&len, sizeof(len), 1, f);
    fprintf(f,"n");
    fwrite(&now, sizeof(now), 1, f);
    fflush(f);
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