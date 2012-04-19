#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <xmlrpc-c/base.h>
#include <xmlrpc-c/client.h>

#include "config.h"  /* information about this build environment */
#include <string.h>
#define NAME "MultiRPC Client"
#define VERSION "1.0"

#define MAJORITY 0
#define ANY 1
#define ALL 2

#define ASYNC 0
#define SYNC 1

#define MAX 100

int ressync[3][2], resasync[3][2];
int syn;
int sig=0;
int alltrack[9999];
int allvalarray[9999][MAX];
int ct[2], num_wait=0;
int num_servers;
char * server_list[1000];
char servers[100][1000];

struct thread_data {
  char **server_list;
  void (*callback) (void);
  int tid;
  int num_servers;
};

struct request {
  char * server_name;
  int tid;
};

int value, num_responses, option, num_req;
pthread_mutex_t result_mutex;
pthread_cond_t client_satisfied;
int track=0;

void* MakeRequest(void* request_data);
void* client_start_sync(void* params);
void client_callback(void);
void client_start_async(pthread_t* t, void* params);

static void 
die_if_fault_occurred (xmlrpc_env * const envP) {
  if (envP->fault_occurred) {
    fprintf(stderr, "Something failed. %s (XML-RPC fault code %d)\n",
	    envP->fault_string, envP->fault_code);
    exit(1);
  }
}

void client_callback(void)
{
  if(value != -1){
  if(syn)ressync[option][value]++;
  else resasync[option][value]++;
  }
}

void client_start_async(pthread_t* t, void* params)
{ syn=0;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
      
  pthread_create(t, &attr, client_start_sync, params);
  pthread_attr_destroy(&attr);
}

void* client_start_sync(void* params)  
{ 
  struct thread_data* input;
  char ** server_list;
  void (*callback) (void);
  int num_servers,tid;
  
  input = (struct thread_data *)params;
  server_list = input->server_list;
  callback = input->callback;
  tid = input->tid;
  
  num_servers = input->num_servers;
  pthread_t threads[num_servers];
  pthread_attr_t attr;
  int rc;
  long t;
    
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  pthread_attr_setstacksize(&attr, 1000000);
  value = 0;
  num_responses = 0;
  pthread_mutex_init(&result_mutex, NULL);
  pthread_cond_init(&client_satisfied, NULL);

  struct request request_data;
  request_data.tid = tid;
  

  for(t = 0; t < num_servers; t++) {
    request_data.server_name = server_list[t];
    rc = pthread_create(&threads[t], &attr, 
			MakeRequest, (void *)&request_data);
   
    
    if(rc) {
      fprintf(stderr, "ERROR: Return code from pthread_create() is %d. Stack size exceeded, reduce the number of requests and try again\n", rc);
      exit(1);
    }     
  }
  pthread_attr_destroy(&attr);
  
  pthread_mutex_lock(&result_mutex);
  pthread_cond_wait(&client_satisfied, &result_mutex);
  sig++;
  /* run callback */
  (*callback)(); 

  
  pthread_mutex_destroy(&result_mutex);
  pthread_cond_destroy(&client_satisfied);

  return 0;
}

