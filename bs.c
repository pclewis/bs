#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>

// adapted from comp.lang.c FAQ Q 20.8
#define BITMASK(b) (1 << ((b) % WORD_BIT))
#define BITSLOT(b) ((b) / WORD_BIT)
#define BITSET(a, b) ((a)[BITSLOT(b)] |= BITMASK(b))
#define BITCLEAR(a, b) ((a)[BITSLOT(b)] &= ~BITMASK(b))
#define BITTEST(a, b) ((a)[BITSLOT(b)] & BITMASK(b))
#define BITNSLOTS(nb) ((nb + WORD_BIT - 1) / WORD_BIT)

#define MAX_BIT (1<<24)
#define GROUP_SIZE (1<<12)
#define MAX_SETS (1<<22)

typedef unsigned int uint;

typedef struct {
  uint ref_count; /* 0 = no shared references */
  uint slots[BITNSLOTS(GROUP_SIZE)];
} Bucket;

typedef struct _BL BucketList;
struct _BL {
  uint number;
  Bucket *bucket;
  BucketList *next;
};

BucketList **g_sets;

static BucketList *
new_bucket_list(uint bucket_n, bool new_bucket) {
    BucketList *bl = calloc(1, sizeof(BucketList));
    bl->number = bucket_n;
    if(new_bucket) bl->bucket = calloc(1, sizeof(Bucket));
    return bl;
}

static BucketList *
find_bucket(uint set_n, uint bucket_n, bool create)
{
  BucketList *prev = NULL, *bl = NULL;

  for(bl = g_sets[set_n]; bl != NULL; prev = bl, bl = bl->next) {
    if(bl->number == bucket_n) return bl;
    if(bl->number > bucket_n) break;
  }

  if(create) {
    BucketList *new_bl = new_bucket_list(bucket_n, true);
    if(prev) prev->next = new_bl;
    else g_sets[set_n] = new_bl;
    new_bl->next = bl;
    return new_bl;
  }

  return NULL;
}

static void
prepare_to_change(BucketList *bl)
{
  if(bl->bucket->ref_count > 0) {
    Bucket *clone = malloc(sizeof(Bucket));
    memcpy(clone->slots, bl->bucket->slots, sizeof(clone->slots));
    clone->ref_count = 0;
    bl->bucket->ref_count -= 1;
    bl->bucket = clone;
  }
}

static void
free_bucket_list(BucketList *bl)
{
  for(BucketList *next; bl != NULL; bl = next) {
    next = bl->next;
    if(bl->bucket->ref_count == 0) free(bl->bucket);
    else bl->bucket->ref_count -= 1;
    free(bl);
  }
}

static inline uint
next_uint()
{
  uint n = 0;
  scanf("%u", &n);
  return n;
}

