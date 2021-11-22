#include <stdio.h>
#include <stdlib.h>

char *getenv_test(char *name)
{
	printf("%s: %s\n", name, getenv(name));
}

int main(int argc, char *argv[])
{
	printf("getenv_test:\n");
	getenv_test(".files[0].filename");
	getenv_test(".files[0].changes");
	getenv_test(".files[1].filename");
	getenv_test(".files[1].changes");
	getenv_test(".files[2].filename");
	getenv_test(".files[2].changes");
	getenv_test(".commit.author.name");
	getenv_test(".commit.author.email");
	return 0;
}
