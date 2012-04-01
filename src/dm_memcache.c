#include "dbmail.h"
//#include <stdio.h>
//#include <sys/types.h>
//#include <libmemcached/memcached.h>
#include "dm_memcache.h"

#define MEMCACHE "Y"
#define MC_SERVER1 "127.0.0.1"

int chkmc(){
	if(MEMCACHE == "Y" || MEMCACHE == "y" || MEMCACHE == "yes" || MEMCACHE == "YES")
		return 1;
	else
		return 0;
}
/*
const char setservers(){
        memcached_server_st *servers = NULL;
        memcached_return rc;
        servers= memcached_server_list_append(servers,"localhost",11211, &rc);
        rc= memcached_server_push(memc, servers);
	return servers;
}
*/

int setmc(char* key, char* value){
	memcached_st * memc = memcached_create(NULL);
	memcached_server_st *servers = NULL;
	memcached_return rc;
	servers= memcached_server_list_append(servers,"localhost",11211, &rc); 
	rc= memcached_server_push(memc, servers);
	rc= memcached_set(memc, key, strlen(key), value,strlen(value), (time_t)0, (uint32_t)0);
	if(rc != MEMCACHED_SUCCESS){
		fprintf(stderr,"Couldn't store key: %s\n",memcached_strerror(memc, rc));
	        memcached_free(memc);
		return 1;
	}

        memcached_free(memc);
	return 0;
}

int setmcmulti(char* keys[10], char* values[10]){
	int z;
	memcached_st * memc = memcached_create(NULL);
	memcached_server_st *servers = NULL;
	memcached_return rc;
	servers= memcached_server_list_append(servers,"localhost",11211,&rc);
	rc= memcached_server_push(memc, servers);
	printf("setmcmulti: before for loop");
	for(z = 0; z < 20; z++){
		if(keys[z] == NULL)
			break;
		printf("setmcmulti: in for loop %s %d %s %d\n",keys[z],strlen(keys[z]),values[z],strlen(values[z]));
		rc=memcached_set(memc,*keys[z],strlen(*keys[z]),*values[z],strlen(*values[z]), (time_t)0,(uint32_t)0);
		if(rc != MEMCACHED_SUCCESS)
			fprintf(stderr,"Couldn't store key: %s\n", memcached_strerror(memc,rc));
	}
	memcached_free(memc);
	return 0;
}

char *getmc(char * key){
	uint32_t flags;
	size_t return_length;
	char *ret;
	memcached_st * memc = memcached_create(NULL);
        memcached_server_st *servers = NULL;
        memcached_return rc;
	servers= memcached_server_list_append(servers,"localhost",11211, &rc);
        rc= memcached_server_push(memc, servers);
	printf("%s - %d \n", key, sizeof(key));
	ret = memcached_get(memc, 
			key, KEY_SIZE,
			&return_length,
			&flags,
			&rc);
	if(rc != MEMCACHED_SUCCESS){
		fprintf(stderr,"Couldnt retrieve key: %s - %s\n",key,memcached_strerror(memc,rc));
		memcached_free(memc);
		return NULL;
	}
	memcached_free(memc);
	return ret;
}

/*
int main(){
	if(chkmc()){
	 char keyencode[];
	 int user_idnr = 10;
	printf("%d \n", (10+sizeof(user_idnr)));
         snprintf(keyencode,50,"dbmencode_%d",user_idnr);
	printf("hello\n");
	printf("%s \n", keyencode);
	printf("%d\n",setmc(keyencode,"some value to cache"));
	printf("%s\n",getmc(keyencode,50));
	}else{
		printf("MEMCACHE not defined\n");
	}
	return 0;
}
*/
