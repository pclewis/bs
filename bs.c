#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <stdint.h>
#include "bs.h"


#define SORT_NAME nuint
#define SORT_TYPE nuint
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
static nuint
builtin_popcnt_unrolled_errata_manual(const nuint* buf, size_t len)
{
  assert(len % 4 == 0);
  nuint cnt[4] = {0};

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

static inline int
popcnt(const nuint *n)
{
  return __builtin_popcountll(*n);
}

static void *
safe_alloc(size_t nmemb, size_t size, bool zero)
{
  assert(size > 0);

  if(nmemb == 0)
    return NULL;

  void *ptr = zero ? calloc(nmemb, size) : malloc(nmemb * size);

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

static nuint *
sort_nuints(size_t n, const nuint *vs)
{
  nuint *vs_copy = safe_alloc(n, sizeof(nuint), false);
  memcpy(vs_copy, vs, n * sizeof(nuint));
  nuint_quick_sort(vs_copy, n);
  return vs_copy;
}

BS_State *
bs_new(nuint n_sets, nuint n_bits)
{
  BS_State *bs = safe_alloc( 1, sizeof(BS_State), true );
  bs->sets = safe_alloc( n_sets, sizeof(BS_Set), true );
  bs->max_set_id = n_sets - 1;
  bs->max_bit_id = n_bits - 1;
  for(bs->depth = 0; n_bits > 128; ++bs->depth) n_bits /= 128;
  return bs;
}

void
bs_destroy(BS_State *bs)
{
  bs_reset(bs);
  free(bs->sets);
  free(bs);
}

static inline nuint
next_power_of_2(nuint v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return ++v;
}

static inline bool
is_0_or_power_of_2(nuint v)
{
  return (v&(v-1))==0;
}

static inline nuint
bit_index( nuint bit_n, nuint v )
{
  nuint index = 0, t;
  while( (t=__builtin_ctz(v)) < bit_n ) {
    v ^= (1<<t);
    ++index;
  }
  return index;
}

static uintptr_t *
insert_v(BS_Node **nodep, nuint bit_n, uintptr_t v)
{
  BS_Node *node = *nodep;
  nuint max_bucket_n = popcnt(&node->mask);

  assert( (node->mask & (1<<bit_n)) == 0 );
  node->mask |= (1<<bit_n);

  nuint bucket_n = bit_index(bit_n, node->mask);

  if(is_0_or_power_of_2(max_bucket_n)) {
    nuint new_max = max_bucket_n ? max_bucket_n * 2 : 2;
    node = safe_realloc(node, 1, sizeof(BS_Node) + (sizeof(node->branches[0]) * new_max));
    *nodep = node;
    max_bucket_n = new_max;
  } else {
    max_bucket_n = next_power_of_2(max_bucket_n);
  }

  // shove everything right a slot
  memmove(node->branches + bucket_n + 1, node->branches + bucket_n, (max_bucket_n - bucket_n - 1) * sizeof(nuint));

  node->branches[bucket_n] = v;

  return &node->branches[bucket_n];
}

static uintptr_t *
insert_node(BS_Node **nodep, nuint bit_n)
{
  return insert_v( nodep, bit_n, (uintptr_t)safe_alloc(1, sizeof(BS_Node), true) );
}

uintptr_t *
find_leaf(BS_Node **nodep, nuint depth, nuint i, bool create)
{
  BS_Node *node = *nodep;
  if(node == NULL) {
    if(create) {
      node = safe_alloc(1, sizeof(BS_Node), true);
      *nodep = node;
    } else {
      return NULL;
    }
  }

  nuint bucket_size = NUINT_BIT;
  for(nuint d = depth; d; --d) {
    bucket_size *= NUINT_BIT;
  }

  nuint bit_n = i / bucket_size; // 0 - 63
  assert(bit_n < NUINT_BIT);

  nuint bit_v = 1 << bit_n;

  /* in val: descend */
  /* else in mask and create: set in val, wipe child val, descend */
  /* create: insert, descend */


  if(node->value & bit_v) {
    nuint branch_n = bit_index( bit_n, node->mask );
    if(depth == 0) return &node->branches[branch_n];
    else return find_leaf( (BS_Node**)&node->branches[branch_n], depth - 1, i % bucket_size, create);
  } else if (create && (node->mask & bit_v)!=0) {
    nuint branch_n = bit_index( bit_n, node->mask );
    node->value |= bit_v;
    if(depth == 0) {
      node->branches[branch_n] = 0;
      return &node->branches[branch_n];
    } else {
      BS_Node *child = (BS_Node*)node->branches[branch_n];
      child->value = 0;
      return find_leaf( (BS_Node**)&node->branches[branch_n], depth - 1, i % bucket_size, create);
    }
  } else if (create) {
    node->value |= bit_v;
    if(depth == 0) return insert_v( nodep, bit_n, 0 );
    else return find_leaf( (BS_Node**)insert_node(nodep, bit_n), depth - 1, i % bucket_size, create );
  }

  return NULL;
}

/**
 * Add values to a set.
 */
void
bs_add(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_BitID *vs)
{
  for (size_t n = 0; n < n_vs; ++n) {
    nuint v = vs[n];
    uintptr_t *leaf = find_leaf( &bs->sets[set_id].root, bs->depth, v-1, true );
    *leaf |= (1 << ((v-1) % NUINT_BIT));
  }
}

/**
 * Remove values from a set.
 */
void
bs_remove(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_BitID *vs)
{
  for (size_t n = 0; n < n_vs; ++n) {
    nuint v = vs[n];
    uintptr_t *leaf = find_leaf( &bs->sets[set_id].root, bs->depth, v-1, true );
    if(leaf) {
      *leaf &= ~(1 << (v % NUINT_BIT));
    }
  }
}

static void
add_v( nuint **vs, size_t *alloced_vs, size_t *n_vs, nuint v )
{
  if(*n_vs >= *alloced_vs) {
    *alloced_vs *= 2;
    *vs = safe_realloc(*vs, *alloced_vs, sizeof(nuint));
  }

  (*vs)[*n_vs] = v;
  *n_vs += 1;
}

static void
find_nuints( BS_Node *node, nuint depth, nuint base, nuint **vs, size_t *alloced_vs, size_t *n_vs )
{
  nuint bucket_size = NUINT_BIT;
  for(nuint d = depth; d; --d) {
    bucket_size *= NUINT_BIT;
  }

  nuint bucket_n = 0;
  for(nuint i = 0; i < NUINT_BIT; ++i) {
    if(node->value & (1<<i)) {
      if(depth == 0) {
        for(nuint j = 0; j < NUINT_BIT; ++j) {
          if(node->branches[bucket_n] & (1<<j)) {
            add_v( vs, alloced_vs, n_vs, base+(i*bucket_size)+j+1 );
          }
        }
      } else {
        find_nuints( (BS_Node*)node->branches[bucket_n], depth - 1, base + (bucket_size * i), vs, alloced_vs, n_vs );
      }
      bucket_n += 1;
    } else if (node->mask & (1<<i)) {
      bucket_n += 1;
    }
  }
}

/**
 * Return a bitset as an array of nuints.
 * Caller must free result, even if n_vs == 0.
 */
nuint *
bs_to_nuints(BS_State *bs, BS_SetID set_id, size_t *n_vs)
{
  size_t alloced_vs = 64;
  nuint *vs = safe_alloc(alloced_vs, sizeof(nuint), true);
  *n_vs = 0;

  find_nuints( bs->sets[set_id].root, bs->depth, 0, &vs, &alloced_vs, n_vs );

  if(alloced_vs > *n_vs) {
    vs = safe_realloc(vs, *n_vs, sizeof(nuint));
  }

  return vs;
}

/*
void
bs_intersection(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_SetID *vs)
{
}

void
bs_copy(BS_State *bs, BS_SetID set_id, BS_SetID src_set_id)
{
}
*/

void
bs_clear(BS_State *bs, BS_SetID set_id)
{
  assert(set_id <= bs->max_set_id);
}

void
bs_reset(BS_State *bs)
{
  assert(bs);
}
