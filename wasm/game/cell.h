#ifndef GAME_CELL_H
#define GAME_CELL_H

/* Hand-written cell geometry and record layout.
 *
 * The generated cells_data.h includes this header instead of redefining the
 * dimensions and flag offsets.  Keep the level dimensions here in sync with
 * the loader/oracle output; the generated tables are keyed by these values.
 */
#include <stdint.h>

#include "cell_fields.h"

#define CELL_COLS 40
#define CELL_ROWS 25
#define CELL_LEVELS 19

#endif /* GAME_CELL_H */
