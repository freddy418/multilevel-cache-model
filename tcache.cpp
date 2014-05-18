#include "tcache.h"
#include <cstring>

#ifdef REFILL
void tcache::set_trace(char* trace){
  char fname[256];
  appname = (char *)malloc(256 * sizeof(char));
  strcpy(appname, trace);  
  sprintf(fname, "%s_l2trace0.log", appname);
  l2trace = fopen(fname, "w");
}
#endif

tcache::tcache(i32 ns, i32 bs, i32 as, i32 ofs){
  /* initialize cache parameters */
  sets = new cache_set[ns];
  nsets = ns;
  bsize = bs;
  bvals = bs >> 3;
  assoc = as;
  imask = ns-1;
  oshift = 2;
  bmask = (bs >> 3) - 1;
  l2trace = 0;

  ishift = log2(ns);
  bshift = log2(bs) - ofs;
  amask = -1 << bshift;

  // initialize pointer to L2 as zero
  next_level = 0;
  /* initialize statisitic counters */
  accs = 0;
  hits = 0;
  misses = 0;
  writebacks = 0;
  allocs = 0;
 
#ifdef LINETRACK
  mcount = (i32*)calloc(nsets, sizeof(i32));
  acount = (i32*)calloc(nsets, sizeof(i32));
#endif

  for (i32 i=0;i<nsets;i++){
    /* distribute data blocks to cache sets */
    sets[i].blks = new cache_block[assoc];
    /* initialize LRU info */ 
    item* node = new item();
    node->val = 0;
    sets[i].lru = node;
    for (i32 j=1;j<assoc;j++){
      node->next = new item();
      node->next->val = j;
      node = node->next;
    }
  }
}

void tcache::clearstats(){
   accs = 0;
   hits = 0;
   misses = 0;
   bwused = 0;
   writebacks = 0;
   allocs = 0;

#ifdef LINETRACK 
   for(int i=0;i<nsets;i++){
     mcount[i] = acount[i] = 0;
   }
#endif
}

void tcache::writeback(cache_block* bp, i32 addr){
  i32 zero = 0;

  // L1 cache
  if (next_level != 0){
    next_level->copy(addr, bp);
    bwused += bsize;
  }

  // update maps on eviction
  if (map != 0 && bp->dirty == 1){
    for (i32 i=0;i<bvals;i++){
      if (bp->value[i] != 0){
	zero = 1;
	break;
      }
    }
    if (zero == 0){ // all zeros
      map->update_block(addr, 0);
    }
  }

  if (mem != 0 && (zero == 1 || map == 0)){
    for (i32 i=0;i<bvals;i++){
      mem->write((addr & amask) + (i<<oshift), bp->value[i]);
#ifdef TEST
      if (addr == 0){
	printf("WB (%X): Writing mem addr (%X), data(%llX)\n", addr, ((addr&amask)+(i<<oshift)), bp->value[i]);
      }
#endif
    }
    bwused += bsize;
  }

  bp->dirty = 0;
  writebacks++;
}

void tcache::allocate(i32 addr){
  i32 tag, index, hit, hitway, wbaddr;
  cache_block* bp;

  index = (addr >> bshift) & imask;
  tag = (addr >> (bshift + ishift));
  hitway = sets[index].lru->val;
  hit = 0;
  for(i32 i=0;i<assoc;i++){
    bp = &(sets[index].blks[i]);
    if ((bp->tag == tag) && (bp->valid == 1)){
      hit = 1;
      hitway = i;
    }
  }
  bp = &(sets[index].blks[hitway]);

  hits+=hit;
  accs++;
  misses+=(1-hit);

#ifdef TEST
  if (addr == 0){
    printf("%s allocate, set(%X), addr(%X)\n", name, index, addr);
  }
#endif

  // if block is valid and dirty, write it back
  if (hit == 0){
#ifdef LINETRACK
    if (((index%512) == 0) && (strcmp(name, "L2") == 0)){
      printf("ALLOC: address(%08X), tag(%X), w0(%X), w1(%X)\n", addr, tag, sets[index].blks[0].tag, sets[index].blks[1].tag);
    }
#endif
    if (bp->valid == 1 && bp->dirty == 1){
      wbaddr = ((bp->tag) << (ishift+bshift)) + (index<<(bshift));
      this->writeback(bp, wbaddr);
    }

    if (bp->value == 0){
      bp->value = new i64[bvals];
      if (bp->value <= 0){
	printf("FATAL: calloc ran out of memory!\n");
	exit(1);
      }
    }

    bp->tag = tag;
    bp->valid = 1;
    bp->dirty = 0;
    for (i32 i=0;i<bvals;i++){
      bp->value[i] = 0;
    }
  } // otherwise just update LRU info

  update_lru(&(sets[index]), hitway);
  allocs++;
}

