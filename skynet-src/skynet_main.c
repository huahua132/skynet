#include "skynet.h"

#include "skynet_imp.h"
#include "skynet_env.h"
#include "skynet_server.h"
#include "skynet_record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <signal.h>
#include <assert.h>
#include <lstring.h>

static int
optint(const char *key, int opt) {
	const char * str = skynet_getenv(key);
	if (str == NULL) {
		char tmp[20];
		sprintf(tmp,"%d",opt);
		skynet_setenv(key, tmp);
		return opt;
	}
	return strtol(str, NULL, 10);
}

static int
optboolean(const char *key, int opt) {
	const char * str = skynet_getenv(key);
	if (str == NULL) {
		skynet_setenv(key, opt ? "true" : "false");
		return opt;
	}
	return strcmp(str,"true")==0;
}

static const char *
optstring(const char *key,const char * opt) {
	const char * str = skynet_getenv(key);
	if (str == NULL) {
		if (opt) {
			skynet_setenv(key, opt);
			opt = skynet_getenv(key);
		}
		return opt;
	}
	return str;
}

static void
_init_env(lua_State *L) {
	lua_pushnil(L);  /* first key */
	while (lua_next(L, -2) != 0) {
		int keyt = lua_type(L, -2);
		if (keyt != LUA_TSTRING) {
			fprintf(stderr, "Invalid config table\n");
			exit(1);
		}
		const char * key = lua_tostring(L,-2);
		if (lua_type(L,-1) == LUA_TBOOLEAN) {
			int b = lua_toboolean(L,-1);
			skynet_setenv(key,b ? "true" : "false" );
		} else {
			const char * value = lua_tostring(L,-1);
			if (value == NULL) {
				fprintf(stderr, "Invalid config table key = %s\n", key);
				exit(1);
			}
			skynet_setenv(key,value);
		}
		lua_pop(L,1);
	}
	lua_pop(L,1);
}

int sigign() {
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGPIPE, &sa, 0);
	return 0;
}

static const char * load_config = "\
	local result = {}\n\
	local function getenv(name) return assert(os.getenv(name), [[os.getenv() failed: ]] .. name) end\n\
	local sep = package.config:sub(1,1)\n\
	local current_path = [[.]]..sep\n\
	local function include(filename)\n\
		local last_path = current_path\n\
		local path, name = filename:match([[(.*]]..sep..[[)(.*)$]])\n\
		if path then\n\
			if path:sub(1,1) == sep then	-- root\n\
				current_path = path\n\
			else\n\
				current_path = current_path .. path\n\
			end\n\
		else\n\
			name = filename\n\
		end\n\
		local f = assert(io.open(current_path .. name))\n\
		local code = assert(f:read [[*a]])\n\
		code = string.gsub(code, [[%$([%w_%d]+)]], getenv)\n\
		f:close()\n\
		assert(load(code,[[@]]..filename,[[t]],result))()\n\
		current_path = last_path\n\
	end\n\
	setmetatable(result, { __index = { include = include } })\n\
	local config_name = ...\n\
	include(config_name)\n\
	setmetatable(result, nil)\n\
	return result\n\
";

//尝试读取并设置lua strseed
static int try_read_strseed(const char* config_file) {
	FILE *f = fopen(config_file, "rb"); // 以二进制读取模式打开文件
    if (f == NULL) {
		fprintf(stderr, "Error opening file: %s", config_file);
        return 1;
    }

	//遍历文件所有行
	char line[1024];
	char recordfile[1024];
	int is_match = 0;
	memset(recordfile, 0, sizeof(recordfile));
	while (fgets(line, sizeof(line), f)) {
		//查找recordfile
		if (strstr(line, "recordfile") != NULL) {
			//读取recordfile
			sscanf(line, "recordfile = [[%[^]]]]", recordfile);
			is_match = 1;
			break;
		}
	}
	fclose(f); // 关闭文件

	if (is_match == 0) {
		return 0;
	}

	//开始从recordfile读取strseed
	f = fopen(recordfile, "rb"); // 以二进制读取模式打开文件
	if (f == NULL) {
		fprintf(stderr, "Error opening record file: %s", recordfile);
		return 1;
	}

	char version[256]; // 存储版本信息
	float pre_progress = 0;
    if (fgets(version, sizeof(SKYNET_RECORD_VERSION), f) == NULL) {
		fprintf(stderr, "read record version err: %s", recordfile);
		return 1;
    }

	if (strcmp(version, SKYNET_RECORD_VERSION) != 0) {
		fprintf(stderr, "record version not same curversion[%s] recordversion[%s]", version, SKYNET_RECORD_VERSION);
		return 1;
	}

	char type;
	if (fread(&type, sizeof(type), 1, f) != 1) {
		fprintf(stderr, "read recordfile open err %s", recordfile);
		return 1;
	}

	if (type != 'o') {
		fprintf(stderr, "recordfile open err %s", recordfile);
		return 1;
	}

	uint32_t starttime = 0;
	if (fread(&starttime, sizeof(starttime), 1, f) != 1) {
		fprintf(stderr, "read recordfile starttime err %s", recordfile);
		return 1;
	}

	uint64_t currenttime = 0;
	if (fread(&currenttime, sizeof(currenttime), 1, f) != 1) {
		fprintf(stderr, "read recordfile currenttime err %s", recordfile);
		return 1;
	}

	uint32_t strseed = 0;
	if (fread(&strseed, sizeof(strseed), 1, f) != 1) {
		fprintf(stderr, "read recordfile strseed err %s", recordfile);
		return 1;
	}

	luaS_set_strseed(strseed);

	return 0;
} 

int
main(int argc, char *argv[]) {
	const char * config_file = NULL ;
	if (argc > 1) {
		config_file = argv[1];
	} else {
		fprintf(stderr, "Need a config file. Please read skynet wiki : https://github.com/cloudwu/skynet/wiki/Config\n"
			"usage: skynet configfilename\n");
		return 1;
	}

	if (try_read_strseed(config_file) != 0) {
		return 1;
	}

	skynet_globalinit();
	skynet_env_init();

	sigign();

	struct skynet_config config;

#ifdef LUA_CACHELIB
	// init the lock of code cache
	luaL_initcodecache();
#endif

	struct lua_State *L = luaL_newstate();
	luaL_openlibs(L);	// link lua lib

	int err =  luaL_loadbufferx(L, load_config, strlen(load_config), "=[skynet config]", "t");
	assert(err == LUA_OK);
	lua_pushstring(L, config_file);

	err = lua_pcall(L, 1, 1, 0);
	if (err) {
		fprintf(stderr,"%s\n",lua_tostring(L,-1));
		lua_close(L);
		return 1;
	}
	_init_env(L);
	lua_close(L);

	config.thread =  optint("thread",8);
	config.module_path = optstring("cpath","./cservice/?.so");
	config.harbor = optint("harbor", 1);
	config.bootstrap = optstring("bootstrap","snlua bootstrap");
	config.daemon = optstring("daemon", NULL);
	config.logger = optstring("logger", NULL);
	config.logservice = optstring("logservice", "logger");
	config.profile = optboolean("profile", 1);
	config.recordfile = optstring("recordfile", "");
	config.recordlimit = optint("recordlimit", 1024 * 1024 * 100);

	skynet_start(&config);
	skynet_globalexit();

	return 0;
}
