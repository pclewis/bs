#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <Judy.h>
#include "bs.h"


#define SORT_NAME uint
#define SORT_TYPE uint
#define SORT_CMP(x,y) ((int)((x) - (y)))
#include "sort.h"

/*
static void
interrupt(int sig)
{
  fclose(stdin);
  signal(sig, SIG_IGN);
}
*/


/*
  add dst v+
  remove dst v+
  copy dst src
  swap dst src
  union dst src+
  difference dst src+
  intersection dst src+
  any dst src+
    for each v in dst:
      if
  every dst src+
  negate dst+ // probably a terrible idea
  parity src
  count src
  print src
  save file
  load file
  clear

  *dst = apply operation to each value
  *src = load srcs
    1 = 2 3 4
    4 = 5 6 7 8
    intersection *1 *4 9 ->
      intersection 2 5 6 7 8 9
      intersection 3 5 6 7 8 9
      intersection 4 5 6 7 8 9

*/

// adapted from http://danluu.com/assembly-intrinsics/
static uint
builtin_popcnt_unrolled_errata_manual(const uint* buf, size_t len)
{
  assert(len % 4 == 0);
  uint cnt[4] = {0};

  for (size_t i = 0; i < len; i+=4) {
    __asm__(
        "popcnt %4, %4  \n\t"
        "add %4, %0     \n\t"
        "popcnt %5, %5  \n\t"
        "add %5, %1     \n\t"
        "popcnt %6, %6  \n\t"
        "add %6, %2     \n\t"
        "popcnt %7, %7  \n\t"
        "add %7, %3     \n\t" // +r means input/output, r means intput
        : "+r" (cnt[0]), "+r" (cnt[1]), "+r" (cnt[2]), "+r" (cnt[3])
        : "r"  (buf[i]), "r"  (buf[i+1]), "r"  (buf[i+2]), "r"  (buf[i+3]));
  }
  return cnt[0] + cnt[1] + cnt[2] + cnt[3];
}

static void
update_pop_count(BS_Block *block)
{
  block->pop_count = builtin_popcnt_unrolled_errata_manual( block->slots, BITNSLOTS(GROUP_SIZE) );
}

static void *
safe_alloc(size_t nmemb, size_t size, bool zero)
{
  assert(size > 0);

  if(nmemb == 0)
    return NULL;

  void *ptr = zero ? calloc(nmemb, size) :malloc(nmemb * size);

  if(ptr == NULL) {
    (void)fprintf(stderr, "PANIC: alloc failed for %u bytes\n", (uint)(nmemb * size));
    abort();
  }

  return ptr;
}

static void *
safe_realloc(void *ptr, size_t nmemb, size_t size)
{
  assert(size > 0);

  if(nmemb == 0) {
    free(ptr);
    return NULL;
  }

  size_t bytes = nmemb * size;
  ptr = realloc(ptr, bytes);

  if(ptr == NULL) {
    (void)fprintf(stderr, "PANIC: realloc failed for %u bytes\n", (uint)bytes);
    abort();
  }

  return ptr;
}

static uint *
sort_uints(size_t n, const uint *vs)
{
  uint *vs_copy = safe_alloc(n, sizeof(uint), false);
  memcpy(vs_copy, vs, n * sizeof(uint));
  uint_quick_sort(vs_copy, n);
  return vs_copy;
}

static BS_Block *
new_block(BS_Set *set, uint index)
{
  BS_Block *block = safe_alloc(1, sizeof(BS_Block), true);
  BS_Block **v = NULL;
  JLI(v, set->blocks, index);
  *v = block;
  set->n_blocks += 1;
  BITSET(set->block_map, index);
  return block;
}

static void
destroy_block(BS_Set *set, uint index)
{
  BS_Block **v = NULL;
  JLG(v, set->blocks, index);
  if(v != NULL && (*v) != NULL) {
    if( (*v)->ref_count == 0) {
      free(*v);
    } else {
      (*v)->ref_count -= 1;
    }
  }
  int rc = 0;
  JLD(rc, set->blocks, index);
  set->n_blocks -= 1;
  BITCLEAR(set->block_map, index);
}

BS_State *
bs_new(uint n_sets, uint n_bits)
{
  BS_State *bs = safe_alloc( 1, sizeof(BS_State), true );
  bs->sets = safe_alloc( n_sets, sizeof(BS_Set), true );
  bs->max_set_id = n_sets - 1;
  bs->max_bit_id = n_bits - 1;
  for(BS_SetID i = 0; i <= bs->max_set_id; ++i) {
    bs->sets[i].block_map = safe_alloc( BITNSLOTS(n_bits / GROUP_SIZE), sizeof(uint), true );
  }
  return bs;
}

void
bs_destroy(BS_State *bs)
{
  bs_reset(bs);
  for(BS_SetID i = 0; i <= bs->max_set_id; ++i) {
    free(bs->sets[i].block_map);
  }
  free(bs->sets);
  free(bs);
}

static BS_Block *
mutable_block(BS_Set *set, uint index)
{
  BS_Block **v = NULL;
  JLG(v, set->blocks, index);
  if(v) {
    if((*v)->ref_count > 0) {
      (*v)->ref_count -= 1;
      BS_Block *nb = safe_alloc(1, sizeof(BS_Block), false);
      memcpy(nb, *v, sizeof(BS_Block));
      nb->ref_count = 0;
      *v = nb;
    }
    return *v;
  } else {
    return new_block(set, index);
  }
}

