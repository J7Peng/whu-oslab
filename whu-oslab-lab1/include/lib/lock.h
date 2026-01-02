#ifndef __LOCK_H__
#define __LOCK_H__

#include "common.h"

typedef struct spinlock {
    int locked;
    char* name;
    int cpuid;
} spinlock_t;

void push_off();
void pop_off();

void spinlock_init(spinlock_t* lk, char* name);
void spinlock_acquire(spinlock_t* lk);
void spinlock_release(spinlock_t* lk);
bool spinlock_holding(spinlock_t* lk); 

typedef struct sleeplock {
  int locked;      
  struct spinlock lk; 
  char *name;        
  int pid;          
}sleeplock_t;

// sleeplock.c
void            acquiresleep(struct sleeplock*);
void            releasesleep(struct sleeplock*);
int             sleeplock_holding(struct sleeplock*);
void            sleeplock_init(struct sleeplock*, char*);

#endif