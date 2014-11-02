#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include "bs.h"

static void
test_bs_add()
{
  BS_State *bs = bs_new(5, 100000);

  uint ns[4] = {1,2,80000,80001};

  bs_add( bs, 1, 4, ns );

  assert( bs->sets[1].first != NULL );
  assert( bs->sets[1].first->block != NULL );
  assert( bs->sets[1].first->index == 0 );
  assert( bs->sets[1].first->block->slots[0] == 3 );
  assert( bs->sets[1].first->next != NULL );
  assert( bs->sets[1].first->next->block != NULL );
  assert( bs->sets[1].first->next->index == (80000 / GROUP_SIZE) );
  assert( bs->sets[1].first->next->next == NULL );

  size_t n_vs;
  uint *uints = bs_to_uints( bs, 1, &n_vs );
  assert( n_vs == 4 );
  assert( memcmp(ns, uints, sizeof(ns)) == 0 );
  free(uints);
  bs_destroy(bs);
}

static void
test_bs_intersection()
{
  BS_State *bs = bs_new(5, 100000);

  uint ns1[25] = {65,343,1084,2752,4133,10293,10439,11143,11938,12223,12837,
                  13020,13111,15325,15388,15427,16791,18050,19083,19213,21752,
                  25447,26898,42000,90210};
  uint ns2[25] = {167,1372,5467,6061,8073,12413,15138,16140,19720,20580,21264,
                  22654,24339,25377,26035,26447,26476,26582,26944,28540,29817,
                  29987,30970,42000,90212};
  uint ns3[5] = {64,1372,31975,29965,42000};

  bs_add( bs, 1, 25, ns1 );
  bs_add( bs, 2, 25, ns2 );
  bs_add( bs, 3, 5,  ns3 );

  uint ni[3] = {1, 2, 3};
  bs_add( bs, 0, 25, ns1 );
  bs_intersection( bs, 0, 1, ni); /* 1 */

  size_t n_vs;
  uint *uints;

  uints = bs_to_uints( bs, 0, &n_vs );
  assert( n_vs == 25 );
  assert( memcmp(ns1, uints, sizeof(ns1)) == 0);
  free(uints);

  bs_add( bs, 0, 25, ns1 );
  bs_intersection( bs, 0, 2, ni); /* 1, 2 */
  uints = bs_to_uints( bs, 0, &n_vs );
  assert( n_vs == 1 );
  assert( uints[0] == 42000 );
  free(uints);
  bs_destroy(bs);
}

static void
test_bs_union()
{
  BS_State *bs = bs_new(5, 100000);

  uint ns1[25] = {65,343,1084,2752,4133,10293,10439,11143,11938,12223,12837,
                  13020,13111,15325,15388,15427,16791,18050,19083,19213,21752,
                  25447,26898,42000,90210};
  uint ns2[25] = {167,1372,5467,6061,8073,12413,15138,16140,19720,20580,21264,
                  22654,24339,25377,26035,26447,26476,26582,26944,28540,29817,
                  29987,30970,42000,90212};
  uint ns3[2] = {1,99999};

  bs_add( bs, 1, 25, ns1 );
  bs_add( bs, 2, 25, ns2 );
  bs_add( bs, 3, 2,  ns3 );

  uint ni[2] = {2, 3};
  bs_union( bs, 1, 2, ni);

  size_t n_vs;
  uint *uints;
  uints = bs_to_uints( bs, 1, &n_vs );
  assert( n_vs == 51 );
  for(uint i = 0, *n1=ns1, *n2=ns2, *n3=ns3; i < n_vs; ++i) {
    if( uints[i] == 42000 ) {
      ++n1, ++n2;
    } else if( uints[i] == *n1 ) {
      ++n1;
    } else if ( uints[i] == *n2 ) {
      ++n2;
    } else {
      assert( uints[i] == *n3 );
      ++n3;
    }
  }
  free(uints);
  bs_destroy(bs);
}

