#include "userapp.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>

static unsigned long input = 0;
unsigned long factorial(unsigned long num);
void do_job(void);

int main(int argc, char* argv[]){ 
    struct timeval start_time, end_time;
    int pidNum, cnt = 10;
    unsigned long res = 0;
    unsigned long JobProcessTime = 0, Period = 2000;
    unsigned long microsecond = 0, second = 0;

    printf("userapp\n");
    pidNum = getpid(); // get the pid number of current process.
    printf("The pid number \"%d\" process is generated\n", pidNum);
    printf("please input value (10 ~ 5000): \n");
    scanf("%lu", &input);
    printf("please input period (1800 ~ ) ms: \n");
    scanf("%lu", &Period);
    printf("please input iteration times: \n");
    scanf("%d", &cnt);
    input = input * 20000;
    printf("Start evaluating factorial computation time ...\n");
    gettimeofday(&start_time, NULL);
    factorial(input);   
    gettimeofday(&end_time, NULL);
    //compute the processing time in terms of ms
    JobProcessTime = (end_time.tv_sec - start_time.tv_sec) * 1000 + (end_time.tv_usec - start_time.tv_usec) / 1000;
    printf("The process_time is %lu ms, period is %lu ms. \n", JobProcessTime, Period);

#if 1
    REGISTER(pidNum, Period, JobProcessTime);
    if(READ_STATUS(pidNum) < 0) {
      printf("Process registration not verified. \n");
      return -1;
    }


    YIELD(pidNum);
    while(cnt > 0)
    {
      do_job();       //do factorial computation job
      YIELD(pidNum);  //yield after computation
      --cnt;
    }
    printf("user application is terminating...\n");	
    UNREGISTER(pidNum); //Unregister current process in the end
#endif
    return 0;
}

/*
    READ_STATUS function
    Check if process is admitted.
*/
int READ_STATUS(int PID){
    FILE *fd_read = NULL;
    int tmp = 0;
    int res = -1;

    printf("Checking if process is registered ...\n");
    fd_read = fopen("/proc/mp2/status", "r");
    if(NULL == fd_read)
    {
        printf("Open proc file failed. \n");
        return -1;
    }
    while(fscanf(fd_read, "PID: %d", &tmp)){ // traverse all admitted processes in list
      if(tmp == PID) {
          printf("Process registration verified.\n");
          res = 0;
          break;
      }
    }
    fclose(fd_read);
    return res;  // if cannot find current PID, return -1;
}

/*
    REGISTER function
*/
void REGISTER(int PID, unsigned long Period, unsigned long JobProcessTime){
    char user_buf[80]={0};
    printf("Start registering ...\n");
    printf("Period: %lu, JobProcessTime: %lu \n", Period, JobProcessTime);
    // write itself pid, period and process_time into proc file
    sprintf(user_buf, "echo %c %d %lu %lu >/proc/mp2/status", 'R', PID, Period, JobProcessTime);  
    system(user_buf);
    printf("Registration completed\n");
}

/*
    YIELD function
*/
void YIELD(int PID){
    char user_buf[80]={0};
    printf("Start yielding ...\n");

    // write yielding process pid into proc file
    sprintf(user_buf, "echo %c %d  >/proc/mp2/status", 'Y', PID);  
    system(user_buf);
    printf("Yield completed\n");
}

/*
    UNREGISTER function
*/
void UNREGISTER(int PID){
    char user_buf[80]={0};
    printf("Start unregistering ...\n");

    // write unregistering process pid into proc file
    sprintf(user_buf, "echo %c %d  >/proc/mp2/status", 'D', PID);  
    system(user_buf);
    printf("Unregistration completed\n");
}

/*
    factorial function
*/
unsigned long factorial(unsigned long num){
  unsigned long res = 0;
  if(num < 0) printf("The input is negative. Compute abort.\n");
  if(num == 0) return 1;
  for(int i = 0; i < num; ++i){
    for(int j = 0; j < 10; ++j){

    }// do nothing;
  }
  while(num >= 1){
    res *= num;
    num--;
  }
  return res;
}

/*
    wrapper function
*/
void do_job(void){
  factorial(input);
}
