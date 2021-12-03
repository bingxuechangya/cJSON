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
#include <sys/types.h>
#include <unistd.h>

char *getenv(const char *name)
{
	char *config_fd_str = NULL;
	char *key = NULL;
	char *value = NULL;
	static int fd = -1;
	static FILE *fp = NULL;
	static time_t update_time = 0;
	struct stat attrib;
	char *p = NULL;
	static char* (*original_getenv_func)(const char *) = NULL;

	// https://gist.github.com/Akdeniz/bed92f7efc01e35407130c567dc479ee
	if(!original_getenv_func)
		original_getenv_func = dlsym(RTLD_NEXT, "getenv");

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

	if (fstat(fd, &attrib) < 0) {
		goto end;
	}
	// 如果已经更新过了，就不再更新。
	if (attrib.st_mtime <= update_time) {
		goto end;
	}
	update_time = attrib.st_mtime;

	// fp 指向临时文件，因为可由多个同级任务共用，所以文件为只读。
	if (fp == NULL) {
		fp	= fdopen(fd, "r");
		if (fp == NULL) {
			goto end;
		}
	}

	struct flock fl = {
		// 写锁主要用来保护setenv，否则可以用读锁
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
		p = value;
		if (*p == '"') {
			p++;
			*(p+strlen(p)-1) = 0;
		}
		setenv(key, p, 1);
	}

unlock:
	rewind(fp);
	if (key) free(key);
	if (value) free(value);
	fl.l_type = F_UNLCK;
	fcntl(fd, F_SETLKW, &fl);

end:
	return original_getenv_func(name);
}

// int main(int argc, char *argv[])
// {
//     printf("%s\n", get_conf(argv[1]));
//     return 0;
// }
//
