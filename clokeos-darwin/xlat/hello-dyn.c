// Dynamisches x86_64-Testprogramm gegen die libSystem des x86_64-Images (siehe xlat/build.sh).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
	printf("hallo dynamisch, argc=%d argv0=%s\n", argc, argv[0]);
	char *p = malloc(100); strcpy(p, "malloc ok"); puts(p); free(p);
	return 5;
}
