#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include "bs.h"

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

static int
compare_uints(const void *a, const void *b)
{
  return (int)(*(uint*)a - *(uint*)b);
}

static uint *
sort_uints(size_t n, const uint *vs)
{
  uint *vs_copy = safe_alloc(n, sizeof(vs), false);
  memcpy(vs_copy, vs, n * sizeof(vs));
  qsort(vs_copy, n, sizeof(uint), compare_uints);
  return vs_copy;
}

static BS_Node *
new_node(uint index, BS_Block *block, BS_Node *next, BS_Node *prev, BS_Node **head)
{
  BS_Node *node = safe_alloc(1, sizeof(BS_Node), true);
  node->index = index;

  if(block) {
    node->block = block;
    node->block->ref_count += 1;
  } else {
    node->block = safe_alloc(1, sizeof(BS_Block), true);
  }

  if(prev) prev->next = node;
  else if(head) *head = node;

  node->next = next;

  return node;
}

static BS_Node *
destroy_node(BS_Node *node, BS_Node *prev, BS_Node **head, bool to_end)
{
  BS_Node *next = NULL;

  do {
    if(node->block->ref_count > 0) {
      node->block->ref_count -= 1;
    } else {
      free(node->block);
    }

    next = node->next;
    free(node);
    node = next;
  } while (node && to_end);

  if(prev) prev->next = next;
  else if(head) *head = next;

  return next;
}

static void
prepare_to_change(BS_Node *node)
{
  if(node->block->ref_count > 0) {
    BS_Block *block = safe_alloc(1, sizeof(BS_Block), false);
    memcpy(block, node->block, sizeof(BS_Block));
    node->block->ref_count -= 1;
    node->block = block;
  }
}

/**
 * Find node with specified number, optionally creating it in the correct place if it doesn't exist.
 * @param head     Pointer to head of list. Will be updated if a new start is inserted.
 * @param start    Node to start searching from, to avoid starting over when looping over sorted sets.
 * @param index    Index to search for.
 * @param create   Create node if it doesn't exist.
 */
static BS_Node *
find_node(BS_Node **head, BS_Node *start, uint index, bool create)
{
  BS_Node *prev = start,
    *node = NULL;

  if( start == NULL || start->index > index )
    node = *head;
  else
    node = start->next;

  for(; node != NULL && node->index < index; prev = node, node = node->next) {
    /* do nothing */
  }

  if(node == NULL || node->index > index) {
    if(create) {
      node = new_node(index, NULL, node, prev, head);
    } else {
      node = NULL;
    }
  }

  return node;
}

void
bs_set(BS_State *bs, BS_SetID set_id, bool value, size_t n_vs, const uint *vs)
{
  assert( set_id <= bs->max_set_id );

  uint *sorted_vs = sort_uints(n_vs, vs);
  BS_Node *node = NULL;

  for(uint n = 0; n < n_vs; ++n) {
    uint i = sorted_vs[n], index = i / GROUP_SIZE;

    if(node == NULL || node->index != index) {
      node = find_node( &bs->sets[set_id], node, index, true );
      prepare_to_change(node);
    }

    BITSETV(node->block->slots, (i-1) % GROUP_SIZE, value);
  }

  free(sorted_vs);
}

/**
 * Add values to a set.
 */
void
bs_add(BS_State *bs, BS_SetID set_id, size_t n_vs, const uint *vs)
{
  bs_set(bs, set_id, true, n_vs, vs);
}

/**
 * Remove values from a set.
 */
void
bs_remove(BS_State *bs, BS_SetID set_id, size_t n_vs, const uint *vs)
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

  for(BS_Node *node = bs->sets[set_id]; node; node = node->next) {
    for(uint i = 0; i < GROUP_SIZE; ++i) {
      if(BITTEST(node->block->slots, i)) {
        if(*n_vs >= alloced_vs) {
          alloced_vs *= 2;
          vs = safe_realloc(vs, alloced_vs, sizeof(uint));
        }
        vs[*n_vs] = (node->index * GROUP_SIZE) + (i + 1);
        *n_vs += 1;
      }
    }
  }

  if(*n_vs < alloced_vs) {
    vs = safe_realloc(vs, *n_vs, sizeof(uint));
  }

  return vs;
}