static void
test_bs_copy()
{
  BS_State *bs = bs_new(5, 100000);

  uint ns1[25] = {65,343,1084,2752,4133,10293,10439,11143,11938,12223,12837,
                  13020,13111,15325,15388,15427,16791,18050,19083,19213,21752,
                  25447,26898,42000,90210};
  uint ns2[25] = {167,1372,5467,6061,8073,12413,15138,16140,19720,20580,21264,
                  22654,24339,25377,26035,26447,26476,26582,26944,28540,29817,
                  29987,30970,42000,90212};
  bs_add( bs, 1, 25, ns1 );
  bs_add( bs, 2, 25, ns2 );

  bs_copy( bs, 3, 2 );
  bs_copy( bs, 4, 1 );

  for (BS_Node *n1 = bs->sets[1].first, *n2 = bs->sets[4].first; n1 && n2; n1 = n1->next, n2 = n2->next) {
    assert(n1->index == n2->index);
    assert(n1->block == n2->block);
    assert(n1->block->ref_count == 1);
  }

  for (BS_Node *n1 = bs->sets[2].first, *n2 = bs->sets[3].first; n1 && n2; n1 = n1->next, n2 = n2->next) {
    assert(n1->index == n2->index);
    assert(n1->block == n2->block);
    assert(n1->block->ref_count == 1);
  }

  bs_destroy(bs);
}

static int
rand_range(int min, int max)
{
  assert(max > min);
  int diff = max - min;
  assert(diff <= RAND_MAX);
  int n = rand() % diff;
  return n + min;
}

static void
print_time_diff(struct timespec *start, struct timespec *stop)
{
  if(start->tv_sec == stop->tv_sec) {
    printf("%d nsec", (int)(stop->tv_nsec - start->tv_nsec));
  } else {
    printf("%lf", (double)((stop->tv_sec - start->tv_sec) + (stop->tv_nsec - start->tv_nsec)/1000000000.0));
  }
}

static void
time_bs_intersection()
{
  struct timespec start, stop;
  BS_State *bs = bs_new(1024, 2500001);

  srand(1);

  clock_gettime(CLOCK_REALTIME, &start);

  uint t_n = 0;
  for(BS_SetID i = 1; i < 1024; ++i) {
    uint n = rand_range(100, 1000000);
    t_n += n;
    uint *vs = calloc(n, sizeof(uint));
    for(size_t j = 0; j < n; ++j) {
      vs[j] = (uint)rand_range(1000, 2500000);
    }
    bs_add( bs, i, n, vs );
    free(vs);
  }
  clock_gettime(CLOCK_REALTIME, &stop);
  printf("bs_add [%u / 1024]: ", t_n);
  print_time_diff(&start, &stop);
  printf("\n");

  uint vs[1024];
  for(uint i = 0; i < 1024; ++i) {
    vs[i] = i;
  }

  clock_gettime(CLOCK_REALTIME, &start);

  for(BS_SetID i = 1; i < 1024; ++i) {
    bs_copy(bs, 0, i);
    bs_intersection(bs, 0, 1024, vs);
    for(BS_Node *node = bs->sets[0].first; node; node = node->next) {
      assert(node->block->pop_count == 0);
    }
  }

  clock_gettime(CLOCK_REALTIME, &stop);
  printf("bs_intersection [1024*1024 -> 0]: ");
  print_time_diff(&start, &stop);
  printf("\n");

  clock_gettime(CLOCK_REALTIME, &start);
  size_t n_s1 = 0;
  uint *v_s1 = bs_to_uints(bs, 1, &n_s1);
  for(BS_SetID i = 2; i < 1024; ++i) {
    bs_add(bs, i, n_s1, v_s1);
  }
  clock_gettime(CLOCK_REALTIME, &stop);
  printf("bs_add [%zu*1024]: ", n_s1);
  print_time_diff(&start, &stop);
  printf("\n");

  clock_gettime(CLOCK_REALTIME, &start);
  for(BS_SetID i = 1; i < 1024; ++i) {
    struct timespec istart, istop;
    bs_copy(bs, 0, i);

    clock_gettime(CLOCK_REALTIME, &istart);
    bs_intersection(bs, 0, 1024, vs);
    clock_gettime(CLOCK_REALTIME, &istop);

    printf("bs_intersection [%u]: ", i);
    print_time_diff(&istart, &istop);
    printf("\n");
  }

  clock_gettime(CLOCK_REALTIME, &stop);
  printf("bs_intersection [1024*1024 -> %zu]: ", n_s1);
  print_time_diff(&start, &stop);
  printf("\n");

  size_t n_s0 = 0;
  uint *v_s0 = bs_to_uints(bs, 0, &n_s0);
  assert(n_s0 == n_s1);
  assert( memcmp(v_s0, v_s1, n_s0 * sizeof(uint)) == 0 );
  free(v_s1); v_s1 = NULL;
  free(v_s0); v_s0 = NULL;


  bs_destroy(bs);
}

int
main()
{
  test_bs_add();
  test_bs_intersection();
  test_bs_union();
  test_bs_copy();

  time_bs_intersection();
  printf("All tests completed successfully.\n");
  return 0;
}
