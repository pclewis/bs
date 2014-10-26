#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "bs.h"

void
test_bs_add()
{
  BS_State bs;
  BS_Node* sets[5] = {0};
  bs.sets = sets;
  bs.max_set_id = 4;

  uint ns[4] = {1,2,80000,80001};

  bs_add( &bs, 1, 4, ns );

  assert( bs.sets[1] != NULL );
  assert( bs.sets[1]->block != NULL );
  assert( bs.sets[1]->index == 0 );
  assert( bs.sets[1]->block->slots[0] == 3 );
  assert( bs.sets[1]->next != NULL );
  assert( bs.sets[1]->next->block != NULL );
  assert( bs.sets[1]->next->index == (80000 / 1024) );
  assert( bs.sets[1]->next->next == NULL );

  size_t n_vs;
  uint *uints = bs_to_uints( &bs, 1, &n_vs );
  assert( n_vs == 4 );
  assert( memcmp(ns, uints, sizeof(ns)) == 0 );
  free(uints);
}

void
test_bs_intersection()
{
  BS_State bs;
  BS_Node *sets[5] = {0};
  bs.sets = sets;
  bs.max_set_id = 4;

  uint ns1[25] = {65,343,1084,2752,4133,10293,10439,11143,11938,12223,12837,
                  13020,13111,15325,15388,15427,16791,18050,19083,19213,21752,
                  25447,26898,42000,90210};
  uint ns2[25] = {167,1372,5467,6061,8073,12413,15138,16140,19720,20580,21264,
                  22654,24339,25377,26035,26447,26476,26582,26944,28540,29817,
                  29987,30970,42000,90212};
  uint ns3[5] = {64,1372,31975,29965,42000};

  bs_add( &bs, 1, 25, ns1 );
  bs_add( &bs, 2, 25, ns2 );
  bs_add( &bs, 3, 5,  ns3 );

  uint ni[3] = {1, 2, 3};
  bs_add( &bs, 0, 25, ns1 );
  bs_intersection( &bs, 0, 1, ni); /* 1 */

  size_t n_vs;
  uint *uints;

  uints = bs_to_uints( &bs, 0, &n_vs );
  assert( n_vs == 25 );
  assert( memcmp(ns1, uints, sizeof(ns1)) == 0);
  free(uints);

  bs_add( &bs, 0, 25, ns1 );
  bs_intersection( &bs, 0, 2, ni); /* 1, 2 */
  uints = bs_to_uints( &bs, 0, &n_vs );
  assert( n_vs == 1 );
  assert( uints[0] == 42000 );
  free(uints);
}

void
test_bs_union()
{
  BS_State bs;
  BS_Node *sets[5] = {0};
  bs.sets = sets;
  bs.max_set_id = 4;

  uint ns1[25] = {65,343,1084,2752,4133,10293,10439,11143,11938,12223,12837,
                  13020,13111,15325,15388,15427,16791,18050,19083,19213,21752,
                  25447,26898,42000,90210};
  uint ns2[25] = {167,1372,5467,6061,8073,12413,15138,16140,19720,20580,21264,
                  22654,24339,25377,26035,26447,26476,26582,26944,28540,29817,
                  29987,30970,42000,90212};
  uint ns3[2] = {1,99999};

  bs_add( &bs, 1, 25, ns1 );
  bs_add( &bs, 2, 25, ns2 );
  bs_add( &bs, 3, 2,  ns3 );

  uint ni[2] = {2, 3};
  bs_union( &bs, 1, 2, ni);

  size_t n_vs;
  uint *uints;
  uints = bs_to_uints( &bs, 1, &n_vs );
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
}

void
test_bs_copy()
{
  BS_State bs;
  BS_Node *sets[5] = {0};
  bs.sets = sets;
  bs.max_set_id = 4;

  uint ns1[25] = {65,343,1084,2752,4133,10293,10439,11143,11938,12223,12837,
                  13020,13111,15325,15388,15427,16791,18050,19083,19213,21752,
                  25447,26898,42000,90210};
  uint ns2[25] = {167,1372,5467,6061,8073,12413,15138,16140,19720,20580,21264,
                  22654,24339,25377,26035,26447,26476,26582,26944,28540,29817,
                  29987,30970,42000,90212};
  bs_add( &bs, 1, 25, ns1 );
  bs_add( &bs, 2, 25, ns2 );

  bs_copy( &bs, 3, 2 );
  bs_copy( &bs, 4, 1 );

  for (BS_Node *n1 = bs.sets[1], *n2 = bs.sets[4]; n1 && n2; n1 = n1->next, n2 = n2->next) {
    assert(n1->index == n2->index);
    assert(n1->block == n2->block);
    assert(n1->block->ref_count == 1);
  }

  for (BS_Node *n1 = bs.sets[2], *n2 = bs.sets[3]; n1 && n2; n1 = n1->next, n2 = n2->next) {
    assert(n1->index == n2->index);
    assert(n1->block == n2->block);
    assert(n1->block->ref_count == 1);
  }
}

int
main()
{
  test_bs_add();
  test_bs_intersection();
  test_bs_union();
  test_bs_copy();
  printf("All tests completed successfully.\n");
  return 0;
}
