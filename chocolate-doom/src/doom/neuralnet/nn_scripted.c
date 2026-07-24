// nn_scripted.c
// Rule-based bot: greedy raycast navigation + simple line-of-sight combat.
// No trained model or weights involved - used to auto-generate training
// CSVs (via the existing nn_logger.c, unmodified) without manual play.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "nn_scripted.h"
#include "nn_common.h"
#include "doomdef.h"
#include "doomstat.h"
#include "m_fixed.h"
#include "d_event.h"
#include "tables.h"

static const int fwd_map[3]  = { -25, 0, 25 };
static const int side_map[3] = { -20, 0, 20 };
static const int turn_map[5] = { -512, -192, 0, 192, 512 };

#define ENGAGE_RANGE       600.0f
#define AIM_CONE_RATIO       0.6f
#define CLEAR_RAY_DIST     200.0f
#define USE_TRIGGER_DIST    80.0f
#define COMBAT_APPROACH_DIST 250.0f  // hold ground once this close to an engaged monster
#define NAV_BACKOFF_DIST     60.0f   // back off instead of walking forward when this blocked

// Stuck-recovery timing/thresholds (all in tics unless noted; 35 tics = 1s)
#define STUCK_MOVE_THRESHOLD (5 * FRACUNIT)  // net movement below this counts as "not moving"
#define STUCK_DETECT_TICS       35
#define RECOVERY_NORMAL_TICS    70
#define RECOVERY_ESCALATE_TICS 140
#define RECOVERY_SUCCESS_DIST   40.0f  // net displacement above this counts as "actually escaped"

// --- Spatial memory: bias navigation away from well-visited ground ---
// Coarse grid over the map, counting how often each cell has been stood in.
// Cells under the exhaustion limit keep full weight (1.0, i.e. "pick by
// raw openness like before"); only once a cell is revisited past that
// limit does it get discounted, nudging ray selection toward directions
// that lead somewhere less-trodden instead of looping the same room.
#define VISIT_GRID_SIZE   256
#define VISIT_CELL_SIZE   (128 * FRACUNIT)
#define VISIT_EXHAUST_LIMIT 4
#define VISIT_PENALTY      0.35f

// 256*256 bytes = 64KB. Fine here: this file only ever compiles into the
// desktop/WSL chocolate-doom build (for -scriptedbot data collection), never
// into the embedded STM32N6 target, which only runs the trained network.
static unsigned char visit_grid[VISIT_GRID_SIZE][VISIT_GRID_SIZE];

static void visit_cell_index(fixed_t x, fixed_t y, int *cx, int *cy)
{
    long ix = (long)(x / VISIT_CELL_SIZE) + VISIT_GRID_SIZE / 2;
    long iy = (long)(y / VISIT_CELL_SIZE) + VISIT_GRID_SIZE / 2;

    if (ix < 0) ix = 0;
    if (ix >= VISIT_GRID_SIZE) ix = VISIT_GRID_SIZE - 1;
    if (iy < 0) iy = 0;
    if (iy >= VISIT_GRID_SIZE) iy = VISIT_GRID_SIZE - 1;

    *cx = (int)ix;
    *cy = (int)iy;
}

static void visit_mark(fixed_t x, fixed_t y)
{
    int cx, cy;
    visit_cell_index(x, y, &cx, &cy);
    if (visit_grid[cx][cy] < 255)
        visit_grid[cx][cy]++;
}

static float visit_weight(fixed_t x, fixed_t y)
{
    int cx, cy;
    visit_cell_index(x, y, &cx, &cy);
    return (visit_grid[cx][cy] <= VISIT_EXHAUST_LIMIT) ? 1.0f : VISIT_PENALTY;
}

static boolean scripted_active = false;

boolean NN_ScriptedInit(void)
{
    scripted_active = true;
    srand((unsigned int)time(NULL));
    fprintf(stderr, "NN_Scripted: initialized (rule-based, no trained model)\n");
    return true;
}

void NN_ScriptedShutdown(void)
{
    scripted_active = false;
    fprintf(stderr, "NN_Scripted: shutdown\n");
}

