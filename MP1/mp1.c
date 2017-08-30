
#define LINUX
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h> 
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/seq_file.h>  
#include "mp1_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_14");
MODULE_DESCRIPTION("CS-423 MP1");

#define DEBUG 1
#define DIRECTORY "mp1"
#define FILENAME "status"
struct proc_dir_entry * proc_dir;
struct proc_dir_entry * proc_entry;
static struct workqueue_struct *test_workqueue;
static struct list_head nodeListHead;
static struct list_head *ptr, *next;
static struct timer_list my_timer;
static struct work_struct *work;
static spinlock_t my_lock;
struct Node {
        int pidNum;
        unsigned long cpuTime;
        struct list_head list;
};
static char info[1024]={0};
static void do_work(struct work_struct *work);

/* read from kernel to user 
	loff_t  offset of file
	count number of bytes to read
*/
static ssize_t mp1_read (struct file *file, char __user *buffer, size_t count, loff_t *data){
   	struct Node * entry;
	int len = 0,offset = 0;
       	if(*data > 0){
		return 0;
	}
	printk("Reading from Kernel\n");
	spin_lock(&my_lock);
	ptr = &nodeListHead;
	list_for_each_safe(ptr,next,&nodeListHead){
		if(ptr == NULL){
			printk("ptr is NULL\n");
			return -1;
		}                                                       
                entry=list_entry(ptr,struct Node,list); 
		if(entry == NULL){
			printk("entry is NULL\n");
			return -1;
		}
              len = sprintf(info + offset, "PID number: %d, CPU Time: %lu \n", entry->pidNum, entry->cpuTime);
	      offset += len;
	}
	spin_unlock(&my_lock);
	if(offset == 0){
		offset = sprintf(info, "There is no registered process.\n");
	}
	copy_to_user(buffer, info, offset);
	printk("Read Kernel Complete\n");
	*data += offset;
	return offset; // 0 means finish reading
}

/* write from user to kernel */
static ssize_t mp1_write (struct file *file, const char __user *buffer, size_t count, loff_t *data){ 
	struct Node* node;
	char msg[40]={0};
	char *ptr;
        spin_lock(&my_lock); // use spin_lock for mutual exclusion
	node = (struct Node *)kmalloc(sizeof(struct Node *),GFP_KERNEL);
	printk(KERN_ALERT "Writing to Kernel\n"); 
	copy_from_user(msg,buffer,count);
	ptr = msg;
	sscanf(msg, "%d", &node->pidNum);  // extract process pid
	node->cpuTime = 0;
	printk("PID number  %d is written into proc.\n", node->pidNum);
	list_add(&node->list,&nodeListHead);
	spin_unlock(&my_lock);
	printk("Write Kernel Complete\n");
	return count;
}

static const struct file_operations mp1_file = {                                                
        .owner = THIS_MODULE,                                                                   
        .read = mp1_read,                                                                       
        .write = mp1_write,
};

/* invoke when timer expires */
void my_timer_callback( unsigned long data )
{
  work = (struct work_struct *)kmalloc(sizeof(struct work_struct), GFP_KERNEL);
  INIT_WORK(work, do_work); 	 		// initialzie work 
  if (queue_work(test_workqueue, work) == 0) {  //push work into workqueue
      printk("Timer adds work queue failed\n");
  }
  mod_timer( &my_timer, jiffies + 5 * HZ); 	// reactivate timer
}

/* work function for workqueue */
void do_work(struct work_struct *work)
{
	unsigned long cpu_use;
	struct Node * entry;
	spin_lock(&my_lock);
	ptr = &nodeListHead;
	list_for_each_safe(ptr,next,&nodeListHead){ //travers list to update cputime
		entry=list_entry(ptr,struct Node,list);
		if(get_cpu_use(entry->pidNum, &cpu_use) == -1){
			printk("PID %d is being deleted\n", entry->pidNum);
			list_del(ptr);  	    // if process terminates, delete node
			kfree(entry);
		}else{
			entry->cpuTime = cpu_use;   //update cpu time
		}
	}
	spin_unlock(&my_lock);
	kfree((void *)work); 
}

// Clear Node List
static void Delete_Node_List(void)
{
	struct Node * entry;
	list_for_each_safe(ptr,next,&nodeListHead){
		entry=list_entry(ptr,struct Node,list);
		list_del(ptr);
		kfree(entry);
	}
}

// mp1_init - Called when module is loaded
int __init mp1_init(void)
{
   int ret;
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE LOADING\n");
   #endif
   INIT_LIST_HEAD(&nodeListHead);		// initialize list
   printk("Initialize ListHead\n");
   spin_lock_init(&my_lock);  			// initialize spin lock
   printk("Initialize Spin_Lock\n");	
   proc_dir = proc_mkdir(DIRECTORY, NULL);      // creat proc directory
   proc_entry = proc_create(FILENAME, 0666, proc_dir, &mp1_file); 
   printk("Create proc directory\n"); 
   setup_timer( &my_timer, my_timer_callback, 0 ); // initialize timer
   ret = mod_timer( &my_timer, jiffies + 5 * HZ ); // trigger the timer.
   test_workqueue = create_workqueue("my_workqueue");
   printk( "my_timer start jiffies: (%ld).\n", jiffies );
   if (ret) printk("Error in mod_timer\n");  
   printk(KERN_ALERT "MP1 MODULE LOADED\n");
   return 0;   
}

// mp1_exit - Called when module is unloaded
void __exit mp1_exit(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE UNLOADING\n");
   #endif
   remove_proc_entry(FILENAME, proc_dir);
   remove_proc_entry(DIRECTORY,NULL);
   Delete_Node_List();
   del_timer(&my_timer);
   flush_workqueue(test_workqueue);   
   destroy_workqueue(test_workqueue);
   printk(KERN_ALERT "MP1 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp1_init);
module_exit(mp1_exit);
