#define LINUX
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
//#include <linux/timer.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h> 
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/kthread.h> 
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/workqueue.h>
#include "mp3_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_14");
MODULE_DESCRIPTION("CS-423 MP3");

#define DEBUG 1
#define DIRECTORY "mp3"
#define FILENAME "status"
#define PAGE_NUM 128
#define PAGE_LENGTH 4096
struct proc_dir_entry * proc_dir;     // proc file directory
struct proc_dir_entry * proc_entry;   // proc file entry

static struct list_head nodeListHead; // head node of list
static struct list_head *ptr, *next;  // pointer used for safe iterate the list
static spinlock_t my_lock;            // spinlock for mutual exclusion
static struct kmem_cache *task_cache; // slab allocator
static long * mem_buffer;
struct mp3_task_struct {              // customized task struct
           struct task_struct *linux_task;        // linux task struct
           int pid;                               // PID number
           unsigned long utilization;
           unsigned long majorFaultCnt;
           unsigned long minorFaultCnt;
           struct list_head list;                 // list data structure
};

static spinlock_t list_lock;                      // for mutual exclusion

int pidnum = 0;                                   // used for read function
unsigned long period, process_time = 0, flags = 0;// used for read function
unsigned int taskCnt = 0;
unsigned int sampleCnt = 0;
unsigned int baseAddr = 0;
static struct delayed_work* delayed_work_queue;
static dev_t node_number;
static struct cdev node_cdev;
int flag = 0;
// function declaration 
void Delete_Node_List(void);
void Registration(int pidnum);
void Unregistration(int pidnum);
void Workqueue_function(struct delayed_work * work);

/* read from kernel to user */
static ssize_t mp3_read (struct file *file, char __user *buffer, size_t count, loff_t *data){
   	struct mp3_task_struct *entry;
    char info[1024]={0};
  	int len = 0,offset = 0;
         	if(*data > 0){
  		return 0;
  	}
  	printk("Reading from Kernel\n");
    spin_lock_irqsave(&list_lock, flags);       // lock the list
  	ptr = &nodeListHead;
  	list_for_each_safe(ptr,next,&nodeListHead){ // traverse list
          entry=list_entry(ptr,struct mp3_task_struct,list); 
          len = sprintf(info + offset, "PID: %d ;\n", entry->pid);
  	      offset += len;
  	}
    spin_unlock_irqrestore(&list_lock, flags);
  	if(offset == 0){                           // if the list is empty
  		len = sprintf(info, "There is no registered process.\n");
      offset += len;  
  	}
  	copy_to_user(buffer, info, offset);        // transfer to user level
  	printk("Read Kernel Complete\n");
  	*data += offset;
  	return offset;                             // 0 means finish reading
}

/* write from user to kernel */
static ssize_t mp3_write (struct file *file, const char __user *buffer, size_t count, loff_t *data){ 
  	char option = ' ';
  	char msg[120]={0};

  	printk(KERN_ALERT "Writing to Kernel\n"); 
  	copy_from_user(msg,buffer,count);
  	sscanf(msg, "%c %d", &option, &pidnum);  // extract command and process pid
    //option = msg[0];
  	printk("PID number: %d \n",pidnum);
  	switch(option)  // option denotes different commands
  	{
    		case 'R':    
               printk("Registration\n");    //registeration
    					 Registration(pidnum);
    				   	 break;
    		case 'U':    
               printk("Unregistration\n"); //deregistration
    					 Unregistration(pidnum);
    				     break;
  	}
  	printk("Write Kernel Complete\n");
  	return count;
}

/* registration function */
void Registration(int pidnum){
    
   	struct mp3_task_struct *temp_task;
    temp_task = kmem_cache_alloc(task_cache, GFP_KERNEL); // slab allocator for memory allocation
  	temp_task -> pid = pidnum;
    temp_task -> linux_task = find_task_by_pid(temp_task->pid);

    spin_lock_irqsave(&list_lock, flags);                 // add task to list
    list_add(&(temp_task->list), &nodeListHead);
    ++taskCnt;
    spin_unlock_irqrestore(&list_lock, flags);

    // If the first process, create work queue;
    if(taskCnt == 1){
      delayed_work_queue = (struct delayed_work*)kmalloc(sizeof(struct delayed_work), GFP_KERNEL);
      INIT_DELAYED_WORK(delayed_work_queue, Workqueue_function);
      schedule_delayed_work(delayed_work_queue, msecs_to_jiffies(0));
    }

    printk("PID %d Registration Complete.\n", pidnum);
}


/* Unregistration function */
void Unregistration(int pidnum){
    struct mp3_task_struct *tmp = NULL;

  	ptr = &nodeListHead;
    spin_lock_irqsave(&list_lock, flags);     // lock the list
   	list_for_each_safe(ptr,next,&nodeListHead) {
   		tmp = list_entry(ptr,struct mp3_task_struct,list); 
      if(tmp->pid == pidnum) {                // find the matched pid process
           	list_del(ptr);                    // delete node
           	kmem_cache_free(task_cache, tmp); // free the cache slab
            printk("PID %d is deregistered.\n", pidnum);
            --taskCnt;
           	break;
      }
    }

    // If the last process, delete work queue;
    if(taskCnt == 0){
      cancel_delayed_work(delayed_work_queue);
      kfree(delayed_work_queue);
    }

    spin_unlock_irqrestore(&list_lock, flags);


    printk("PID %d Unegistration Complete.\n", pidnum);
}

