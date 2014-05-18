#ifndef TCACHE_H
#define TCACHE_H

#include "utils.h"
#include "memmap.h"
#include "store.h"

//#define LINETRACK 1

// cache implementation

class tcache {
  cache_set* sets;
  i32 nsets;
  i32 bsize;
  i32 bvals;
  i32 assoc;
  i32 imask;
  i32 bmask;
  i32 ishift;
  i32 bshift;
  i32 oshift;
  i32 amask;
  i64 accs;
  i64 hits;
  i64 misses;
  i64 writebacks;
  i64 allocs;
  i64 bwused;
  i32 anum;
  i32* acount;
  i32* mcount;
  tcache* next_level;
  tmemory* mem;
  mem_map* map;
  char * name;
#ifdef REFILL
  FILE* l2trace;
  char* appname;
  int fcnt;
  int lcnt;
#endif
 public:
  tcache(i32 ns, i32 bs, i32 as, i32 ofs);
  i64 read(i32 addr, i32 refill);
  void write(i32 addr, i64 data);
  void writeback(cache_block* bp, i32 addr);
  void refill(cache_block* bp, i32 addr);
  void stats();
  void update_lru(cache_set * lru, i32 hitway);
  void copy(i32 addr, cache_block* op);
  void allocate(i32 addr);
  void touch(i32 addr);
  void clearstats();
  void set_mem(tmemory* sp);
  void set_map(mem_map* mp);
  void set_nl(tcache* cp);
  void set_name(char * cp);
  void set_anum(i32 n);
  i64 get_accs();
  i64 get_hits();
  void set_accs(i64 num);
  void set_hits(i64 num);
#ifdef REFILL
  void set_trace(char *);
#endif
};

#endif /* TCACHE_H */
