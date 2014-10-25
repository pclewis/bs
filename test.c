#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <assert.h>
#include "bs.h"

void
test_bs_add()
{
  BS_State bs;
  bs.sets = calloc(5, sizeof(BS_Node*));
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
}

int
main()
{
  test_bs_add();
  printf("All tests completed successfully.\n");
  return 0;
}
