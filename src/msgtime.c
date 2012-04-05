#include <time.h>
#include <stdio.h>
#include <string.h>
#include <libmemcached/memcached.h>
char *substr(char *string, int position, int length) 
{
   char *pointer;
   int c;
 
   pointer = malloc(length+1);
 
   if( pointer == NULL )
   {
      printf("Unable to allocate memory.\n");
      exit(0);
   }
 
   for( c = 0 ; c < position -1 ; c++ ) 
      string++; 
 
   for( c = 0 ; c < length ; c++ ) 
   {
      *(pointer+c) = *string;      
      string++;   
   }
 
   *(pointer+c) = '\0';
 
   return pointer;
}

int main(){

	time_t cur_time;
	char *time_str;
	char *prefix;
	prefix = "dbm:l";
	long msg_id = '21345543234';
	char *msg_keyname;
	time_str = malloc(sizeof(char));
	msg_keyname = malloc(sizeof(char));
	cur_time = time(NULL);

	sprintf(time_str,"%ld",cur_time/3600);
	time_str = substr(time_str, 5,strlen(time_str));
	sprintf(msg_keyname,"%s_%ld-%s",prefix,msg_id,time_str);
	
	printf("mesg keyname: %s\n",msg_keyname);
	
	const char *config_string="--SERVER=127.0.0.1:11211";

	memcached_st *memc = memcached(config_string, strlen(config_string));
	char *keys;
	size_t key_length;
	memcached_return *rc;
	int a;
	rc=memcached_dump_fn(memc,keys,key_length,(void*)0);
	for(a=0;a<key_length;a++){
		printf("%s\n",keys[a]);
	}
	return 0;
}
	
