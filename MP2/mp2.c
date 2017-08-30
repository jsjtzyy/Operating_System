#define LINUX
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h> 
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/kthread.h> 
#include "mp2_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_14");
MODULE_DESCRIPTION("CS-423 MP2");

#define DEBUG 1
#define DIRECTORY "mp2"
#define FILENAME "status"
struct proc_dir_entry * proc_dir;   // proc file directory
struct proc_dir_entry * proc_entry; // proc file entry

static struct list_head nodeListHead; // head node of list
static struct list_head *ptr, *next;  // pointer used for safe iterate the list
static spinlock_t my_lock;            // spinlock for mutual exclusion
static struct kmem_cache *task_cache; // slab allocator

struct mp2_task_struct {              // customized task struct
           struct task_struct *linux_task;  // linux task struct
           struct timer_list wakeup_timer;  // timer for each task
           int pid;                         // PID number
           unsigned long period;            // period of task
           unsigned long process_time;      // processing time of task
           unsigned long start_time;        // start time of current period
           int state;                       // 0 - sleep, 1 - ready, 2 - running
           struct list_head list;           // list data structure
};
static struct task_struct *dispatch_thread; // dispatch kernel thread 
static struct task_struct *current_running_task = NULL; // current running task
static spinlock_t list_lock;                // for mutual exclusion

static int schedule_flag = 0;               // flag denotes dispatch thread is alive
int pidnum = 0;                             // used for read function
unsigned long period, process_time = 0, flags = 0;  // used for read function
unsigned long total_ratio = 0;              // used for admission check

// function declaration 
void Delete_Node_List(void);
int admissionCheck(unsigned long period, unsigned long process_time);
void Registration(int pidnum, unsigned long period, unsigned long process_time);
void Yield(int pidnum);
void Deregistration(int pidnum);
int schedule_task(void* dummy);
void wakeup_timer_handler(unsigned long task);
struct mp2_task_struct* get_ready_task(void);
struct mp2_task_struct* get_mp2_task_struct(int pid);

/* read from kernel to user */
static ssize_t mp2_read (struct file *file, char __user *buffer, size_t count, loff_t *data){
   	struct mp2_task_struct *entry;
    char info[1024]={0};
  	int len = 0,offset = 0;
         	if(*data > 0){
  		return 0;
  	}
  	printk("Reading from Kernel\n");
    spin_lock_irqsave(&list_lock, flags);  // lock the list
  	ptr = &nodeListHead;
  	list_for_each_safe(ptr,next,&nodeListHead){ // traverse list
          entry=list_entry(ptr,struct mp2_task_struct,list); 
          len = sprintf(info + offset, "PID: %d, Period: %lu, Process Time: %lu;\n", entry->pid, entry->period, entry->process_time);
  	      offset += len;
  	}
    spin_unlock_irqrestore(&list_lock, flags);
  	if(offset == 0){ // if the list is empty
  		len = sprintf(info, "There is no registered process.\n");
      offset += len;  
  	}
    len = sprintf(info + offset, "Current utilization rate is 0.%lu \n", total_ratio);
    offset += len;
  	copy_to_user(buffer, info, offset); // transfer to user level
  	printk("Read Kernel Complete\n");
  	*data += offset;
  	return offset; // 0 means finish reading
}

/* write from user to kernel */
static ssize_t mp2_write (struct file *file, const char __user *buffer, size_t count, loff_t *data){ 
  	char option = ' ';
  	char msg[120]={0};

  	printk(KERN_ALERT "Writing to Kernel\n"); 
  	copy_from_user(msg,buffer,count);
  	sscanf(msg, "%c %d", &option, &pidnum);  // extract command and process pid
    //option = msg[0];
  	printk("PID number: %d \n",pidnum);
  	switch(option)  // optioin denotes different commands
  	{
    		case 'R':    
               printk("Registration\n");    //registeration
    					 sscanf(&msg[1], "%d %lu %lu", &pidnum, &period, &process_time);
    					 printk("pid: %d, period: %lu, process: %lu\n", pidnum, period, process_time);
    					 Registration(pidnum, period, process_time);
    				   	 break;
    		case 'Y':    
               printk("Yield\n");           //yield
    					 sscanf(&msg[1], "%d", &pidnum);
    					 Yield(pidnum);
    				     break;
    		case 'D':    
               printk("De-registration\n"); //deregistration
    					 sscanf(&msg[1], "%d", &pidnum);
    					 Deregistration(pidnum);
    				     break;
  	}
  	printk("Write Kernel Complete\n");
  	return count;
}