/**
 * Find node with specified number, optionally creating it in the correct place if it doesn't exist.
 * @param set
 * @param index    Index to search for.
 * @param create   Create node if it doesn't exist.
 */
static BS_Block *
find_block(BS_Set *set, uint index, bool create)
{
  BS_Block **v = NULL;
  JLG(v, set->blocks, index);
  if(v) return *v;
  else if(create) return new_block(set, index);
  else return NULL;
}

void
bs_set(BS_State *bs, BS_SetID set_id, bool value, size_t n_vs, const BS_BitID *vs)
{
  assert( set_id <= bs->max_set_id );

  BS_Set *set = &bs->sets[set_id];

  for(uint n = 0; n < n_vs; ++n) {
    BS_BitID i = vs[n], index = i / GROUP_SIZE;

    BS_Block *block = mutable_block(set, index);
    BITSETV(block->slots, (i-1) % GROUP_SIZE, value);
    update_pop_count(block);
  }
}

/**
 * Add values to a set.
 */
void
bs_add(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_BitID *vs)
{
  bs_set(bs, set_id, true, n_vs, vs);
}

/**
 * Remove values from a set.
 */
void
bs_remove(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_BitID *vs)
{
  bs_set(bs, set_id, false, n_vs, vs);
}

/**
 * Return a bitset as an array of uints.
 * Caller must free result, even if n_vs == 0.
 */
uint *
bs_to_uints(BS_State *bs, BS_SetID set_id, size_t *n_vs)
{
  size_t alloced_vs = 64;
  uint *vs = safe_alloc(alloced_vs, sizeof(uint), false);
  *n_vs = 0;

  BS_Set *set = &bs->sets[set_id];
  BS_Block **pblock = NULL, *block = NULL;
  Word_t index = 0;

  JLF(pblock, set->blocks, index);
  while(pblock) {
    block = *pblock;
    for(uint i = 0; i < GROUP_SIZE; ++i) {
      if(BITTEST(block->slots, i)) {
        if(*n_vs >= alloced_vs) {
          alloced_vs *= 2;
          vs = safe_realloc(vs, alloced_vs, sizeof(uint));
        }
        vs[*n_vs] = (index * GROUP_SIZE) + (i + 1);
        *n_vs += 1;
      }
    }
    JLN(pblock, set->blocks, index);
  }

  if(*n_vs < alloced_vs) {
    vs = safe_realloc(vs, *n_vs, sizeof(uint));
  }

  return vs;
}

void
bs_intersection(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_SetID *vs)
{
  assert( set_id <= bs->max_set_id );

  BS_Set *set = &bs->sets[set_id];
  BS_Set **sets = safe_alloc(n_vs, sizeof(BS_Set*), false);

  uint nn = 0;
  for(uint vn = 0; vn < n_vs; ++vn) {
    assert( vs[vn] <= bs->max_set_id );
    if(vs[vn] != set_id)  {
      sets[nn] = &bs->sets[vs[vn]];
      for(uint i = 0; i < BITNSLOTS((bs->max_bit_id+1) / GROUP_SIZE); ++i) {
        if(set->block_map[i] != sets[nn]->block_map[i]) {
          for(uint j = 0; j < WORD_BIT; ++j) {
            if( (set->block_map[i] & (1<<j)) && !(sets[nn]->block_map[i] & (1<<j)) ) {
              destroy_block(set, (i * WORD_BIT) + j);
            }
          }
        }
      }
      nn += 1;
    }
  }

  for(uint i = 0; i <= ((bs->max_bit_id+1)/GROUP_SIZE); ++i) {
    if(BITTEST(set->block_map, i)) {
      for(uint n = 0; n < nn; ++n) {
        BS_Block *block = find_block( set, i, false );
        BS_Block *other_block = find_block( sets[n], i, false );
        for(int i = BITNSLOTS(GROUP_SIZE); i-- > 0; ) {
          block->slots[i] &= other_block->slots[i];
        }
        update_pop_count(block);
        if(block->pop_count == 0) {
          destroy_block(set, i);
          break;
        }
      }
    }
  }

  free(sets);
}

void
bs_copy(BS_State *bs, BS_SetID set_id, BS_SetID src_set_id)
{
  assert( set_id <= bs->max_set_id );
  assert( src_set_id <= bs->max_set_id );

  bs_clear(bs, set_id);

  BS_Set *set = &bs->sets[set_id];
  BS_Set *src = &bs->sets[src_set_id];
  BS_Block **pblock = NULL, *block = NULL;
  Word_t index = 0;

  JLF(pblock, src->blocks, index);
  while(pblock) {
    block = *pblock;
    BS_Block **nb = NULL;
    JLI(nb, set->blocks, index);
    *nb = block;
    block->ref_count += 1;
    JLN(pblock, src->blocks, index);
  }

  set->n_blocks = src->n_blocks;
  memcpy(set->block_map, src->block_map, BITNSLOTS((bs->max_bit_id+1) / GROUP_SIZE));
}

void
bs_clear(BS_State *bs, BS_SetID set_id)
{
  assert(set_id <= bs->max_set_id);
  BS_Set *set = &bs->sets[set_id];
  BS_Block **pblock = NULL;
  Word_t index = 0;

  JLF(pblock, set->blocks, index);
  while(pblock) {
    destroy_block(set, index);
    JLN(pblock, set->blocks, index);
  }
  assert(set->n_blocks == 0);
}

void
bs_reset(BS_State *bs)
{
  for(BS_SetID i = 0; i <= bs->max_set_id; ++i) {
    bs_clear(bs, i);
  }
}
