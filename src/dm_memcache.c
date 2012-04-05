#include "dbmail.h"
//#include <stdio.h>
//#include <sys/types.h>
//#include <glib.h>
//#include <pthread.h>
#include <libmemcached/memcached.h>
#include "dm_memcache.h"

#define MEMCACHE "Y"
#define MC_SERVER1 "127.0.0.1"
#define MEMCACHE_PRFX "dbm"
#define KEY_SIZE 20
#define MAX_KEYS 20
#define MAX_VALUE 1024
#define VALUE_SIZE 1024
#define KEY_BUFFER
#define DAY_SECONDS 86400
#define THIS_MODULE "memcache"

// GET buffers for memcache
struct memcache_buffer{
	int  elements, action;
	char keys[MAX_KEYS], values[MAX_KEYS], returns[MAX_KEYS]; //action 0 = delete , 1 = get 2 = set
}memcache_buffer;

void memcache_process(void *ptr);
int memcache_check(){
	if(MEMCACHE == "Y" || MEMCACHE == "y" || MEMCACHE == "yes" || MEMCACHE == "YES")
		return 1;
	else
		return 0;
}

char* construct_keyname(char *key, char *postfix){
        char *keyname;
        keyname = g_new0(char,KEY_SIZE); //allocate memory that is freed inmemcache_gstr and memcache_gid

        snprintf(keyname,
                KEY_SIZE,
                "%s%s_%s",
                MEMCACHE_PRFX,
                key,
                postfix);
	
        return keyname;
}

char *memcache_gstr(char *key, int id){
        char *ret, *keyname, keyid[4];
	ret = g_new0(char,VALUE_SIZE);
	keyname = g_new0(char,KEY_SIZE);
	memset(keyid, 0, sizeof(keyid));

        sprintf(keyid,"%d",id);
        keyname= construct_keyname(key,keyid);
        printf("key: %s\n", keyname);
	ret = memcache_get(keyname);
        g_free(keyname);
	g_free(keyid);
	return ret;
}

void memcache_sstr(char *key, int id,char*value){
        char keyid[4];
	memset(keyid, 0, sizeof(keyid));

        sprintf(keyid,"%d",id);
        key = construct_keyname(key,keyid);
        printf("sstr: %s,%d,%s\n",key,id,value);
        memcache_set(key,value);
        g_free(key);
}

int memcache_gint(char*key,char*postfix){
	char *keyuser, *ret;
        int *ret64u;
	keyuser = g_new0(char,KEY_SIZE);
	ret = g_new0(char,VALUE_SIZE);
	ret64u = g_new0(int,4);

        keyuser = construct_keyname(key,postfix);
        ret = memcache_get(keyuser);
        free(keyuser);
                *ret64u=atoi(ret);
	free(ret);	
        return ret64u;

}

void memcache_sint(char*key, int id,char*value){
//	char *keyuser;
	//char *value;
//	value = (char *)malloc(MAX_VALUE*sizeof(char));
//	sprintf(value,"%d",id);
//	keyuser = construct_keyname(key,
}

int memcache_gid(char*username){
	char *keyuser, *ret;
        int *ret64u;
	keyuser=g_new0(char,KEY_SIZE);
	ret=g_new0(char, VALUE_SIZE);
	ret64u=g_new(int,4);
        keyuser = construct_keyname("idnr",username);
        ret64u = atoi(memcache_get(keyuser));
        free(keyuser);
	free(ret);
	return ret64u;
}

void memcache_sid(char*username, int*id){
	char *keyuser, *value;
	keyuser=g_new0(char,KEY_SIZE);
	value=g_new0(char,VALUE_SIZE);
	sprintf(value,"%d",id);
	keyuser = construct_keyname("idnr",username);
	printf("sdasdasdasd");
	memcache_set(keyuser,value);
	g_free(keyuser);	
	g_free(value);
}

int memcache_intreturn(char*string){
	return atoi(string);
}

char *memcache_charreturn(long int number){
	char *buffer;
	buffer = g_new0(char,VALUE_SIZE);
	snprintf(buffer,"%d",number);
	return buffer;
}

char *memcache_gencode(int id){
        return memcache_gstr("encode",id);
}
long int memcache_gmaxmail(int id){
	return strtol(memcache_gstr("maxmail",id),NULL,0);
}

long int memcache_gcurmail(int id){
	return strtol(memcache_gstr("curmail",id),NULL,0);
}

long int *memcache_gmaxseive(int id){
	return strtol(memcache_gstr("maxseive",id),NULL,0);
}

void memcache_smaxmail(int id, long int *value){
	memcache_sstr("maxmail",id,memcache_charreturn(value));
}