void tcache::touch(i32 addr){
  i32 tag, index, hit, hitway;
  cache_block* bp;

  index = (addr >> bshift) & imask;
  tag = (addr >> (bshift + ishift));
  hitway = sets[index].lru->val;

  hit = 0;
  for(i32 i=0;i<assoc;i++){
    bp = &(sets[index].blks[i]);
    if ((bp->tag == tag) && (bp->valid == 1)){
      hit = 1;
      hitway = i;
    }
  }

  if (hit == 1){
    update_lru(&(sets[index]), hitway);
  }
}

void tcache::copy(i32 addr, cache_block* op){
  i32 tag, index, hitway, wbaddr, hit;
  cache_block* bp;

  index = (addr >> bshift) & imask;
  tag = (addr >> (bshift + ishift));
  hitway = sets[index].lru->val;
  hit = 0;
  for(i32 i=0;i<assoc;i++){
    bp = &(sets[index].blks[i]);
    if ((bp->tag == tag) && (bp->valid == 1)){
      hit = 1;
      hitway = i;
    }
  }
  bp = &(sets[index].blks[hitway]);

  // if block is valid and dirty, write it back
  if ((hit == 0) && (bp->valid == 1) && (bp->dirty == 1)){
    wbaddr = ((bp->tag)<<(ishift+bshift)) + (index<<bshift);
    this->writeback(bp, wbaddr);
  }

  if (bp->value == 0){
    bp->value = (i64*) calloc(bvals, sizeof(i64));
    if (bp->value <= 0){
      printf("FATAL: calloc ran out of memory!\n");
      exit(1);
    }
  }

  bp->tag = tag;
  bp->valid = op->valid;
  bp->dirty = op->dirty;
  
#ifdef TEST
  if (addr == 0){ //(strcmp(name, "L2") == 0){
    printf("%s (%u, %u) WB to addr (%X-%X): ", name, index, hitway, addr, addr+64);
  }
#endif
  for (i32 i=0;i<bvals;i++){
#ifdef L2TRACE
    if (op->value[i] > 0ULL){
      fprintf(l2trace, "%llx\n", op->value[i]);
      if (lcnt++ > LMAX){
	char fname[256];
	sprintf(fname, "%s_l2trace%u.log", appname, ++fcnt);
	fprintf(stderr, "Filled trace with %lu values, closing trace and opening new trace: %s\n", lcnt, fname);
	fclose(l2trace);
	l2trace = fopen(fname, "w");
	lcnt = 0;
      }
    }
#endif
    bp->value[i] = op->value[i];
#ifdef TEST
   if (addr == 0){ //(strcmp(name, "L2") == 0){  //if (strcmp(name, "L2") == 0){
     printf("%llX,", op->value[i]);
   }
#endif
  }
#ifdef TEST
  if (addr == 0){ //(strcmp(name, "L2") == 0){ //if (strcmp(name, "L2") == 0){
    printf("\n");
  }
#endif

  update_lru(&(sets[index]), hitway);
}