static void 
handle_sample_add_response(const char *   const server_url ATTR_UNUSED,
                           const char *   const method_name ATTR_UNUSED,
                           xmlrpc_value * const param_array,
                           void *         const user_data ATTR_UNUSED,
                           xmlrpc_env *   const faultP,
                           xmlrpc_value * const resultP) {

  xmlrpc_env env;
  xmlrpc_int tid;
  int l;
    
  /* Initialize our error environment variable */
  xmlrpc_env_init(&env);

  
  xmlrpc_decompose_value(&env, param_array, "(i)", &tid);
  die_if_fault_occurred(&env);
            
  if (faultP->fault_occurred)
    printf("The RPC failed.  %s", faultP->fault_string);
  else {
    xmlrpc_int res;
    int ns=num_servers;
    xmlrpc_read_int(&env, resultP, &res);
    die_if_fault_occurred(&env);

    pthread_mutex_lock(&result_mutex);
    num_responses++;
    
    

    if(option == MAJORITY) {
      value = res;
      if(num_responses >= ns/2 + 1){	
	pthread_cond_signal(&client_satisfied);
	}
	  
    }  else if(option == ANY) { 
      //We have already received a response, so ANY is satisfied
       value = res;
       pthread_cond_signal(&client_satisfied);

    } else { /*All or an invalid option*/
        allvalarray[tid][alltrack[tid]++] = res; 
        value = -1;
        if(num_responses >= ns){
	
        for(l=0; l<alltrack[tid]; l++)
        {
         if(allvalarray[tid][l] == 0) ct[0]++;
         else ct[1]++;
        }
        if(ct[0] == ns) value = ct[1]; 
        if(ct[1] == ns) value = ct[0]; 
        
        for(l=0; l<2; l++) ct[l] = 0;
        pthread_cond_signal(&client_satisfied);
        
        }
    }

    pthread_mutex_unlock(&result_mutex);
  }
}

void* MakeRequest(void* request_data)
{
  int tid;
  char* name;
  xmlrpc_env env;
  xmlrpc_client * clientP;
  char * const methodName = "sample.add";
  struct request* data;

  data = (struct request *)request_data;
  name = data->server_name;
  tid = data->tid;
  
  
  /* Initialize our error environment variable */
  xmlrpc_env_init(&env);
   
  /* Required before any use of Xmlrpc-c client library: */
  xmlrpc_client_setup_global_const(&env);
  die_if_fault_occurred(&env);

  xmlrpc_client_create(&env, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, NULL, 0,
		       &clientP);
  die_if_fault_occurred(&env);

  xmlrpc_client_start_rpcf(&env, clientP, name, methodName,
			   handle_sample_add_response, NULL,
			   "(i)", (xmlrpc_int32) tid);
  die_if_fault_occurred(&env);

  /* Wait for all RPCs to be done. 
  */
  xmlrpc_client_event_loop_finish(clientP);
   
  xmlrpc_client_destroy(clientP);
   
  pthread_exit(NULL);
}

