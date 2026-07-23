// nn_scripted.h
// Rule-based ("scripted") Doom bot: no trained model, no weights.
// Plays using raycasts + monster scan directly, to auto-generate
// training CSVs without manual play.

#ifndef NN_SCRIPTED_H
#define NN_SCRIPTED_H

#include "doomtype.h"
#include "d_player.h"
#include "d_ticcmd.h"

boolean NN_ScriptedInit(void);
void    NN_ScriptedBuildTicCmd(player_t *player, ticcmd_t *cmd);
void    NN_ScriptedShutdown(void);

#endif
