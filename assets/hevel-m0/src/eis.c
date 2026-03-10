#include "hevel.h"

#include <libeis.h>

struct eis_state eis_state = {0};

int
eis_initialize(void)
{
  if (eis_state.ctx) {
    eis_state.available = true;
    return 0;
  }

  eis_state.ctx = eis_new(NULL);
  if (!eis_state.ctx) {
    fprintf(stderr, "hevel: cannot initialize EIS scaffold\n");
    eis_state.available = false;
    return -1;
  }

  eis_state.available = true;
  return 0;
}

void
eis_finalize(void)
{
  if (eis_state.ctx) {
    eis_unref(eis_state.ctx);
    eis_state.ctx = NULL;
  }

  eis_state.available = false;
}
