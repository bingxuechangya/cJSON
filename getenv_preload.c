#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>

char *getenv(const char *name)
{
	char *config_fd_str = NULL;
	char *key = NULL;
	char *value = NULL;
	static char cache[256];
	static int fd = -1;
	static FILE *fp = NULL;
	static char* (*original_getenv_func)(const char *) = NULL;

	// https://gist.github.com/Akdeniz/bed92f7efc01e35407130c567dc479ee
	if(!original_getenv_func)
		original_getenv_func = dlsym(RTLD_NEXT, "getenv");

	memset(cache, 0, sizeof(cache));
	if (name == NULL) goto end;

	if (fd < 0) {
		config_fd_str = original_getenv_func("CONFIG_FILE_NO");
		if (!config_fd_str) {
			goto end;
		}
		fd = atoi(config_fd_str);
		if (fd < 2) {
			goto end;
		}
	}
	if (fp == NULL) {
		fp	= fdopen(fd, "r");
		if (fp == NULL) {
			goto end;
		}
	}

	struct flock fl = {
		.l_type = F_WRLCK,
		// 下面3个指明锁区域
		.l_whence = SEEK_SET,
		.l_start = 0,
		.l_len = 0,
		// 对所有进程上锁
		.l_pid = 0
	};
	fcntl(fd, F_SETLKW, &fl);
	key = calloc(1, 256);
	value = calloc(1, 256);
	if ((key == NULL) || (value == NULL)) {
		goto unlock;
	}

	rewind(fp);

	while (fscanf(fp, "%256[^=]=%256s\n", key, value) == 2) {
		if (strcmp(key, name)) {
			continue;
		}
		if (value[0] == '"') {
			memcpy(cache, value+1, strlen(value)-2);
		} else {
			memcpy(cache, value, strlen(value));
		}
		goto unlock;
	}

unlock:
	rewind(fp);
	if (key) free(key);
	if (value) free(value);
	fl.l_type = F_UNLCK;
	fcntl(fd, F_SETLKW, &fl);

end:
	// 如果从头tmpfile里没拿到配置，那么就从环境变量里取
	if (strlen(cache) == 0) {
		value = original_getenv_func(name);
		if (value) {
			strncpy(cache, value, sizeof(cache));
		}
	}
	return cache;
}

// int main(int argc, char *argv[])
// {
//     printf("%s\n", get_conf(argv[1]));
//     return 0;
// }
//