int
main()
{
  g_sets = calloc(MAX_SETS, sizeof(BucketList*));

  while(true) {
    char cmd;
    scanf(" "); // eat whitespace
    if(scanf("%c", &cmd) == EOF) break;
    switch(cmd) {
    case '+': /* add to set */
      {
        uint set_n = next_uint();
        for (uint i = next_uint(); i != 0; i = next_uint()) {
          if(i > MAX_BIT) {
            printf("%u is too high, ignored\n", i);
            continue;
          }
          BucketList *bl = find_bucket(set_n, i / GROUP_SIZE, true);
          prepare_to_change(bl);
          BITSET(bl->bucket->slots, i % GROUP_SIZE);
        }
      }
      break;
    case '-': /* remove from set */
      {
        uint set_n = next_uint();
        for (uint i = next_uint(); i != 0; i = next_uint()) {
          if(i > MAX_BIT) {
            printf("%u is too high, ignored\n", i);
            continue;
          }
          BucketList *bl = find_bucket(set_n, i / GROUP_SIZE, false);
          if(bl) {
            prepare_to_change(bl);
            BITCLEAR(bl->bucket->slots, i % GROUP_SIZE);
          }
        }
      }
      break;
    case '&': /* intersection */
      {
        uint to_set_n = next_uint();
        for (uint set_n = next_uint(); set_n != 0; set_n = next_uint()) {
          BucketList *to_bl = g_sets[to_set_n];
          BucketList *to_prev = NULL;
          BucketList *other_bl = g_sets[set_n];
          while(to_bl) {
            if(other_bl == NULL) { /* no buckets left on other side, drop all of our remaining buckets */
              if(to_prev == NULL) g_sets[to_set_n] = NULL;
              else to_prev->next = NULL;
              free_bucket_list(to_bl);
              break;
            }

            if(other_bl->number > to_bl->number) { /* other side ahead of us, drop our bucket */
              BucketList *next = to_bl->next;
              if(to_prev == NULL) g_sets[to_set_n] = to_bl->next;
              else to_prev->next = to_bl->next;
              to_bl->next = NULL;
              free_bucket_list(to_bl);
              to_bl = next;
              continue;
            }

            if(to_bl->number > other_bl->number) { /* other side behind us, catch up */
              other_bl = other_bl->next;
              continue;
            }

            /* to_bl->number == other_bl->number */
            if(to_bl->bucket != other_bl->bucket) {
                prepare_to_change(to_bl);
                for(int i = BITNSLOTS(GROUP_SIZE); i-- > 0; ) {
                  to_bl->bucket->slots[i] &= other_bl->bucket->slots[i];
                }
            }
            to_prev = to_bl;
            to_bl = to_bl->next;
            other_bl = other_bl->next;
          }
        }
      }
      break;
    case '|': /* union */
      {
        uint to_set_n = next_uint();
        for (uint set_n = next_uint(); set_n != 0; set_n = next_uint()) {
          BucketList *to_bl = g_sets[to_set_n];
          BucketList *to_prev = NULL;
          BucketList *other_bl = g_sets[set_n];
          while(to_bl) {
            if(other_bl == NULL) { /* no buckets left on other side, all done */
              break;
            }

            if(other_bl->number > to_bl->number) { /* other side ahead of us, skip ahead */
              to_prev = to_bl;
              to_bl = to_bl->next;
              continue;
            }

            if(to_bl->number > other_bl->number) { /* other side behind us, clone and inject */
              BucketList *new_bl = new_bucket_list(other_bl->number, false);
              new_bl->bucket = other_bl->bucket;
              new_bl->bucket->ref_count += 1;
              if(to_prev) to_prev->next = new_bl;
              else g_sets[to_set_n] = new_bl;
              new_bl->next = to_bl;
              to_prev = new_bl;
              other_bl = other_bl->next;
              continue;
            }

            /* to_bl->number == other_bl->number */
            if(to_bl->bucket != other_bl->bucket) {
                prepare_to_change(to_bl);
                for(int i = BITNSLOTS(GROUP_SIZE); i-- > 0; ) {
                  to_bl->bucket->slots[i] |= other_bl->bucket->slots[i];
                }
            }
            to_prev = to_bl;
            to_bl = to_bl->next;
            other_bl = other_bl->next;
          }
        }
      }
      break;
    case '=': /* clone */
      {
        uint to_set_n = next_uint();
        uint from_set_n = next_uint();
        BucketList *prev = NULL;
        if(g_sets[to_set_n] != NULL) free_bucket_list(g_sets[to_set_n]);
        for(BucketList *bl = g_sets[from_set_n], *new_bl; bl != NULL; prev = new_bl, bl = bl->next) {
          new_bl = new_bucket_list(bl->number, false);
          new_bl->bucket = bl->bucket;
          new_bl->bucket->ref_count += 1;
          if(prev == NULL) g_sets[to_set_n] = new_bl;
          else prev->next = new_bl;
        }
      }
      break;
    case 'p': /* print */
      {
        uint set_n = next_uint();
        for(BucketList *bl = g_sets[set_n]; bl; bl = bl->next) {
          uint base = bl->number * GROUP_SIZE;
          for(uint i = 0; i < GROUP_SIZE; ++i) {
            if(BITTEST(bl->bucket->slots, i))
              printf("%u ", base + i);
          }
        }
        printf("\n");
      }
      break;
    default:
      printf("Unrecognized command: %c\n", cmd);
      break;
    }
  }

  return 0;
}
