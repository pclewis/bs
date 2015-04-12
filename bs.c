#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>
#include "bs.h"


#define SORT_NAME nuint
#define SORT_TYPE nuint
#define SORT_CMP(x,y) ((int)((x) - (y)))
#include "sort.h"

#define BS1 (sizeof(uint64_t) * CHAR_BIT)
#define BSM (sizeof(((BS_Node*)0)->value) * CHAR_BIT)
#define BSM5 (BSM*BSM*BSM*BSM*BSM)
static const uint64_t BUCKET_SIZES[] = {
  sizeof(uint64_t) * CHAR_BIT, // 1<<6
  BS1 * BSM,                   // 1<<14
  BS1 * BSM * BSM,             // 1<<22
  BS1 * BSM * BSM * BSM,       // 1<<30
  BS1 * BSM * BSM * BSM * BSM, // 1<<38
  BS1 * BSM5,                  // 1<<46
  BS1 * BSM5 * BSM,            // 1<<54
  BS1 * BSM5 * BSM * BSM,      // 1<<62
};

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
static inline uint_fast32_t
popcnt4(const uint64_t* buf)
{
  uint_fast32_t cnt[4] = {0};

  __asm__("popcnt %4, %4  \n\t"
          "add %4, %0     \n\t"
          "popcnt %5, %5  \n\t"
          "add %5, %1     \n\t"
          "popcnt %6, %6  \n\t"
          "add %6, %2     \n\t"
          "popcnt %7, %7  \n\t"
          "add %7, %3     \n\t" // +r means input/output, r means intput
          : "+r" (cnt[0]), "+r" (cnt[1]), "+r" (cnt[2]), "+r" (cnt[3])
          : "r"  (buf[0]), "r"  (buf[1]), "r"  (buf[2]), "r"  (buf[3]));

  return cnt[0] + cnt[1] + cnt[2] + cnt[3];
}

static inline int
popcnt(nuint n)
{
  __asm__("popcnt %0, %0"
          : "+r" (n)
          );
  return n;
}

static inline nuint
bit_index( uint bit_n, uint64_t v )
{
  return bit_n ? popcnt(v << (sizeof(v) * CHAR_BIT - bit_n)) : 0;
}