void tcache::refill(cache_block* bp, i32 addr){
  i32 i, index, tag;
  tag = (addr >> (bshift + ishift)); 
  index = (addr >> bshift) & imask;
  
  bp->tag = tag;
  bp->valid = 1;
  bp->dirty = 0;
  if (bp->value == 0){
    bp->value = (i64*) calloc(bvals, sizeof(i64));
    if (bp->value == 0){
      printf("FATAL: calloc ran out of memory!\n");
      exit(1);
    }
  }

#ifdef TEST
  if (strcmp(name, "L1") == 0 && addr == 0){
    printf("refill called on %s, addr(%X-%X), %u\n", name, addr, addr+64, bvals);
  }
#endif

  if (next_level != 0){
    for (i32 i=0;i<bvals;i++){
      bp->value[i] = next_level->read((addr&amask)+(i<<oshift), i);
      /*if (strcmp(name, "L1") == 0  && addr == 0){
	printf("%X-%llX,", (addr&amask)+(i<<oshift), bp->value[i]);
	}*/
    }
    /*if (strcmp(name, "L1") == 0  && addr == 0){
      printf("\n");
      }*/
    next_level->accs -= (bvals-1);
    next_level->hits -= (bvals-1);
    bwused += bsize;
  }
  else if (mem != 0){
    //printf("sets(%u), bsize(%u) - Refill from memory - addr(%08X), index(%u), tag(%X)\n", nsets, bsize, addr, index, tag);
    //exit(1);

#ifdef LOG
    fprintf(tlog, "m\n");
#endif

    for (i=0;i<bvals;i++){
      i64 value = mem->read((addr & amask) + (i<<oshift));
#ifdef REFILL
      if (value > 0ULL){
	fprintf(l2trace, "%lx\n", value);
	if (lcnt++ > LMAX){
	  char fname[256];
	  sprintf(fname, "%s_l2trace%u.log", appname, ++fcnt);
	  fprintf(stderr, "Filled trace with %u values, closing trace and opening new trace: %s\n", lcnt, fname);
	  fclose(l2trace);
	  l2trace = fopen(fname, "w");
	  lcnt = 0;
	}
      }
#endif
      bp->value[i] = value; //mem->read((addr & amask) + (i<<oshift));
      //printf("REFILL (%X): Reading mem addr (%X), data(%llX)\n", addr, ((addr&amask)+(i<<oshift)), bp->value[i]);
    }
    bwused += bsize;
  }
  //printf("block size: %d, index: %d, addr: %X, bmask: %X\n", (bsize), (addr>>bshift)&(bmask), addr, bmask);
}

i64 tcache::read(i32 addr, i32 refill){
  i32 index = (addr >> bshift) & imask;
  i32 tag = (addr >> (bshift + ishift));  
  i32 hit = 0;
  i32 hitway = 0;
  i32 zero = 0;
  i32 wbaddr;
  cache_block* block;

  // check tags
  for(i32 i=0;i<assoc;i++){
    if ((sets[index].blks[i].tag == tag) && (sets[index].blks[i].valid == 1)){
      hit = 1;
      hitway = i;      
    }
  }

#ifdef LINETRACK
  acount[index]++;
#endif

  // update bookkeeping
  if (hit == 1){
    hits++;
    block = &(sets[index].blks[hitway]);

#ifdef LOG
    if (refill == 0){
      fprintf(tlog, "%s\n", name);
    }
#endif

#ifdef TEST
    if (addr == 0){
      printf("%s Read hit(%u,%u), addr(%X), data(%llX)\n", name, index, hitway, addr, block->value[((addr>>oshift)&bmask)]);
      fflush(stdout);
    }   
#endif

  }else{
    misses++;    
    hitway = sets[index].lru->val;
#ifdef LINETRACK
    mcount[index]++;
    if (((index%512) == 0) && (strcmp(name, "L2") == 0)){
      printf("address(%08X), tag(%X), w0(%X), w1(%X)\n", addr, tag, sets[index].blks[0].tag, sets[index].blks[1].tag);
    }
#endif
    block = &(sets[index].blks[hitway]);
    //printf("miss to index: %d on tag: %x, replaced %d\n", index, tag, hitway);
    if (block->valid == 1 && block->dirty == 1){
      // lock line in next level
      if (next_level != 0){
	next_level->touch(addr);
      }
      wbaddr = ((block->tag)<<(ishift+bshift)) + (index<<bshift);
      this->writeback(block, wbaddr);
    }
    this->refill(block, addr);
#ifdef TEST
    if (addr == 0){
      printf("%s Read miss(%u,%u), addr(%X), data(%llX)\n", name, index, hitway, addr, block->value[((addr>>oshift)&bmask)]);
      fflush(stdout);
    }   
#endif
  }
  
  this->update_lru(&(sets[index]), hitway);
  accs++;

  //printf("bsize(%u), sets(%u) - Access: read, addr(%X), index(%X), tag(%X), block(%X)\n", bsize, nsets, addr, index, tag, ((addr>>(oshift))&bmask));

  return block->value[((addr>>oshift)&bmask)];
}