void
bs_intersection(BS_State *bs, BS_SetID set_id, size_t n_vs, const uint *vs)
{
  assert( set_id <= bs->max_set_id );

  BS_Node *node = bs->sets[set_id],
    *prev = NULL,
    **other_nodes = safe_alloc(n_vs, sizeof(BS_Node*), false);

  for(uint n = 0; n < n_vs; ++n) {
    assert( vs[n] <= bs->max_set_id );
    other_nodes[n] = bs->sets[vs[n]];
  }

  while(node != NULL) {
    bool advance = true;
    for(uint n = 0; n < n_vs; ++n) {
      while(other_nodes[n] && other_nodes[n]->index < node->index) {
        other_nodes[n] = other_nodes[n]->next;
      }

      if(other_nodes[n] == NULL) {
        destroy_node( node, prev, &bs->sets[set_id], true );
        advance = false;
        break;
      }

      while(node && other_nodes[n]->index > node->index) {
        node = destroy_node(node, prev, &bs->sets[set_id], false );
        advance = false;
        break;
      }

      if(node && other_nodes[n]->index == node->index) {
        prepare_to_change(node);
        for(int i = BITNSLOTS(GROUP_SIZE); i-- > 0; ) {
          node->block->slots[i] &= other_nodes[n]->block->slots[i];
        }
      }
    }

    if(advance) {
      prev = node;
      node = node->next;
    }
  }

  free(other_nodes);
}

void
bs_union(BS_State *bs, BS_SetID set_id, size_t n_vs, const uint *vs)
{
  assert( set_id <= bs->max_set_id );

  BS_Node *node = bs->sets[set_id],
    *prev = NULL,
    **other_nodes = safe_alloc(n_vs, sizeof(BS_Node*), false);

  for(uint n = 0; n < n_vs; ++n) {
    assert( vs[n] <= bs->max_set_id );
    other_nodes[n] = bs->sets[vs[n]];
  }

  uint remaining = 0;
  do {
    remaining = n_vs;
    for(uint n = 0; n < n_vs; ++n) {
      if(other_nodes[n] == NULL) {
        remaining -= 1;
        continue;
      }

      if(node == NULL || other_nodes[n]->index < node->index) {
        node = new_node( other_nodes[n]->index, other_nodes[n]->block, node, prev, &bs->sets[set_id] );
      } else if(other_nodes[n]->index == node->index) {
        prepare_to_change(node);
        for(int i = BITNSLOTS(GROUP_SIZE); i-- > 0; ) {
          node->block->slots[i] |= other_nodes[n]->block->slots[i];
        }
      } else if(other_nodes[n]->index > node->index) {
        continue;
      }
      other_nodes[n] = other_nodes[n]->next;
    }

    if(node) {
        prev = node;
        node = node->next;
    }
  } while (remaining > 0);

  free(other_nodes);
}

void
bs_copy(BS_State *bs, BS_SetID set_id, BS_SetID src_set_id)
{
  assert( set_id <= bs->max_set_id );

  BS_Node *node = bs->sets[set_id], *prev = NULL;

  if(node) destroy_node(node, NULL, &bs->sets[set_id], true);

  for(node = bs->sets[src_set_id]; node; node = node->next) {
    BS_Node *n = new_node( node->index, node->block, NULL, prev, &bs->sets[set_id] );
    prev = n;
  }
}

void
bs_clear(BS_State *bs, BS_SetID set_id)
{
  assert(set_id <= bs->max_set_id);
  if(bs->sets[set_id]) destroy_node(bs->sets[set_id], NULL, NULL, true);
}

void
bs_reset(BS_State *bs)
{
  for(BS_SetID i = 0; i <= bs->max_set_id; ++i) {
    bs_clear(bs, i);
  }
}
