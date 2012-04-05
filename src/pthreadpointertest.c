
#include <stdio.h>
#include <pthread.h>

 struct buffer{
         int a;
         char *string[];
 }buffer;

 void thread1_function(void *ptr){
         struct buffer *buffer=(struct buffer*)ptr;

         printf("hello world\n");

         printf("%s-%d\n", buffer->string[1],buffer->a);

 }

 int main(){

        struct buffer *buffer;
        int err;
        buffer = (struct buffer*)malloc((11*sizeof(char))+sizeof(int));
        //memset(buffer->a,0,(sizeof(int)));
	pthread_t thread1;
        buffer->string[0]="hello";
	buffer->string[1]="world";
	//sprintf(buffer->string,"%s","strint");
        buffer->a=1;
        printf("main: %s - %d\n",buffer->string[0],buffer->a);
        err = pthread_create(&thread1, NULL, thread1_function, buffer);
        printf("error: %d\n",err);
        pthread_join(thread1,NULL);
        free(buffer);
	return 0;
 }