// Bucket a lateral/forward offset into the same 5 turn classes nn_player.c
// uses (0=hard right .. 4=hard left), so nn_logger.c's discretization sees
// a clean class regardless.
static int turn_class_towards(float dy, float dx)
{
    float angle = atan2f(dy, dx > 0.0f ? dx : 1.0f);

    if (angle > 0.6f)   return 4;
    if (angle > 0.15f)  return 3;
    if (angle < -0.6f)  return 0;
    if (angle < -0.15f) return 1;
    return 2;
}

static int turn_class_from_ray(int best_ray)
{
    int turn_units = (best_ray <= 8) ? best_ray * 128 : -(16 - best_ray) * 128;

    if (turn_units > 384)       return 4;
    else if (turn_units > 64)   return 3;
    else if (turn_units < -384) return 0;
    else if (turn_units < -64)  return 1;
    else                        return 2;
}

// Picks the ray with the best (distance x visit-weight) score: still
// prefers open space like before, but a well-trodden opening now loses
// out to a less-open direction that leads somewhere new.
static int best_open_ray(player_t *player, const float *features)
{
    mobj_t *plmo = player->mo;
    angle_t base = plmo->angle;
    angle_t step = ANG_MAX / NN_NUM_RAYS;

    int best_ray = 0;
    float best_score = -1.0f;

    for (int i = 0; i < NN_NUM_RAYS; i++)
    {
        float dist = features[5 + i];
        angle_t ang = base + step * i;

        fixed_t ex = plmo->x + FixedMul((fixed_t)(dist * FRACUNIT), finecosine[ang >> ANGLETOFINESHIFT]);
        fixed_t ey = plmo->y + FixedMul((fixed_t)(dist * FRACUNIT), finesine[ang >> ANGLETOFINESHIFT]);

        float score = dist * visit_weight(ex, ey);
        if (score > best_score)
        {
            best_score = score;
            best_ray = i;
        }
    }
    return best_ray;
}