/* registration function */
void Registration(int pidnum, unsigned long period, unsigned long process_time){

   	struct mp2_task_struct *temp_task;

  	if(admissionCheck(period, process_time) >= 0){
    		temp_task = kmem_cache_alloc(task_cache, GFP_KERNEL); // slab allocator for memory allocation
    		temp_task -> pid = pidnum;
    		temp_task -> period = period;
    		temp_task -> process_time = process_time;
    		temp_task -> linux_task = find_task_by_pid(temp_task->pid);
    		// initialize timer
        setup_timer(&temp_task -> wakeup_timer, wakeup_timer_handler, (unsigned long)temp_task);
        temp_task -> state = 0;  // sleep
        temp_task -> start_time = 0;
    		// add into list
    		spin_lock_irqsave(&list_lock, flags);  // maybe this should happen before if(check) to avoid two check pass
    		list_add(&(temp_task->list), &nodeListHead);
      	spin_unlock_irqrestore(&list_lock, flags);
      	printk("Add process to list\n");
  	}else{
  		  printk("Process fails to pass admission check\n");
  	}
}

/* admission check function. return -1 means fail to pass check */
int admissionCheck(unsigned long period, unsigned long process_time){
    unsigned long tmp = 0;
  	printk("Period : %lu, process_time: %lu \n", period, process_time);
  	if(period == 0) return -1;
  	tmp = process_time * 10000 / period;
  	if(total_ratio + tmp < 6930){
  		total_ratio += tmp;
  		return 1;
  	}
  	return -1;
}

/* yield function */
void Yield(int pidnum){
  	unsigned long run_time, sleep_time = 0;
  	struct mp2_task_struct *yield_task;
    yield_task = get_mp2_task_struct(pidnum);
    //compute start point of next period
    if(yield_task -> start_time == 0){
    	run_time = 0;
    }else{
    	run_time = jiffies_to_msecs(jiffies) - yield_task -> start_time;
    }
    //compute sleep time duration
    sleep_time = yield_task -> period - run_time;
    if (sleep_time > 0){
        // force task to Sleep
      	mod_timer(&(yield_task->wakeup_timer), jiffies + msecs_to_jiffies(sleep_time));

      	spin_lock_irqsave(&list_lock, flags);
      	yield_task->state = 0;
      	spin_unlock_irqrestore(&list_lock, flags);

      	set_task_state(yield_task->linux_task, TASK_UNINTERRUPTIBLE); // not interruptable here. Sleep is mandatory
      	schedule(); // enable other process to occupy CPU

      	if (current_running_task != NULL && yield_task->pid == current_running_task->pid){
         	current_running_task = NULL; // if the only process is force to sleep, then current running task is NULL
      	}
   	}
   	wake_up_process(dispatch_thread); // wake up dispatch thread
}

/* deregistration function */
void Deregistration(int pidnum){
    struct mp2_task_struct *tmp = NULL;

  	ptr = &nodeListHead;
    spin_lock_irqsave(&list_lock, flags); // lock the list
   	list_for_each_safe(ptr,next,&nodeListHead) {
   		tmp = list_entry(ptr,struct mp2_task_struct,list); 
      if(tmp->pid == pidnum) {   // find the matched pid process
          	del_timer(&tmp->wakeup_timer); // delete timer
          	total_ratio -= (tmp -> process_time * 10000 / tmp -> period);// update total ratio
           	printk("total_ratio: %lu\n", total_ratio);
            if (current_running_task != NULL &&  tmp->pid == current_running_task->pid){
              	current_running_task = NULL; // update current running task
           	}
           	list_del(ptr); // delete node
           	kmem_cache_free(task_cache, tmp); // free the cache slab
            printk("PID %d is deregistered.\n", pidnum);
           	break;
      }
    }
    spin_unlock_irqrestore(&list_lock, flags);
}

/* timer interrupt handler */
void wakeup_timer_handler(unsigned long task){
    struct mp2_task_struct *wakeup_task = (struct mp2_task_struct*) task;

	  //printk("Timer handler\n");
   	spin_lock_irqsave(&list_lock, flags);
   	wakeup_task->state = 1;  // Ready
   	wakeup_task->start_time = jiffies_to_msecs(jiffies);// update start time
   	spin_unlock_irqrestore(&list_lock, flags);
   	wake_up_process(dispatch_thread); // invoke dispatch thread
}

