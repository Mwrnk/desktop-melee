/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Window platforms (desktop_melee_platforms.h). Each is a joint of Melee's
 * stage collision with one line in the dynamic group, as moving platforms
 * are (Yoshi's Story's cloud, Rainbow Cruise): mpJointUpdateDynamics makes a
 * flat dynamic line a floor, and its 0x400 flag a drop-through one. A rider
 * is carried by the line's move since the last frame (mpGetSpeed: pos minus
 * prev_pos). */
#include "desktop_melee_platforms.h"

#include <string.h>

#include <melee/mp/mplib.h>
#include <melee/mp/types.h>
#include <sysdolphin/baselib/memory.h>

#include "desktop_melee_game.h"

#define PLATFORMS DESKTOP_MELEE_WINDOW_PLATFORMS
/* MapLine lo_flags: a dynamic floor is a drop-through platform. */
#define DYNAMIC_PLATFORM 0x400
/* A platform that moved farther than this in one frame jumped (a window
 * was raised or moved to another spot): riders aren't flung with it. */
#define JUMP_UNITS 40.0f

static MapCollData s_coll;
static int s_first_joint = -1; /* the platforms' joints, lines and vertices */
static int s_first_line = -1;
static bool s_shown[PLATFORMS];

MapCollData* desktop_melee_platforms_extend(MapCollData* coll_data, float scale)
{
    s_first_joint = -1;
    if (coll_data == NULL || !desktop_melee_overlay_enabled() || scale == 0.0f) {
        return coll_data;
    }
    const int verts = coll_data->vert_count;
    const int lines = coll_data->line_count;
    const int joints = coll_data->joint_count;
    /* Only stages without dynamic lines: their range must be ours alone. */
    if (coll_data->ranges[MapLineGroup_Dynamic].count != 0) {
        return coll_data;
    }

    DiscVec2* new_verts = HSD_MemAlloc(sizeof(DiscVec2) * (verts + 2 * PLATFORMS));
    MapLine* new_lines = HSD_MemAlloc(sizeof(MapLine) * (lines + PLATFORMS));
    MapJoint* new_joints = HSD_MemAlloc(sizeof(MapJoint) * (joints + PLATFORMS));
    if (new_verts == NULL || new_lines == NULL || new_joints == NULL) {
        return coll_data;
    }
    memcpy(new_verts, DP(DiscVec2, coll_data->verts), sizeof(DiscVec2) * verts);
    memcpy(new_lines, DP(MapLine, coll_data->lines), sizeof(MapLine) * lines);
    memcpy(new_joints, DP(MapJoint, coll_data->joints), sizeof(MapJoint) * joints);
    /* A floor from screen edge to screen edge: the stage's own ledges would
     * hang fighters past the side blastzones, so the ground has none. */
    if (!desktop_melee_floor_ledges()) {
        for (int i = 0; i < lines; i++) {
            new_lines[i].lo_flags &= (u16)~LINE_FLAG_LEDGE;
        }
    }

    for (int i = 0; i < PLATFORMS; i++) {
        /* Far below the screen until a window has a top for it. */
        const float x = (float)(i * 20) / scale;
        const float y = -2000.0f / scale;
        DiscVec2* v = &new_verts[verts + 2 * i];
        v[0].x = x;
        v[0].y = y;
        v[1].x = x + 10.0f / scale;
        v[1].y = y;

        MapLine* line = &new_lines[lines + i];
        memset(line, 0, sizeof(*line));
        line->v0_idx = (u16)(verts + 2 * i);
        line->v1_idx = (u16)(verts + 2 * i + 1);
        line->prev_id0 = -1;
        line->next_id0 = -1;
        line->prev_id1 = -1;
        line->next_id1 = -1;
        line->hi_flags = CollLine_Floor;
        line->lo_flags = DYNAMIC_PLATFORM;

        MapJoint* joint = &new_joints[joints + i];
        memset(joint, 0, sizeof(*joint));
        for (int g = 0; g < MapLineGroup_Count; g++) {
            joint->ranges[g].start = (s16)(lines + i);
            joint->ranges[g].count = 0;
        }
        joint->ranges[MapLineGroup_Dynamic].count = 1;
        joint->left_bound = x - 1.0f;
        joint->right_bound = x + 11.0f / scale;
        joint->bottom_bound = y - 1.0f;
        joint->top_bound = y + 1.0f;
        joint->vtx_start = (s16)(verts + 2 * i);
        joint->vtx_count = 2;
    }

    s_coll = *coll_data;
    DP_SET(s_coll.verts, new_verts);
    DP_SET(s_coll.lines, new_lines);
    DP_SET(s_coll.joints, new_joints);
    s_coll.vert_count = verts + 2 * PLATFORMS;
    s_coll.line_count = lines + PLATFORMS;
    s_coll.joint_count = joints + PLATFORMS;
    s_coll.ranges[MapLineGroup_Dynamic].start = (s16)lines;
    s_coll.ranges[MapLineGroup_Dynamic].count = PLATFORMS;

    s_first_joint = joints;
    s_first_line = lines;
    for (int i = 0; i < PLATFORMS; i++) {
        s_shown[i] = true; /* hidden on the first frame */
    }
    return &s_coll;
}

static void place(int i, float left, float right, float y)
{
    const int joint_id = s_first_joint + i;
    const int line_id = s_first_line + i;
    float old_x0, old_y0, old_x1, old_y1;
    mpLib_8004ED5C(line_id, &old_x0, &old_y0, &old_x1, &old_y1);

    mpLib_80057424(joint_id); /* the last frame's spot, for mpGetSpeed */
    mpLineSetPos(line_id, left, y, right, y);
    const float dx = left - old_x0;
    const float dy = y - old_y0;
    if (!s_shown[i] || dx * dx + dy * dy > JUMP_UNITS * JUMP_UNITS) {
        mpLib_80057424(joint_id); /* appeared or jumped: no carry */
    }
    mpLib_80055E24(joint_id);

    /* Bounding from scratch: mpJointUpdateBounding only grows it. */
    CollJoint* joint = &mpGetGroundCollJoint()[joint_id];
    joint->bounding_min.x = left;
    joint->bounding_max.x = right;
    joint->bounding_min.y = y;
    joint->bounding_max.y = y;
    mpJointUpdateBounding(joint_id);
}

void desktop_melee_platforms_frame(void)
{
    if (s_first_joint < 0) {
        return;
    }
    for (int i = 0; i < PLATFORMS; i++) {
        float left, right, y;
        bool ledges = false;
        const bool on =
            desktop_melee_window_platform(i, &left, &right, &y, &ledges) && right > left;
        if (on) {
            /* The window's top corners can be grabbed, like a stage's ledges. */
            MapLine* line = mpGetGroundCollLine()[s_first_line + i].inner;
            if (ledges) {
                line->lo_flags |= LINE_FLAG_LEDGE;
            } else {
                line->lo_flags &= (u16)~LINE_FLAG_LEDGE;
            }
            place(i, left, right, y);
            if (!s_shown[i]) {
                mpJointUnhide(s_first_joint + i);
                s_shown[i] = true;
            }
        } else if (s_shown[i]) {
            mpJointHide(s_first_joint + i);
            s_shown[i] = false;
        }
    }
}
