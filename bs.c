#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

// adapted from comp.lang.c FAQ Q 20.8
#define BITMASK(b) (1 << ((b) % WORD_BIT))
#define BITSLOT(b) ((b) / WORD_BIT)
#define BITSET(a, b) ((a)[BITSLOT(b)] |= BITMASK(b))
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
find_bucket(uint set_n, uint bucket_n, BucketList *bl, bool create)
{
  BucketList *prev = NULL;
  if(bl == NULL || bl->number > bucket_n)
    bl = g_sets[set_n];

  for(; bl != NULL; prev = bl, bl = bl->next) {
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

static void
print_set(FILE *fp, uint set_n) {
  for(BucketList *bl = g_sets[set_n]; bl; bl = bl->next) {
    uint base = bl->number * GROUP_SIZE;
    for(uint i = 0; i < GROUP_SIZE; ++i) {
      if(BITTEST(bl->bucket->slots, i))
        fprintf(fp, "%u ", base + i);
    }
  }
  fprintf(fp, "0\n");
}

static void
dump(FILE *fp) {
  for(uint i = 0; i < MAX_SETS; ++i) {
    if(g_sets[i] != NULL) {
      fprintf(fp, "+ %u ", i);
      print_set(fp, i);
    }
  }
}

static inline uint
next_uint(FILE *fp)
{
  uint n = 0;
  fscanf(fp, "%u", &n);
  return n;
}

static void
main_loop(FILE *fp)
{
  while(true) {
    char cmd;
    fscanf(fp, " "); // eat whitespace
    if(fscanf(fp, "%c", &cmd) == EOF) break;
    switch(cmd) {
    case '+': /* add to set */
      {
        uint set_n = next_uint(fp);
        BucketList *bl = NULL;
        for (uint i = next_uint(fp); i != 0; i = next_uint(fp)) {
          if(i > MAX_BIT) {
            printf("%u is too high, ignored\n", i);
            continue;
          }
          uint bucket_n = i / GROUP_SIZE;
          if(bl == NULL || bl->number != bucket_n)
            bl = find_bucket(set_n, i / GROUP_SIZE, bl, true);
          prepare_to_change(bl);
          BITSET(bl->bucket->slots, i % GROUP_SIZE);
        }
      }
      break;
    case '-': /* remove from set */
      {
        uint set_n = next_uint(fp);
        for (uint i = next_uint(fp); i != 0; i = next_uint(fp)) {
          if(i > MAX_BIT) {
            printf("%u is too high, ignored\n", i);
            continue;
          }
          BucketList *bl = find_bucket(set_n, i / GROUP_SIZE, NULL, false);
          if(bl) {
            prepare_to_change(bl);
            BITCLEAR(bl->bucket->slots, i % GROUP_SIZE);
          }
        }
      }
      break;
    case '&': /* intersection */
      {
        uint to_set_n = next_uint(fp);
        for (uint set_n = next_uint(fp); set_n != 0; set_n = next_uint(fp)) {
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
        uint to_set_n = next_uint(fp);
        for (uint set_n = next_uint(fp); set_n != 0; set_n = next_uint(fp)) {
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
        uint to_set_n = next_uint(fp);
        uint from_set_n = next_uint(fp);
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
      print_set(stdout, next_uint(fp) );
      break;
    case 'd': /* dump */
      dump(stdout);
      break;
    case '0': /* nop */
      break;
    default:
      printf("Unrecognized command: %c\n", cmd);
      break;
    }
    fflush(stdout);
  }
}

static void
interrupt(int sig)
{
  fclose(stdin);
  signal(sig, SIG_IGN);
}

int
main(int argc, char *argv[])
{
  g_sets = calloc(MAX_SETS, sizeof(BucketList*));

  if(argc > 1) {
    FILE *fp = fopen(argv[1], "r");
    if(fp != NULL) {
      main_loop(fp);
      fclose(fp);
    } else {
      fprintf(stderr, "Cannot read %s\n", argv[1]);
    }
  }

  signal(SIGINT, interrupt);

  main_loop(stdin);

  if(argc > 2) {
    FILE *fp = fopen(argv[2], "w");
    if(fp != NULL) {
      dump(fp);
      fclose(fp);
    } else {
      fprintf(stderr, "Cannot write %s\n", argv[1]);
    }
  }

  return 0;
}
