#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include "bs.h"

static void
test_bs_add()
{
  BS_State *bs = bs_new(5, 100000);

  nuint ns0[4] = {1,2,80000,80001};
  nuint ns1[25] = {65,343,1084,2752,4133,10293,10439,11143,11938,12223,12837,
                  13020,13111,15325,15388,15427,16791,18050,19083,19213,21752,
                  25447,26898,42000,90210};
  nuint ns2[25] = {167,1372,5467,6061,8073,12413,15138,16140,19720,20580,21264,
                  22654,24339,25377,26035,26447,26476,26582,26944,28540,29817,
                  29987,30970,42000,90212};

  bs_add( bs, 0,  4, ns0 );
  bs_add( bs, 1, 25, ns1 );
  bs_add( bs, 2, 25, ns2 );

  size_t n_vs;
  nuint *nuints = bs_to_nuints( bs, 0, &n_vs );
  assert( n_vs == 4 );
  assert( memcmp(ns0, nuints, sizeof(ns0)) == 0 );
  free(nuints);

  nuints = bs_to_nuints( bs, 1, &n_vs );
  assert( n_vs == 25 );
  assert( memcmp(ns1, nuints, sizeof(ns1)) == 0 );
  free(nuints);

  nuints = bs_to_nuints( bs, 2, &n_vs );
  assert( n_vs == 25 );
  assert( memcmp(ns2, nuints, sizeof(ns2)) == 0 );
  free(nuints);

  bs_destroy(bs);
}

static void
test_bs_intersection()
{
  BS_State *bs = bs_new(5, 100000);

  nuint ns1[25] = {65,343,1084,2752,4133,10293,10439,11143,11938,12223,12837,
                  13020,13111,15325,15388,15427,16791,18050,19083,19213,21752,
                  25447,26898,42000,90210};
  nuint ns2[25] = {167,1372,5467,6061,8073,12413,15138,16140,19720,20580,21264,
                  22654,24339,25377,26035,26447,26476,26582,26944,28540,29817,
                  29987,30970,42000,90212};
  nuint ns3[5] = {64,1372,31975,29965,42000};

  bs_add( bs, 1, 25, ns1 );
  bs_add( bs, 2, 25, ns2 );
  bs_add( bs, 3, 5,  ns3 );

  nuint ni[3] = {1, 2, 3};
  bs_add( bs, 0, 25, ns1 );
  bs_intersection( bs, 0, 1, ni); // 1

  size_t n_vs;
  nuint *nuints;

  nuints = bs_to_nuints( bs, 0, &n_vs );
  assert( n_vs == 25 );
  assert( memcmp(ns1, nuints, sizeof(ns1)) == 0);
  free(nuints);

  bs_add( bs, 0, 25, ns1 );
  bs_intersection( bs, 0, 2, ni); // 1,2
  nuints = bs_to_nuints( bs, 0, &n_vs );
  assert( n_vs == 1 );
  assert( nuints[0] == 42000 );
  free(nuints);
  bs_destroy(bs);
}

/*
static void
test_bs_union()
{
  BS_State *bs = bs_new(5, 100000);

  nuint ns1[25] = {65,343,1084,2752,4133,10293,10439,11143,11938,12223,12837,
                  13020,13111,15325,15388,15427,16791,18050,19083,19213,21752,
                  25447,26898,42000,90210};
  nuint ns2[25] = {167,1372,5467,6061,8073,12413,15138,16140,19720,20580,21264,
                  22654,24339,25377,26035,26447,26476,26582,26944,28540,29817,
                  29987,30970,42000,90212};
  nuint ns3[2] = {1,99999};

  bs_add( bs, 1, 25, ns1 );
  bs_add( bs, 2, 25, ns2 );
  bs_add( bs, 3, 2,  ns3 );

  nuint ni[2] = {2, 3};
  bs_union( bs, 1, 2, ni);

  size_t n_vs;
  nuint *nuints;
  nuints = bs_to_nuints( bs, 1, &n_vs );
  assert( n_vs == 51 );
  for(nuint i = 0, *n1=ns1, *n2=ns2, *n3=ns3; i < n_vs; ++i) {
    if( nuints[i] == 42000 ) {
      ++n1, ++n2;
    } else if( nuints[i] == *n1 ) {
      ++n1;
    } else if ( nuints[i] == *n2 ) {
      ++n2;
    } else {
      assert( nuints[i] == *n3 );
      ++n3;
    }
  }
  free(nuints);
  bs_destroy(bs);
}
*/

