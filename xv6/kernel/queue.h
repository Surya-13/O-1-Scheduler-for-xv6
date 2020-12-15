#include "proc.h"
#include "defs.h"

// The type - 'queue'
struct Queue{
    // Variables to store the start, end and size of the queue
    // which is being emulated by using an array
	int front, rear, size;              

    // The array of struct proc * 's.                
	struct proc* array[100];                            
};

// The type of list of 40 queues
struct queue_list{
  // To acquire and release the locks for the queue list.
  struct spinlock lock;   

  // The list of 40 queues.                              
  struct Queue Q[40];    

  // Bitmask, to store the indices having non-empty queues.                                
  uint64 bitmask;                                       
};

void createQueue(struct Queue* q) 
{ 
    // Front of the queue is the 0th index intitally.
    q->front = 0;  

    // Initial size of the queue is 0.                                     
    q->size = 0;      

    // Rear of the queue would be the last index i.e, 99.                                  
    q->rear = 99;   

    return;
} 

int isEmpty(struct Queue* queue) 
{ 
    // Size is 0 => queue is empty. 
    return (queue->size == 0);                          
} 
  

void insert(struct Queue* queue, struct proc* item) 
{ 
    // Rear points to the last item, hence increment it.
    queue->rear = (queue->rear + 1)%100;       

    // Insert at that new rear position.         
    queue->array[queue->rear] = item;             

    // Size increases by 1.      
    queue->size++;     

    return;                                 
} 
  
struct proc* del(struct Queue* queue) 
{ 
    // Get the first process inserted, by accsessing the front index.
    struct proc* item = queue->array[queue->front];   

    // Updated front index will be the next index.  
    queue->front = (queue->front + 1)%100;       

    // Size decreases by 1.       
    queue->size--;                

    // Return the first process.                      
    return item;                                        
} 

struct proc* front(struct Queue* queue) 
{ 
     // Return the first process.
    return queue->array[queue->front];                 
} 
  
struct proc* rear(struct Queue* queue) 
{  
    // Return the last process.
    return queue->array[queue->rear];                   
} 