/* work function for delayed_work_queue */
void Workqueue_function(struct delayed_work * work){
   //struct list_head *head;
   struct mp3_task_struct *tmp = NULL;
   unsigned long minorFault, majorFault, utime, stime, utilization;
   unsigned long totalMinorFault = 0, totalMajorFault = 0, totalUtilization = 0;
   
   ptr = &nodeListHead;
   spin_lock_irqsave(&list_lock, flags); 
   list_for_each_safe(ptr,next,&nodeListHead) {
      tmp = list_entry(ptr,struct mp3_task_struct,list); 
      if(get_cpu_use(tmp->pid, &minorFault, &majorFault, &utime, &stime) == 0) {
         utilization = cputime_to_jiffies(utime) + cputime_to_jiffies(stime);// / msecs_to_jiffies(50); // 
         totalMinorFault += minorFault;
         totalMajorFault += majorFault;
         totalUtilization += utilization;
         if(flag == 0 && majorFault > 0){
          printk("major fault cnt > 0 \n");
          flag = 1;
         }
         #ifdef DEBUG
          //printk("%lu %lu %lu\n", utime, stime, utilization);
         #endif
      }else{
         printk("PID %d is not valid anymore.\n", tmp->pid);
      }
   }
   spin_unlock_irqrestore(&list_lock, flags);

   baseAddr = sampleCnt * 4;
   mem_buffer[baseAddr + 0] = (long)jiffies;                        //the Linux kernel variable
   mem_buffer[baseAddr + 1] = totalMinorFault;
   mem_buffer[baseAddr + 2] = totalMajorFault;
   mem_buffer[baseAddr + 3] = totalUtilization;
   sampleCnt = (sampleCnt + 1) % 12000;

   schedule_delayed_work(delayed_work_queue, msecs_to_jiffies(50)); // 20 times per second
   //printk("Execution of workqueue function complete.\n");
}

/* Drive open function */
int drive_open(struct inode *inode, struct file *file){
   return 0;
}

/* Drive close function */
int drive_close(struct inode *inode, struct file *file){
   return 0;
}

/* Drive mmap function*/
int drive_mmap(struct file *file, struct vm_area_struct *vma){
   unsigned long pfn;
   int i;
   for (i = 0; i < PAGE_NUM; i++){
       /* get the physical page address of a virtual page buffer */
       pfn = vmalloc_to_pfn((char *)mem_buffer + i * PAGE_SIZE);
       /* map a virtual page of a user process to a physical page */
       remap_pfn_range(vma, vma->vm_start + i * PAGE_SIZE, pfn, PAGE_SIZE, PAGE_SHARED);
   }
   return 0;
}

/* Define file operation */
static const struct file_operations mp3_file = {                                                
        .owner = THIS_MODULE,                                                                   
        .read = mp3_read,                                                                       
        .write = mp3_write,
};

static const struct file_operations drive_fops = {
        .open = drive_open,
        .release = drive_close,
        .mmap = drive_mmap
};

/* Delete nodes in list funtion */
void Delete_Node_List(void)
{
  struct mp3_task_struct *entry;
  ptr = &nodeListHead;
  list_for_each_safe(ptr,next,&nodeListHead){
    entry = list_entry(ptr,struct mp3_task_struct,list);
    //del_timer(&(entry -> wakeup_timer));
    list_del(ptr);
    kmem_cache_free(task_cache, entry);
  }
}

/*---------------------- Module Initialization -------------------------------*/

// mp3_init - Called when module is loaded
int __init mp3_init(void)
{
   	#ifdef DEBUG
   	printk(KERN_ALERT "MP3 MODULE LOADING\n");
   	#endif
    mem_buffer = (long *)vmalloc(PAGE_NUM * PAGE_SIZE);
    if(!mem_buffer){
      printk("memory buffer allocation failed! \n");
    }
    memset(mem_buffer, 0, PAGE_NUM * PAGE_SIZE); //clear the buffer
   	INIT_LIST_HEAD(&nodeListHead);		           // initialize list
   	printk("Initialize ListHead\n");
   	spin_lock_init(&my_lock);  			             // initialize spin lock
   	printk("Initialize Spin_Lock\n");	
   	proc_dir = proc_mkdir(DIRECTORY, NULL);      // creat proc directory
   	proc_entry = proc_create(FILENAME, 0666, proc_dir, &mp3_file); 
   	printk("Create proc directory\n"); 
   	task_cache = kmem_cache_create("mp3_task_cache", sizeof(struct mp3_task_struct), 0, SLAB_HWCACHE_ALIGN,NULL);


    /* alloc_chrdev_region ( dev_t * dev, unsigned baseminor, unsigned count, const char * name) */
    if(alloc_chrdev_region(&node_number, 0, 1, "node") < 0){ // node_number contains major number, 0 is minor number and 1 is count
        printk("Char device allocation failed.\n");  
    }
    cdev_init(&node_cdev, &drive_fops);
    node_cdev.owner = THIS_MODULE;
    cdev_add(&node_cdev, node_number, 1);
    printk("Initialize Character Device\n"); 

	  printk(KERN_ALERT "MP3 MODULE LOADED\n");
    return 0;   
}

// mp3_exit - Called when module is unloaded
void __exit mp3_exit(void)
{
   #if DEBUG
   printk(KERN_ALERT "MP3 MODULE UNLOADING\n");
   #endif

   kmem_cache_destroy(task_cache); // free cache slab
   Delete_Node_List();
   remove_proc_entry(FILENAME, proc_dir);
   remove_proc_entry(DIRECTORY,NULL);
   vfree(mem_buffer);

   cdev_del(&node_cdev);
   unregister_chrdev_region(node_number, 1);
   printk(KERN_ALERT "MP3 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp3_init);
module_exit(mp3_exit);