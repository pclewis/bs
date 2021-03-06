#ifndef WORD_BIT /* only thing from _XOPEN_SOURCE we need */
#define WORD_BIT ((sizeof(int))*CHAR_BIT)
#endif

// adapted from comp.lang.c FAQ Q 20.8
#define BITMASK(b) (1 << ((b) % WORD_BIT))
#define BITSLOT(b) ((b) / WORD_BIT)
#define BITSET(a, b) ((a)[BITSLOT(b)] |= BITMASK(b))
#define BITSETV(a, b, v) ((v)?BITSET(a,b):BITCLEAR(a,b))
#define BITCLEAR(a, b) ((a)[BITSLOT(b)] &= ~BITMASK(b))
#define BITTEST(a, b) ((a)[BITSLOT(b)] & BITMASK(b))
#define BITNSLOTS(nb) ((nb + WORD_BIT - 1) / WORD_BIT)

#define MAX_BIT (1<<24)
// (1<<9)  - 1144972  16400ms
// (1<<10) - 1396740
// (1<<11) - 1847500  15000ms
// (1<<12) - 2592972
// (1<<14) - 4239380 (segfault)
#define GROUP_SIZE (1<<11)
#define MAX_SETS (1<<22)

typedef unsigned int uint;
typedef uint BS_SetID;
typedef uint BS_BitID;

typedef struct {
  uint slots[BITNSLOTS(GROUP_SIZE)];
  size_t ref_count; /* 0 = no shared references */
  size_t pop_count;
} BS_Block;

typedef struct _BS_Node {
  uint index;
  BS_Block *block;
  struct _BS_Node *next;
} BS_Node;

typedef struct {
  uint n_nodes;
  BS_Node *first;
  uint *node_map;
} BS_Set;

typedef struct {
  BS_Set *sets;
  BS_SetID max_set_id;
  BS_BitID max_bit_id;
} BS_State;

typedef enum {
  BS_OP_AND,
  BS_OP_OR,
  BS_OP_ANDNOT,
  BS_OP_TEST_ANY,
  BS_OP_TEST_EVERY,
} BS_OP;

BS_State *bs_new(uint n_sets, uint n_bits);
void bs_destroy(BS_State *bs);
void bs_add(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_BitID *vs);
void bs_remove(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_BitID *vs);
uint *bs_to_uints(BS_State *bs, BS_SetID set_id, size_t *n_vs);
void bs_intersection(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_SetID *vs);
void bs_union(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_SetID *vs);
void bs_copy(BS_State *bs, BS_SetID set_id, BS_SetID src_set_id);
void bs_clear(BS_State *bs, BS_SetID set_id);
void bs_reset(BS_State *bs);