void memcache_sencode(int id,char *value){
        memcache_sstr("encode",id,value);
}
void memcache_scurmail(int id, long int value){
	memcache_sstr("curmail",id,memcache_charreturn(value));
}
void memcache_sseive(int id, long int *value){
	memcache_sstr("maxseive",id,memcache_charreturn(value));
}

char* memcache_gpasswd(int id){
	return memcache_gstr("passwd",id);
}

void memcache_spasswd(int id,char *value){
	memcache_sstr("passwd",id,value);
}

void memcache_set(char * key, char * value){
	pthread_t process_t;
	struct memcache_buffer *buffer=memcache_allocate_buffer(1);
	char keys,values;
	buffer->elements = 1;
	buffer->action=2;
	//printf("crash?\n");
//	printf("%s %s",key,value);
	snprintf(buffer->keys[0],KEY_SIZE,"%s",key); 
	snprintf(buffer->values[0],VALUE_SIZE,"%s",value);
	pthread_create(&process_t,NULL,memcache_process,buffer);
}

void memcache_setmulti(char *keys[],char * values[], int id, int size){
	memcache_multi(keys,values,id,size,2);
}
char* memcache_getmulti(char *keys[],char * values[], int id, int size){
	return	memcache_multi(keys,values,id,size,1);
}

char * memcache_multi(char * keys[], char * values[],int id,int size,int action){
        pthread_t process_t;
	struct memcache_buffer *buffer=memcache_allocate_buffer(size);
	int a;
	char *cid;

	cid=g_new0(char,10);
	snprintf(cid,10,"%d",id);
        buffer->elements=size;
	buffer->action=action;

	for(a=0;a<buffer->elements;a++){
		snprintf(buffer->keys[a],KEY_SIZE,"%s",construct_keyname(keys[a],cid));
        	if(buffer->action==2){
				snprintf(buffer->values[a],VALUE_SIZE,"%s",values[a]);
	}

	pthread_create(&process_t,NULL,memcache_process,buffer);

	g_free(cid);

	if(buffer->action==1){
		pthread_join(process_t,NULL);
	        char**ret;
        	*ret=malloc((VALUE_SIZE*size)*sizeof(*ret));
       
		for(a=0;a<buffer->elements;a++)
                	snprintf(ret[a],VALUE_SIZE,"%s",buffer->values[a]);

        	free(buffer);
        	return *ret;
	}
}
	g_free(cid);
	return NULL;
}

char* memcache_get(char * key){
	pthread_t process_t;
        struct memcache_buffer *buffer=memcache_allocate_buffer(1);

	buffer->elements=1;
	pthread_create(&process_t,NULL,memcache_process,buffer);
	pthread_join(process_t,NULL);
	char *values;
	values =malloc((1*VALUE_SIZE)*sizeof(char));
	values = buffer->values;
	free(buffer);
	return values;	
}

struct memcache_buffer* memcache_allocate_buffer(int size){
	struct memcache_buffer *buffer;
	*buffer =(struct memcache_buffer*)malloc(sizeof(struct memcache_buffer));
	return buffer;
}

void memcache_process(void *ptr){
	uint32_t flags;
	size_t return_length;
	int a;
        struct memcache_buffer *buffer = (struct memcache_buffer*)ptr;
        memcached_st * memc = memcached_create(NULL);
        memcached_server_st *servers = NULL;
        memcached_return rc;

		char *ret;
		ret=g_new0(char,VALUE_SIZE);
	

        servers= memcached_server_list_append(servers,"localhost",11211, &rc);
        rc= memcached_server_push(memc, servers);
        for(a=0;a<buffer->elements;a++){
		switch(buffer->action){
		case 0:
		  rc= memcached_delete(memc,
			buffer->keys[a], KEY_SIZE,
			(time_t)0);
			break;
		case 1:
		  ret = memcached_get(memc,
                        buffer->keys[a], KEY_SIZE,
                        &return_length,
                        &flags,
                        &rc);

                        snprintf(buffer->values[a],VALUE_SIZE,"%s",ret);
			break;
		case 2:
                printf("%d - %d\n",buffer->action,buffer->keys[a]);  
		rc= memcached_set(memc,
                  	buffer->keys[a],
                  	strlen(buffer->keys[a]),
                  	buffer->values[a],strlen(buffer->values[a]),
                  	(time_t)0, (uint32_t)0);
         		break;
		}
                  if(rc != MEMCACHED_SUCCESS)
                        fprintf(stderr,"Error: %s\n",memcached_strerror(memc,rc));
                
	}
        	g_free(ret);

	g_free(buffer);
        memcached_free(memc);
}

