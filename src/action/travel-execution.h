﻿#pragma once

#include "system/angband.h"
#include "floor/floor-base-definitions.h"

 /*  A structure type for travel command  */
typedef struct travel_type {
    int run; /* Remaining grid number */
    int cost[MAX_HGT][MAX_WID];
    POSITION x; /* Target X */
    POSITION y; /* Target Y */
    DIRECTION dir; /* Running direction */
} travel_type;

extern travel_type travel;

void travel_step(player_type *creature_ptr);

typedef struct floor_type floor_type;
void forget_travel_flow(floor_type *floor_ptr);
