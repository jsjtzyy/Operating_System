#include "userapp.h"
#include<stdio.h>
#include<string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

/* self-defined function. Execution for test */
void foo_1(int random){
    int num = 0;
    for(int i = 0; i < random; i++){
	for(int j = 0; j < 20000; j++){
		num = (num + 1) % 200;
	}
    }
   printf("foo_1 is done. \n");
}

/*
   -----------self-defined recursion function.----------- 
   compute how many solutions to go upstairs.
   Suppose each time you are allowed 1 or 2 steps.
   The stair has num steps. 
*/
unsigned int recursion(unsigned int num){
  if(num == 1) return 1;
  if(num == 2) return 2;
  return (recursion(num - 1) + recursion(num - 2)) % 1000000000;
}

int main(int argc, char* argv[]){   
    char user_buf[50]={0};
    int pidNum;
    unsigned long input = 0, res = 0;
    printf("userapp\n");
    pidNum = getpid(); // get the pid number of current process.
#if 1
    printf("userapp starts to open proc file\n");
    printf("write pid number \"%d\" to proc_entry\n", pidNum);
    
    sprintf(user_buf, "echo '%d'>/proc/mp1/status", pidNum);  // write itself pid into proc file
    system(user_buf);
#endif
    srand((unsigned)time(0)); 
    printf("user application is running...\n");	
    input = (unsigned int)(rand()%20 + 35);
    res = recursion(input);
    sleep(15);
    printf("The total solution is %lu.\n User application terminated.\n", res);
    return 0;
}