void NN_ScriptedBuildTicCmd(player_t *player, ticcmd_t *cmd)
{
    static const float dummy_last_action[5] = { 1.0f, 1.0f, 2.0f, 0.0f, 0.0f };
    float features[NN_NUM_FEATURES];

    memset(cmd, 0, sizeof(*cmd));

    if (!scripted_active || !player->mo)
        return;

    NN_BuildFeatures(player, dummy_last_action, features);

    float ray0 = features[5];  // straight ahead

    int fwd_class = 1, side_class = 1, turn_class = 2;
    int fire = 0, use = 0;
    boolean engaging = false;

    // --- Combat: nearest monster present and roughly ahead? ---
    float m_dx = features[21], m_dy = features[22], m_present = features[23];

    if (m_present > 0.5f && m_dx > 0.0f)
    {
        float dist = sqrtf(m_dx * m_dx + m_dy * m_dy);
        if (dist < ENGAGE_RANGE)
        {
            engaging = true;
            turn_class = turn_class_towards(m_dy, m_dx);

            if (fabsf(m_dy) < AIM_CONE_RATIO * m_dx)
                fire = 1;

            fwd_class = (dist > COMBAT_APPROACH_DIST) ? 2 : 1;
        }
    }

    // --- Navigation: no monster to fight, follow the most open ray ---
    // Turn decisions are committed for NAV_TURN_HOLD tics instead of being
    // recomputed every tic: recomputing every tic can "hunt" forever at a
    // spot where two directions look equally open (e.g. a corner), since
    // the decision flips back before either turn completes.
    {
        static int nav_turn_class = 2;
        static int nav_hold = 0;
        #define NAV_TURN_HOLD 20

        if (!engaging)
        {
            if (nav_hold <= 0)
            {
                int best_ray = best_open_ray(player, features);
                nav_turn_class = turn_class_from_ray(best_ray);
                nav_hold = NAV_TURN_HOLD;
            }
            nav_hold--;

            turn_class = nav_turn_class;
            fwd_class = (ray0 > CLEAR_RAY_DIST) ? 2 : 1;

            // Blocked ahead and the opening is behind-ish: back off while
            // turning instead of walking into the wall.
            if (ray0 < NAV_BACKOFF_DIST && nav_turn_class != 2)
                fwd_class = 0;
        }
        else
        {
            nav_hold = 0;  // re-evaluate navigation fresh once combat ends
        }
    }

    // --- Use button: something close directly ahead (door/switch) ---
    if (ray0 < USE_TRIGGER_DIST)
        use = 1;

    // --- Stuck-recovery safety net (same approach as nn_player.c, plus a
    // strafe component so the bot can slide off a wall it's pressed flat
    // against, which pure rotation can't fix), with an escalating fallback
    // if normal recovery keeps producing zero net movement at the same
    // spot. That happens where raycasts see an "open" sightline but the
    // player's actual collision radius can't fit through (e.g. a tight
    // corner) - in that case, picking a ray-based direction is pointless,
    // so escalation ignores rays entirely and wiggles forward/backward on
    // a random heading for much longer instead. ---
    {
        static int stuck_count = 0;
        static fixed_t last_x = 0, last_y = 0;
        static int recovery_timer = 0;
        static int recovery_turn_class = 2;
        static int recovery_side_class = 1;
        static fixed_t recovery_start_x = 0, recovery_start_y = 0;
        static int escalation_count = 0;
        static boolean escalating = false;

        fixed_t dx_pos = player->mo->x - last_x;
        fixed_t dy_pos = player->mo->y - last_y;

        visit_mark(player->mo->x, player->mo->y);

        if (abs(dx_pos) < STUCK_MOVE_THRESHOLD && abs(dy_pos) < STUCK_MOVE_THRESHOLD)
            stuck_count++;
        else if (recovery_timer <= 0)
            stuck_count = 0;

        if (stuck_count > STUCK_DETECT_TICS && recovery_timer <= 0)
        {
            recovery_start_x = player->mo->x;
            recovery_start_y = player->mo->y;

            if (escalation_count >= 2)
            {
                escalating = true;
                recovery_timer = RECOVERY_ESCALATE_TICS;
                recovery_turn_class = rand() % 5;
                recovery_side_class = rand() % 3;
            }
            else
            {
                escalating = false;
                recovery_timer = RECOVERY_NORMAL_TICS;

                // Pick the escape direction ONCE per recovery attempt (not
                // every tic) so it can actually be carried out.
                int best_ray = best_open_ray(player, features);
                best_ray = (best_ray + (rand() % 3) - 1) & 15;
                recovery_turn_class = turn_class_from_ray(best_ray);
                recovery_side_class = (best_ray <= 8) ? 2 : 0;  // strafe toward the open side
            }
        }

        if (recovery_timer > 0)
        {
            recovery_timer--;
            turn_class = recovery_turn_class;
            side_class = recovery_side_class;

            if (escalating)
            {
                // Alternate forward/backward every ~20 tics to wiggle out
                // of a tight pocket instead of pushing into the same spot.
                fwd_class = ((recovery_timer / 20) % 2 == 0) ? 2 : 0;
            }
            else
            {
                fwd_class = (recovery_timer > RECOVERY_NORMAL_TICS / 2) ? 1 : 2;
            }

            if (recovery_timer == 0)
            {
                fixed_t moved_dx = player->mo->x - recovery_start_x;
                fixed_t moved_dy = player->mo->y - recovery_start_y;
                float mdx = (float)moved_dx / (float)FRACUNIT;
                float mdy = (float)moved_dy / (float)FRACUNIT;
                float moved = sqrtf(mdx * mdx + mdy * mdy);

                escalation_count = (moved < RECOVERY_SUCCESS_DIST) ? escalation_count + 1 : 0;

                stuck_count = 0;
                escalating = false;
            }
        }

        last_x = player->mo->x;
        last_y = player->mo->y;
    }

    // --- Emit command ---
    cmd->forwardmove = fwd_map[fwd_class];
    cmd->sidemove    = side_map[side_class];
    cmd->angleturn   = turn_map[turn_class];
    cmd->buttons     = (fire ? BT_ATTACK : 0) | (use ? BT_USE : 0);

    // Debug output once per second
    {
        static int dbg_count = 0;
        if (dbg_count++ % 35 == 0)
        {
            fprintf(stderr,
                "Scripted: fwd=%d side=%d turn=%d fire=%d use=%d | ray0=%.0f engaging=%d | pos=%.0f,%.0f\n",
                cmd->forwardmove, cmd->sidemove, cmd->angleturn, fire, use,
                ray0, engaging,
                (float)player->mo->x / (float)FRACUNIT,
                (float)player->mo->y / (float)FRACUNIT);
        }
    }
}
