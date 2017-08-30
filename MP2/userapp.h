#ifndef __USERAPP_INCLUDE__
#define __USERAPP_INCLUDE__

// unsigned int factorial(unsigned int num);
// void do_job(void);
void REGISTER(int PID, unsigned long Period, unsigned long JobProcessTime);
int READ_STATUS(int PID);   // return -1 means not in the list;
void YIELD(int PID); 
void UNREGISTER(int PID);




#endif