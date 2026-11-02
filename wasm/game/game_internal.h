#ifndef GAME_INTERNAL_H
#define GAME_INTERNAL_H

/* Gameplay-internal aggregator.
 *
 * The original Phase 1 umbrella header has been split into per-module
 * headers.  Translation units may include the specific header they need;
 * including this file keeps the existing module source simple and gives the
 * old name a stable, small definition.
 */
#include "objects.h"
#include "object.h"
#include "cell.h"
#include "score.h"
#include "shell.h"
#include "sound.h"
#include "sound_ids.h"

#include "state.h"
#include "math.h"
#include "cell_ops.h"
#include "object_ops.h"
#include "physics.h"
#include "combat.h"
#include "cell_event.h"
#include "handlers.h"
#include "level.h"
#include "update.h"
#include "save.h"
#include "rng.h"
#include "sound_ops.h"

#endif /* GAME_INTERNAL_H */
