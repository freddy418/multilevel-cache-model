#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
typedef unsigned int i32;
typedef unsigned long i64;

#ifdef LOG
extern FILE* tlog;
#endif

//#define TEST 1
//#define L2TRACE 1
#define REFILL
#define LMAX 1<<26

// lru implementation

typedef struct llnode {
   unsigned int val;
   struct llnode * next;
} item;

unsigned int pow2(unsigned int v);

typedef struct cache_blk_t
{
  i32 valid;
  i32 dirty;
  i32 tag;
  i64 * value;
} cache_block;

typedef struct cache_set
{
  cache_block* blks;
  item* lru;
} cache_set;


#endif /* UTILS_H */
