#include <stdlib.h>
#include <stdio.h>
#include <time.h>

int main(){

	time_t now;
	char *ctime;

	now = time(NULL);
	printf("%ld\n",now/3600);
	return 0;

}