static inline uint_fast32_t
bit_index4(uint bit_n, const uint64_t* buf)
{
  uint byte_n = bit_n / (sizeof(buf[0]) * CHAR_BIT);

  if(byte_n == 0) return bit_index( bit_n, buf[0] );

  uint subbit_n = bit_n % (sizeof(buf[0]) * CHAR_BIT);

  return popcnt(buf[0]) +
    (popcnt(buf[1]) * (byte_n>1)) +
    (popcnt(buf[2]) * (byte_n>2)) +
    bit_index( subbit_n, buf[byte_n] );
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
  for(bs->depth = 0; BUCKET_SIZES[bs->depth+1] < n_bits; ++bs->depth);
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

static uintptr_t *
insert_v(BS_Node **nodep, uint bit_n, uintptr_t v)
{
  BS_Node *node = *nodep;
  uint n_bits = popcnt4( node->branch_map );

  assert( BITTEST(node->branch_map, bit_n) == 0 );
  BITSET( node->branch_map, bit_n );

  nuint bucket_n = bit_index4(bit_n, node->branch_map);

  size_t current_size = is_0_or_power_of_2(n_bits) ? n_bits : next_power_of_2(n_bits);
  size_t new_size = current_size;

  if(n_bits + 1 > current_size) {
    new_size = current_size ? current_size * 2 : 2;
    node = safe_realloc(node, 1, sizeof(BS_Node) + (sizeof(uintptr_t) * new_size));
    *nodep = node;
  }

  if(bucket_n < n_bits) {
    memmove( node->branches + bucket_n + 1,
             node->branches + bucket_n,
             (n_bits - bucket_n) * sizeof(uintptr_t) );
  }

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

  uint bucket_size = BUCKET_SIZES[depth];
  nuint bit_n = i / bucket_size; // 0 - 255

  /* in val: descend */
  /* else in mask and create: set in val, wipe child val, descend */
  /* create: insert, descend */


  if(BITTEST(node->value, bit_n)) {
    uint branch_n = bit_index4( bit_n, node->branch_map );
    if(depth == 0) return &node->branches[branch_n];
    else return find_leaf( (BS_Node**)&node->branches[branch_n], depth - 1, i % bucket_size, create);
  } else if (create && BITTEST(node->branch_map, bit_n)) {
    uint branch_n = bit_index4( bit_n, node->branch_map );
    BITSET(node->value, bit_n);
    if(depth == 0) {
      node->branches[branch_n] = 0;
      return &node->branches[branch_n];
    } else {
      BS_Node *child = (BS_Node*)node->branches[branch_n];
      memset(child->value, 0, sizeof(child->value));
      return find_leaf( (BS_Node**)&node->branches[branch_n], depth - 1, i % bucket_size, create);
    }
  } else if (create) {
    BITSET(node->value, bit_n);
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
    *leaf |= (1L << ((v-1) % NUINT_BIT));
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
      *leaf &= ~(1L << (v % NUINT_BIT));
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
  nuint bucket_size = BUCKET_SIZES[depth];

  nuint bucket_n = 0;
  for(nuint i = 0; i < sizeof(node->value)*CHAR_BIT; ++i) {
    if(BITTEST(node->value, i)) {
      if(depth == 0) {
        for(nuint j = 0; j < NUINT_BIT; ++j) {
          if(node->branches[bucket_n] & (1L<<j)) {
            add_v( vs, alloced_vs, n_vs, base+(i*bucket_size)+j+1 );
          }
        }
      } else {
        find_nuints( (BS_Node*)node->branches[bucket_n], depth - 1, base + (bucket_size * i), vs, alloced_vs, n_vs );
      }
      bucket_n += 1;
    } else if (BITTEST(node->branch_map, i)) {
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

static uint
intersect_node(BS_Node *node, int depth, BS_Node *other)
{
  node->value[0] &= other->value[0];
  node->value[1] &= other->value[1];
  node->value[2] &= other->value[2];
  node->value[3] &= other->value[3];

  if(depth == 0 && popcnt4(node->branch_map) == (64*4) && popcnt4(other->branch_map) == (64*4)) {
    uint br = 255;
    for(uint bi = 0; bi < 256; ++bi) {
      if((node->branches[bi] &= other->branches[bi]) == 0) {
        BITCLEAR(node->value, bi);
        br -= 1;
      }
    }
    return br;
  }

  uint my_b = 0;
  for(uint vi = 0; vi < 4; ++vi) {
    uint64_t v = node->value[vi];
    while(v) {
      uint64_t bit_v = (v & ~(v-1));
      v ^= bit_v;
      uint64_t bit_n = __builtin_ctzll(bit_v) + (vi*64);

      if(depth == 0) {
        uint b = bit_index4( bit_n, other->branch_map );
        if((node->branches[my_b] &= other->branches[b]) == 0) {
          node->value[vi] &= ~bit_v;
        }
      } else {
        uint b = bit_index4( bit_n, other->branch_map );
        if(intersect_node((BS_Node*)node->branches[my_b], depth - 1, (BS_Node*)other->branches[b]) == 0) {
          node->value[vi] &= ~bit_v;
        }
      }

      my_b += 1;
    }
  }

  return my_b;
}

static uint
intersect_nodes(BS_Node *node, int depth, size_t n_vs, BS_Node **others)
{
  for(uint n = 0; n < n_vs; ++n) {
    node->value[0] &= others[n]->value[0];
    node->value[1] &= others[n]->value[1];
    node->value[2] &= others[n]->value[2];
    node->value[3] &= others[n]->value[3];
  }

  if(depth == 0 && n_vs == 1 && popcnt4(node->branch_map) == (64*4) && popcnt4(others[0]->branch_map) == (64*4)) {
    uint br = 255;
    for(uint bi = 0; bi < 256; ++bi) {
      if((node->branches[bi] &= others[0]->branches[bi]) == 0) {
        BITCLEAR(node->value, bi);
        br -= 1;
      }
    }
    return br;
  }

  uint my_b = 0;
  for(uint vi = 0; vi < 4; ++vi) {
    uint64_t v = node->value[vi];
    while(v) {
      uint64_t bit_v = (v & ~(v-1));
      v ^= bit_v;
      uint64_t bit_n = __builtin_ctzll(bit_v) + (vi*64);

      if(depth == 0) {
        for(uint n = 0; n < n_vs; ++n) {
          uint b = bit_index4( bit_n, others[n]->branch_map );
          if((node->branches[my_b] &= others[n]->branches[b]) == 0) {
            node->value[vi] &= ~bit_v;
            break;
          }
        }
      } else {
        for(uint n = 0; n < n_vs; ++n) {
          uint b = bit_index4( bit_n, others[n]->branch_map );
          if(intersect_node((BS_Node*)node->branches[my_b], depth - 1, (BS_Node*)others[n]->branches[b]) == 0) {
            node->value[vi] &= ~bit_v;
            break;
          }
        }
      }

      my_b += 1;
    }
  }

  return my_b;
}

void
bs_intersection(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_SetID *vs)
{
  BS_Node *node = bs->sets[set_id].root;
  if(node == NULL) return;

  BS_Node **others = safe_alloc( n_vs, sizeof(BS_Node*), false);
  for(uint n = 0; n < n_vs; ++n) {
    others[n] = bs->sets[vs[n]].root;
    /* TODO handle null */
  }

  intersect_nodes( node, bs->depth, n_vs, others );
}

static BS_Node *
copy_node(BS_Node *src, uint depth)
{
  uint n_val_bits = popcnt4( src->value );

  size_t branches_size = is_0_or_power_of_2(n_val_bits) ? n_val_bits : next_power_of_2(n_val_bits);

  BS_Node *dst = safe_alloc( 1, sizeof(BS_Node) + (branches_size * sizeof(uintptr_t)), false );
  memcpy( dst->branch_map, src->value, sizeof(src->value) );
  memcpy( dst->value, src->value, sizeof(src->value) );

  uint dst_b = 0;
  for(uint vi = 0; vi < 4; ++vi) {
    uint64_t v = src->value[vi];
    while(v) {
      uint64_t bit_v = (v & ~(v-1));
      v ^= bit_v;
      uint64_t bit_n = __builtin_ctzll(bit_v) + (vi*64);

      uint src_b = bit_index4( bit_n, src->branch_map );
      //printf("depth=%u v=%"PRIu64", bit_v=%"PRIu64", bit_n=%"PRIu64", src_b=%u->%u\n", depth, v, bit_v, bit_n, src_b, dst_b);
      if(depth == 0) {
        dst->branches[dst_b] = src->branches[src_b];
      } else {
        dst->branches[dst_b] = (uintptr_t)copy_node((BS_Node*)src->branches[src_b], depth - 1);
      }

      dst_b += 1;
    }
  }

  /*printf("dst->branch_map: %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64"\n",
         dst->branch_map[0], dst->branch_map[1], dst->branch_map[2], dst->branch_map[3]);
  printf("dst->value: %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64"\n",
         dst->value[0], dst->value[1], dst->value[2], dst->value[3]);
  */
  return dst;
}

void
bs_copy(BS_State *bs, BS_SetID set_id, BS_SetID src_set_id)
{
  bs_clear(bs, set_id);
  if(bs->sets[src_set_id].root == NULL) return;
  bs->sets[set_id].root = copy_node( bs->sets[src_set_id].root, bs->depth );
}

static void
free_node(BS_Node *node, uint depth)
{
  uint b = 0;
  for(uint vi = 0; vi < 4; ++vi) {
    uint64_t v = node->value[vi];
    while(v) {
      uint64_t bit_v = (v & ~(v-1));
      v ^= bit_v;
      if(depth > 1) free_node( (BS_Node*)node->branches[b], depth - 1);
      else free( (void*) node->branches[b] );
      b += 1;
    }
  }
  free(node);
}

void
bs_clear(BS_State *bs, BS_SetID set_id)
{
  assert(set_id <= bs->max_set_id);
  if(bs->sets[set_id].root != NULL) {
    free_node(bs->sets[set_id].root, bs->depth);
    bs->sets[set_id].root = NULL;
  }
}

void
bs_reset(BS_State *bs)
{
  assert(bs);
}

static void
node_to_gv(BS_Node *node, uint depth, uint64_t base, FILE *fp)
{
  nuint bucket_size = BUCKET_SIZES[depth];
  fprintf(fp, "\tnode_%p [label=<", node);
  fprintf(fp, "\t\t<table border=\"0\" cellborder=\"1\" cellspacing=\"0\">");
  fprintf(fp, "\t\t<tr><td colspan=\"%"PRIuFAST32"\">node %p, range %" PRIu64 " - %" PRIu64 "</td></tr>\n",
          popcnt4(node->branch_map)+1,
          node, base, base + (bucket_size * sizeof(node->value)*CHAR_BIT));

  fprintf(fp, "\t\t<tr><td>range</td>");
  for(uint bit_n = sizeof(node->value)*CHAR_BIT; bit_n--;) {
    if ( BITTEST(node->branch_map, bit_n)) {
      fprintf(fp, "<td>%"PRIu64" -<br/>%"PRIu64"</td>",
              base + (bit_n * bucket_size),
              base + ((bit_n+1) * bucket_size));
    }
  }
  fprintf(fp, "</tr>\n");
  fprintf(fp, "\t\t<tr><td>val</td>");
  for(uint bit_n = sizeof(node->value)*CHAR_BIT; bit_n--;) {
    if( BITTEST(node->value, bit_n) ) {
      fprintf(fp, "<td>%u</td>", bit_n);
    } else if ( BITTEST(node->branch_map, bit_n)) {
      fprintf(fp, "<td> </td>");
    }
  }
  fprintf(fp, "</tr>\n");
  fprintf(fp, "\t\t<tr><td>map</td>");
  for(uint bit_n = sizeof(node->value)*CHAR_BIT; bit_n--;) {
    if( BITTEST(node->branch_map, bit_n) ) {
      fprintf(fp, "<td port=\"b%u\">%u</td>", bit_n, bit_n);
    }
  }
  fprintf(fp, "</tr></table>>];\n");

  uint branch_n = 0;
  for(uint bit_n = 0; bit_n < sizeof(node->value)*CHAR_BIT; bit_n++) {
    if( BITTEST(node->branch_map, bit_n) ) {
      if(depth==0) {
        fprintf(fp, "node_%p:b%d:s -> node_%p_br%u:n;\n", node, bit_n, node, branch_n);
      } else {
        fprintf(fp, "node_%p:b%d:s -> node_%p:n;\n", node, bit_n, (void*)node->branches[branch_n]);
      }
      branch_n++;
    }
  }

  if(depth == 0) {
    branch_n = 0;
    for(uint bit_n = 0; bit_n < sizeof(node->value)*CHAR_BIT; bit_n++) {
      if( BITTEST(node->branch_map, bit_n) ) {
        fprintf(fp, "node_%p_br%u [label=<\n", node, branch_n);
        fprintf(fp, "\t\t<table border=\"0\" cellborder=\"1\" cellspacing=\"0\">");
        if(popcnt(node->branches[branch_n]) == 0) {
          fprintf(fp, "<tr><td bgcolor=\"red\">ZERO</td></tr>");
        } else {
            for(uint n = sizeof(node->branches[0])*CHAR_BIT; n--;) {
              if( node->branches[branch_n] & (1L<<n) ) {
                fprintf(fp, "<tr><td>%" PRIu64 "</td></tr>", base + (bit_n * bucket_size) + n);
              }
            }
        }
        fprintf(fp, "</table>>];\n");
        branch_n++;
      }
    }
  } else {
    /*
    fprintf(fp, "\t{rank=same; ");
    for(uint b = branch_n; b--;) {
      fprintf(fp, "node_%p%s", (BS_Node*)node->branches[b], (b!=0) ? "," : "" );
    }
    fprintf(fp, "}\n");
    */
    branch_n = 0;
    for(uint bit_n = 0; bit_n < sizeof(node->value)*CHAR_BIT; bit_n++) {
      if( BITTEST(node->branch_map, bit_n) ) {
        node_to_gv( (BS_Node*)node->branches[branch_n], depth - 1, base + (bit_n * bucket_size), fp );
        branch_n++;
      }
    }
  }
}

void
bs_to_gv(BS_State *bs, BS_SetID set_id, const char *fn)
{
  FILE *fp = fopen(fn, "w");
  if (!fp) return;
  fprintf(fp, "digraph {\n");
  fprintf(fp, "\tnode[shape=plaintext];\n");
  node_to_gv( bs->sets[set_id].root, bs->depth, 0, fp );
  fprintf(fp, "}\n");
  fclose(fp);
}

void
tree_debug_header()
{
  printf("node\tdepth\tmapsize\tvalsize\tuse%%\tbytes\n");
}

size_t
tree_debug(BS_Node *node, uint depth)
{
  uint n_map_bits = popcnt4( node->branch_map );
  uint n_val_bits = popcnt4( node->value );
  size_t branches_size = is_0_or_power_of_2(n_val_bits) ? n_val_bits : next_power_of_2(n_val_bits);
  size_t child_size = 0;

  uint dst_b = 0;
  for(uint vi = 0; vi < 4; ++vi) {
    uint64_t v = node->branch_map[vi];
    while(v) {
      uint64_t bit_v = (v & ~(v-1));
      v ^= bit_v;
      //uint64_t bit_n = __builtin_ctzll(bit_v) + (vi*64);
      if(depth == 0) {
        /*
        uint pc = popcnt(node->branches[dst_b]);
        printf("%p\t[%"PRIu64"/%u]\t-\t%u\t%.2f\t-\n",
               node, bit_n, dst_b, pc, (pc/256.0)*100);
        */
      } else {
        child_size += tree_debug((BS_Node*)node->branches[dst_b], depth - 1);
      }
      dst_b += 1;
    }
  }
  printf("%p\t%u\t%u\t%u\t%.2f\t%zu+%zu\n",
         node, depth, n_map_bits, n_val_bits, (n_map_bits/256.0)*100,
         sizeof(BS_Node) + (branches_size * sizeof(uintptr_t)), child_size);

  return sizeof(BS_Node) + (branches_size * sizeof(uintptr_t)) + child_size;
}