int main(int argc, char** argv) 
{FILE *fp = fopen("num_servers", "r");
  if(!fp){printf("Service not started.\n"); exit(-1);}
  char ns[10]; int t=0, start=8080;
  while(!feof(fp)){ns[t++] = fgetc(fp);}
  ns[--t] = '\0';
  fclose(fp);
  num_servers = atoi(ns);

  char *str1 = "http://localhost:";
  char *str2 = "/RPC2";



  if (argc <  2) {
    fprintf(stderr,
            "Usage: xmlrpc_client"
            " [num_requests]\n");

    exit(1);
  }

  int i, j;
  pthread_t thread_id1, thread_id2, thread_id3;
  struct timeval tv[2];
  float time;
  int tid=0;
  
  for(i = 0; i < num_servers; i++) {
    sprintf(servers[i], "%s%d%s",str1, i+start, str2);
    server_list[i] = servers[i];
  }



  num_req = atoi(argv[argc-1]);
  
  struct thread_data input;
  input.num_servers = num_servers;
  input.server_list = server_list;
  input.callback = &client_callback;
  


  option = 0;
  syn=1;
  gettimeofday(&tv[0],NULL);
  for(j=0; j<num_req; j++){
    //printf("The client has made a blocking request with MAJORITY semantics.\n");
    input.tid = tid++;
    client_start_sync((void*)&input);
   }
  gettimeofday(&tv[1],NULL);
  time = (float)(((tv[1].tv_sec * 1000) + (tv[1].tv_usec/1000)) - ((tv[0].tv_sec * 1000) + (tv[0].tv_usec/1000)))/(float)num_req;
  while(sig != num_req) {}
  printf("3|%d|%d|%d|1|%f\n", ressync[0][0], ressync[0][1], num_req - (ressync[0][0] + ressync[0][1]), time);
  ressync[0][0]=0; ressync[0][1]=0;
  
  gettimeofday(&tv[0],NULL);
  for(j=0; j<num_req; j++){input.tid=tid++;
    client_start_async(&thread_id1, (void*)&input); 
    //printf("The client has made a nonblocking request with MAJORITY semantics.\n");
    pthread_join(thread_id1, NULL);
    } 
  gettimeofday(&tv[1],NULL);
  time = (float)(((tv[1].tv_sec * 1000) + (tv[1].tv_usec/1000)) - ((tv[0].tv_sec * 1000) + (tv[0].tv_usec/1000)))/(float)num_req;
  while(sig != 2*num_req) {}
  printf("3|%d|%d|%d|2|%f\n",resasync[0][0], resasync[0][1], num_req - (resasync[0][0] + resasync[0][1]), time);  
  resasync[0][0]=0; resasync[0][1]=0;
  
  option = 1;
  syn=1;
  gettimeofday(&tv[0],NULL);
    for(j=0; j<num_req; j++){
    //printf("The client has made a blocking request with ANY semantics.\n");
    input.tid=tid++;
    client_start_sync((void*)&input);
    }
  gettimeofday(&tv[1],NULL);
  time = (float)(((tv[1].tv_sec * 1000) + (tv[1].tv_usec/1000)) - ((tv[0].tv_sec * 1000) + (tv[0].tv_usec/1000)))/(float)num_req;
   while(sig != 3*num_req) {}    
   printf("2|%d|%d|%d|1|%f\n", ressync[1][0], ressync[1][1], num_req - (ressync[1][0] + ressync[1][1]), time);
   ressync[1][0]=0; ressync[1][1]=0;
   
  gettimeofday(&tv[0],NULL);
  for(j=0; j<num_req; j++){input.tid = tid++;
    client_start_async(&thread_id2, (void*)&input); 
    //printf("The client has made a nonblocking request with ANY semantics.\n");
    pthread_join(thread_id2, NULL);
    } 
  gettimeofday(&tv[1],NULL);
  time = (float)(((tv[1].tv_sec * 1000) + (tv[1].tv_usec/1000)) - ((tv[0].tv_sec * 1000) + (tv[0].tv_usec/1000)))/(float)num_req;
   while(sig != 4*num_req) {}
   printf("2|%d|%d|%d|2|%f\n", resasync[1][0], resasync[1][1], num_req - (resasync[1][0] + resasync[1][1]), time);  
   resasync[1][0]=0; resasync[1][1]=0;		
   
  option = 2;
  syn=1;
  gettimeofday(&tv[0],NULL);
    for(j=0; j<num_req; j++){
    //printf("The client has made a blocking request with ALL semantics.\n");
    input.tid = tid++;
    client_start_sync((void*)&input);
    }
  gettimeofday(&tv[1],NULL);
  time = (float)(((tv[1].tv_sec * 1000) + (tv[1].tv_usec/1000)) - ((tv[0].tv_sec * 1000) + (tv[0].tv_usec/1000)))/(float)num_req;
    while(sig != 5*num_req) {}
    printf("1|%d|%d|%d|1|%f\n", ressync[2][0], ressync[2][1], num_req - (ressync[2][0] + ressync[2][1]), time);
    
  gettimeofday(&tv[0],NULL);
  for(j=0; j<num_req; j++){input.tid = tid++;
    client_start_async(&thread_id3, (void*)&input); 
    //printf("The client has made a nonblocking request with ALL semantics.\n");
    pthread_join(thread_id3, NULL);
    } 
  gettimeofday(&tv[1],NULL);
  time = (float)(((tv[1].tv_sec * 1000) + (tv[1].tv_usec/1000)) - ((tv[0].tv_sec * 1000) + (tv[0].tv_usec/1000)))/(float)num_req;
    while(sig !=6*num_req) {}
    printf("1|%d|%d|%d|2|%f\n",resasync[2][0], resasync[2][1], num_req - (resasync[2][0] + resasync[2][1]), time);  
    resasync[2][0]=0; resasync[2][1]=0;
    
     
  return 0;
}