void tcache::write(i32 addr, i64 data){
  i32 index = (addr >> bshift) & imask;
  i32 tag = (addr >> (bshift + ishift));  
  i32 hit = 0;
  i32 hitway = 0;
  i32 zero = 0;
  i32 wbaddr;
  cache_block* block;

  //printf("cache write: address(%08X), data(%llX)\n", addr, data);

  // check tags
  for(i32 i=0;i<assoc;i++){
    block = &(sets[index].blks[i]);
    if ((sets[index].blks[i].tag == tag) && (sets[index].blks[i].valid == 1)){
      hit = 1;
      hitway = i;
    }
  }

#ifdef LINETRACK
  acount[index]++;
#endif

  // update bookkeeping
  if (hit == 1){
    hits++;
    block = &(sets[index].blks[hitway]);
#ifdef LOG
    fprintf(tlog, "%s\n", name);
#endif
  }else{
    misses++;    
    hitway = sets[index].lru->val;
#ifdef LINETRACK
    mcount[index]++;      
    if (((index%512) == 0) && (strcmp(name, "L2") == 0)){
      printf("address(%08X), tag(%X), w0(%X), w1(%X)\n", addr, tag, sets[index].blks[0].tag, sets[index].blks[1].tag);
    }
#endif
    block = &(sets[index].blks[hitway]);
    //printf("miss to index: %d on tag: %x, replaced %d\n", index, tag, hitway);
    if (block->valid == 1 && block->dirty == 1){
      // lock line in next level
      if (next_level != 0){
	next_level->touch(addr);
      }
      wbaddr =  ((block->tag)<<(ishift+bshift)) + (index<<bshift);
      this->writeback(block, wbaddr);
    }
    this->refill(block, addr);
  }

#ifdef L2TRACE
  if (data > 0){
    fprintf(l2trace, "%llx\n", data);
    if (lcnt++ > LMAX){      
      char fname[256];
      sprintf(fname, "%s_l2trace%u.log", appname, ++fcnt);
      fprintf(stderr, "Filled trace with %lu values, closing trace and opening new trace: %s\n", lcnt, fname);
      fclose(l2trace);
      l2trace = fopen(fname, "w");
      lcnt = 0;
    }
  }
#endif
  
  block->value[((addr>>oshift)&bmask)] = data;
  block->dirty = 1;
  this->update_lru(&(sets[index]), hitway);
  accs++;
}

void tcache::stats(){
  i32 size = (nsets) * (assoc) * (bsize);

  printf("%s: %d KB cache:\n", name, size >> 10);
  printf("miss rate: %1.8f\n", (((double)misses)/(accs)));
  printf("%lu accesses, %lu hits, %lu misses, %lu writebacks, %lu allocs\n", accs, hits, misses, writebacks, allocs);
  if (mem != 0){
    printf("bandwidth used: %lu KB\n", (bwused >> 10));
  }
 
#ifdef LINETRACK 
  for (i32 i=0;i<nsets;i++){
    printf("Set %u, Accesses %u, Misses %u\n", i, acount[i], mcount[i]);
  }
#endif
}


void tcache::update_lru(cache_set * set, unsigned int hitway){
  item * hitnode;
  item * node = set->lru;
  item * temp;

  /*temp = set->lru;
  printf("before: way %d referenced, (%d,", hitway, temp->val);
  while (temp->next != 0){
    temp = temp->next;
    printf("%d,", temp->val);
  }
  printf(")\n");*/

  if (node->next == 0){
    // direct-mapped cache
    return;
  }
  
  if (node->val == hitway){
    hitnode = node;
    set->lru = node->next;
    while (node->next != 0){
      node = node->next;
    }
  }else{
    while (node->next != 0){
      if (node->next->val == hitway){
	hitnode = node->next;
	node->next = hitnode->next;
      }else{
	node = node->next;
      }
    }
  }
  node->next = hitnode;
  hitnode->next = 0;

  /*temp = set->lru;
  printf("after: way %d referenced, (%d,", hitway, temp->val);
  while (temp->next != 0){
    temp = temp->next;
    printf("%d,", temp->val);
  }
  printf(")\n");*/
}

void tcache::set_mem(tmemory* sp){
  mem = sp;
}

void tcache::set_map(mem_map* mp){
  map = mp;
}

void tcache::set_nl(tcache* cp){
  next_level = cp;
}

void tcache::set_name(char *cp){
  name = cp;
}

void tcache::set_anum(i32 n){
  anum = n;
}

i64 tcache::get_accs(){
  return accs;
}

i64 tcache::get_hits(){
  return hits;
}

void tcache::set_accs(i64 num){
  accs = num;
}

void tcache::set_hits(i64 num){
  hits = num;
}
