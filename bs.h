typedef uintptr_t nuint;
typedef nuint BS_SetID;
typedef nuint BS_BitID;

#define NUINT_BIT (sizeof(nuint) * 8)

// adapted from comp.lang.c FAQ Q 20.8
#define BITMASK(b) (1L << ((b) % NUINT_BIT))
#define BITSLOT(b) ((b) / NUINT_BIT)
#define BITSET(a, b) ((a)[BITSLOT(b)] |= BITMASK(b))
#define BITSETV(a, b, v) ((v)?BITSET(a,b):BITCLEAR(a,b))
#define BITCLEAR(a, b) ((a)[BITSLOT(b)] &= ~BITMASK(b))
#define BITTEST(a, b) ((a)[BITSLOT(b)] & BITMASK(b))
#define BITNSLOTS(nb) ((nb + NUINT_BIT - 1) / NUINT_BIT)

#define MAX_BIT (1<<24)
// (1<<9)  - 1144972  16400ms
// (1<<10) - 1396740
// (1<<11) - 1847500  15000ms
// (1<<12) - 2592972
// (1<<14) - 4239380 (segfault)
#define GROUP_SIZE (1<<11)
#define MAX_SETS (1<<22)

typedef struct {
  uint64_t branch_map[4]; // 32 bytes
  uint64_t value[4];      // 32 bytes
  // -- cache line --
  uintptr_t branches[];   // ptr or val, blocks of 64 bytes
} BS_Node;

typedef struct {
  BS_Node *root;
} BS_Set;

typedef struct {
  BS_Set *sets;
  BS_SetID max_set_id;
  BS_BitID max_bit_id;
  size_t depth;
} BS_State;

typedef enum {
  BS_OP_AND,
  BS_OP_OR,
  BS_OP_ANDNOT,
  BS_OP_TEST_ANY,
  BS_OP_TEST_EVERY,
} BS_OP;

BS_State *bs_new(nuint n_sets, nuint n_bits);
void bs_destroy(BS_State *bs);
void bs_add(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_BitID *vs);
void bs_remove(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_BitID *vs);
nuint *bs_to_nuints(BS_State *bs, BS_SetID set_id, size_t *n_vs);
void bs_intersection(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_SetID *vs);
/*
void bs_union(BS_State *bs, BS_SetID set_id, size_t n_vs, const BS_SetID *vs);
*/
void bs_copy(BS_State *bs, BS_SetID set_id, BS_SetID src_set_id);
void bs_clear(BS_State *bs, BS_SetID set_id);
void bs_reset(BS_State *bs);