/* scheduler function */
int schedule_task(void* dummy){
   	struct sched_param sparam;
	  struct mp2_task_struct *new_task, *old_task;

	while(schedule_flag){ // the dispatch thread should keeps alive all the time
      	// Preempt the old task
      	new_task = get_ready_task();
      	if (current_running_task != NULL){
         	old_task = get_mp2_task_struct(current_running_task->pid);
         	sparam.sched_priority = 0;
         	sched_setscheduler(current_running_task, SCHED_NORMAL, &sparam);
      	}else{
         	old_task = NULL;
      	}

      	if (new_task != NULL){
         	if(old_task != NULL && new_task->period < old_task->period && old_task->state == 2) 
         	{
            	spin_lock_irqsave(&list_lock, flags);
            	old_task->state = 1;  // ready
            	spin_unlock_irqrestore(&list_lock, flags);
         	}

         	if (old_task == NULL || new_task->period < old_task->period){
               spin_lock_irqsave(&list_lock, flags);
               new_task->state = 2; //Running
               spin_unlock_irqrestore(&list_lock, flags);

               wake_up_process(new_task->linux_task);
               sparam.sched_priority = 99; // set higher priority for new task
               sched_setscheduler(new_task->linux_task, SCHED_FIFO, &sparam);
               current_running_task = new_task->linux_task;
         	}
      	}

      	#ifdef DEBUG
      	printk("Scheduling...\n");
      	#endif
      	// Make dispatch thread sleep
      	set_current_state(TASK_INTERRUPTIBLE);
      	schedule(); // enable another process to occupy CPU
   }
   return 0;
}

/* get mp2_task_struct function */
struct mp2_task_struct* get_mp2_task_struct(int pid){
	struct mp2_task_struct *res = NULL;
  ptr = &nodeListHead;
	list_for_each_safe(ptr,next,&nodeListHead){ // traverse the list
      	res = list_entry(ptr, struct mp2_task_struct, list);
      	if(res->pid == pid) {
         	return res;
      	}
   	}
   	return res;
}

/* get next ready task function */
struct mp2_task_struct* get_ready_task(void){
    struct mp2_task_struct *ready_task = NULL;
    struct mp2_task_struct *tmp = NULL;
    ptr = &nodeListHead;
    list_for_each_safe(ptr,next,&nodeListHead){
      tmp = list_entry(ptr, struct mp2_task_struct, list);
      // shorter period has higher priority
      if((tmp->state == 1) && (ready_task == NULL || tmp->period < ready_task->period)) {
         ready_task = tmp;
      }
   }
   return ready_task;
}

/* Delete nodes in list funtion */
void Delete_Node_List(void)
{
	struct mp2_task_struct *entry;
  ptr = &nodeListHead;
	list_for_each_safe(ptr,next,&nodeListHead){
		entry = list_entry(ptr,struct mp2_task_struct,list);
		del_timer(&(entry -> wakeup_timer));
		list_del(ptr);
    kmem_cache_free(task_cache, entry);
	}
}

/* Define file operation */
static const struct file_operations mp2_file = {                                                
        .owner = THIS_MODULE,                                                                   
        .read = mp2_read,                                                                       
        .write = mp2_write,
};

/*---------------------- Module Initialization -------------------------------*/

// mp2_init - Called when module is loaded
int __init mp2_init(void)
{
   	#ifdef DEBUG
   	printk(KERN_ALERT "MP2 MODULE LOADING\n");
   	#endif
   	INIT_LIST_HEAD(&nodeListHead);		// initialize list
   	printk("Initialize ListHead\n");
   	spin_lock_init(&my_lock);  			// initialize spin lock
   	printk("Initialize Spin_Lock\n");	
   	proc_dir = proc_mkdir(DIRECTORY, NULL);      // creat proc directory
   	proc_entry = proc_create(FILENAME, 0666, proc_dir, &mp2_file); 
   	printk("Create proc directory\n"); 
   	task_cache = kmem_cache_create("mp2_task_cache", sizeof(struct mp2_task_struct), 0, SLAB_HWCACHE_ALIGN,NULL);
   	dispatch_thread = kthread_create(schedule_task, NULL, "mp2_schedule");
    schedule_flag = 1;
	  printk(KERN_ALERT "MP2 MODULE LOADED\n");
    return 0;   
}

// mp2_exit - Called when module is unloaded
void __exit mp2_exit(void)
{
   #if 1
   printk(KERN_ALERT "MP2 MODULE UNLOADING\n");
   #endif
   schedule_flag = 0; // terminate while loop
   wake_up_process(dispatch_thread);
   kthread_stop(dispatch_thread);  //terminate dispatch thread
   kmem_cache_destroy(task_cache); // free cache slab
   Delete_Node_List();
   remove_proc_entry(FILENAME, proc_dir);
   remove_proc_entry(DIRECTORY,NULL);
   printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp2_init);
module_exit(mp2_exit);