static void
test_bs_copy()
{
  BS_State *bs = bs_new(5, 100000);

  nuint ns1[25] = {65,343,1084,2752,4133,10293,10439,11143,11938,12223,12837,
                  13020,13111,15325,15388,15427,16791,18050,19083,19213,21752,
                  25447,26898,42000,90210};
  nuint ns2[25] = {167,1372,5467,6061,8073,12413,15138,16140,19720,20580,21264,
                  22654,24339,25377,26035,26447,26476,26582,26944,28540,29817,
                  29987,30970,42000,90212};
  bs_add( bs, 1, 25, ns1 );
  bs_add( bs, 2, 25, ns2 );

  bs_copy( bs, 3, 2 );
  bs_copy( bs, 4, 1 );

  size_t n_vs;
  nuint *nuints = bs_to_nuints( bs, 3, &n_vs );
  assert( n_vs == 25 );
  assert( memcmp( nuints, ns2, n_vs*sizeof(nuint)) == 0 );
  free(nuints);

  nuints = bs_to_nuints( bs, 4, &n_vs );
  assert( n_vs == 25 );
  assert( memcmp( nuints, ns1, n_vs*sizeof(nuint)) == 0 );
  free(nuints);

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
  printf("%lf", (double)((stop->tv_sec - start->tv_sec) + (stop->tv_nsec - start->tv_nsec)/1000000000.0));
}

static void
time_bs_intersection()
{
  struct timespec start, stop;
  BS_State *bs = bs_new(1024, 2500001);

  srand(1);

  clock_gettime(CLOCK_REALTIME, &start);

  nuint t_n = 0;
  for(BS_SetID i = 1; i < 1024; ++i) {
    nuint n = rand_range(100, 1000000);
    t_n += n;
    nuint *vs = calloc(n, sizeof(nuint));
    for(size_t j = 0; j < n; ++j) {
      vs[j] = (nuint)rand_range(1000, 2500000);
    }
    bs_add( bs, i, n, vs );
    //printf("%lu: %lu\n", i, n);
    free(vs);
  }
  clock_gettime(CLOCK_REALTIME, &stop);
  printf("bs_add [%lu / 1024]: ", t_n);
  print_time_diff(&start, &stop);
  printf("\n");

  nuint vs[1024];
  for(nuint i = 0; i < 1024; ++i) {
    vs[i] = i;
  }

  clock_gettime(CLOCK_REALTIME, &start);

  for(BS_SetID i = 1; i < 1024; ++i) {
    bs_copy(bs, 0, i);
    bs_intersection(bs, 0, 1024, vs);
  }

  clock_gettime(CLOCK_REALTIME, &stop);
  printf("bs_intersection [1024*1024 -> 0]: ");
  print_time_diff(&start, &stop);
  printf("\n");

  clock_gettime(CLOCK_REALTIME, &start);
  size_t n_s1 = 0;
  nuint *v_s1 = bs_to_nuints(bs, 1, &n_s1);
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

    printf("bs_intersection [%lu]: ", i);
    print_time_diff(&istart, &istop);
    printf("\n");
  }

  clock_gettime(CLOCK_REALTIME, &stop);
  printf("bs_intersection [1024*1024 -> %zu]: ", n_s1);
  print_time_diff(&start, &stop);
  printf("\n");

  size_t n_s0 = 0;
  nuint *v_s0 = bs_to_nuints(bs, 0, &n_s0);
  assert(n_s0 == n_s1);
  assert( memcmp(v_s0, v_s1, n_s0 * sizeof(nuint)) == 0 );
  free(v_s1); v_s1 = NULL;
  free(v_s0); v_s0 = NULL;


  bs_destroy(bs);
}

int
main()
{
  test_bs_add(); printf("bs_add: success\n");
  test_bs_intersection(); printf("bs_intersection: success\n");
  test_bs_copy(); printf("bs_copy: success\n");

  time_bs_intersection();
  printf("All tests completed successfully.\n");
  return 0;
}
