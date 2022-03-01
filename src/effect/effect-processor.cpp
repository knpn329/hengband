﻿#include "effect/effect-processor.h"
#include "core/stuff-handler.h"
#include "effect/attribute-types.h"
#include "effect/effect-characteristics.h"
#include "effect/effect-feature.h"
#include "effect/effect-item.h"
#include "effect/effect-monster.h"
#include "effect/effect-player.h"
#include "effect/spells-effect-util.h"
#include "floor/cave.h"
#include "floor/geometry.h"
#include "floor/line-of-sight.h"
#include "game-option/map-screen-options.h"
#include "game-option/special-options.h"
#include "grid/feature-flag-types.h"
#include "grid/grid.h"
#include "io/cursor.h"
#include "io/screen-util.h"
#include "main/sound-definitions-table.h"
#include "main/sound-of-music.h"
#include "monster-race/monster-race.h"
#include "monster-race/race-flags2.h"
#include "monster-race/race-indice-types.h"
#include "monster/monster-describer.h"
#include "monster/monster-description-types.h"
#include "monster/monster-info.h"
#include "pet/pet-fall-off.h"
#include "player/player-status.h"
#include "spell/range-calc.h"
#include "system/floor-type-definition.h"
#include "system/grid-type-definition.h"
#include "system/monster-race-definition.h"
#include "system/monster-type-definition.h"
#include "system/player-type-definition.h"
#include "target/projection-path-calculator.h"
#include "term/gameterm.h"
#include "timed-effect/player-hallucination.h"
#include "timed-effect/timed-effects.h"
#include "util/bit-flags-calculator.h"
#include "view/display-messages.h"

/*!
 * @brief 配置した鏡リストの次を取得する /
 * Get another mirror. for SEEKER
 * @param next_y 次の鏡のy座標を返す参照ポインタ
 * @param next_x 次の鏡のx座標を返す参照ポインタ
 * @param cury 現在の鏡のy座標
 * @param curx 現在の鏡のx座標
 */
static void next_mirror(PlayerType *player_ptr, POSITION *next_y, POSITION *next_x, POSITION cury, POSITION curx)
{
    POSITION mirror_x[10], mirror_y[10]; /* 鏡はもっと少ない */
    int mirror_num = 0; /* 鏡の数 */
    for (POSITION x = 0; x < player_ptr->current_floor_ptr->width; x++) {
        for (POSITION y = 0; y < player_ptr->current_floor_ptr->height; y++) {
            if (player_ptr->current_floor_ptr->grid_array[y][x].is_mirror()) {
                mirror_y[mirror_num] = y;
                mirror_x[mirror_num] = x;
                mirror_num++;
            }
        }
    }

    if (mirror_num) {
        int num = randint0(mirror_num);
        *next_y = mirror_y[num];
        *next_x = mirror_x[num];
        return;
    }

    *next_y = cury + randint0(5) - 2;
    *next_x = curx + randint0(5) - 2;
}

/*!
 * @brief 汎用的なビーム/ボルト/ボール系処理のルーチン Generic
 * "beam"/"bolt"/"ball" projection routine.
 * @param who 魔法を発動したモンスター(0ならばプレイヤー) / Index of "source"
 * monster (zero for "player")
 * @param rad 効果半径(ビーム/ボルト = 0 / ボール = 1以上) / Radius of explosion
 * (0 = beam/bolt, 1 to 9 = ball)
 * @param target_y 目標Y座標 / Target y location (or location to travel "towards")
 * @param target_x 目標X座標 / Target x location (or location to travel "towards")
 * @param dam 基本威力 / Base damage roll to apply to affected monsters (or
 * player)
 * @param typ 効果属性 / Type of damage to apply to monsters (and objects)
 * @param flag 効果フラグ / Extra bit flags (see PROJECT_xxxx)
 * @param monspell 効果元のモンスター魔法ID
 * @todo 似たような処理が山ほど並んでいる、何とかならないものか
 * @todo 引数にそのまま再代入していてカオスすぎる。直すのは簡単ではない
 */
ProjectResult project(PlayerType *player_ptr, const MONSTER_IDX who, POSITION rad, const POSITION target_y, const POSITION target_x, const int dam,
    const AttributeType typ, BIT_FLAGS flag, std::optional<CapturedMonsterType *> cap_mon_ptr)
{
    int dist;
    POSITION y1;
    POSITION x1;
    POSITION y2;
    POSITION x2;
    POSITION y_saver;
    POSITION x_saver;
    bool visual = false;
    bool drawn = false;
    bool breath = false;
    bool blind = player_ptr->blind != 0;
    bool old_hide = false;
    int path_n = 0;
    uint16_t path_g[512];
    int grids = 0;
    POSITION gx[1024];
    POSITION gy[1024];
    POSITION gm[32];
    POSITION gm_rad = rad;
    bool jump = false;
    GAME_TEXT who_name[MAX_NLEN];
    bool see_s_msg = true;
    who_name[0] = '\0';
    rakubadam_p = 0;
    rakubadam_m = 0;
    monster_target_y = player_ptr->y;
    monster_target_x = player_ptr->x;

    ProjectResult res;

    if (flag & (PROJECT_JUMP)) {
        x1 = target_x;
        y1 = target_y;
        flag &= ~(PROJECT_JUMP);
        jump = true;
    } else if (who <= 0) {
        x1 = player_ptr->x;
        y1 = player_ptr->y;
    } else if (who > 0) {
        x1 = player_ptr->current_floor_ptr->m_list[who].fx;
        y1 = player_ptr->current_floor_ptr->m_list[who].fy;
        monster_desc(player_ptr, who_name, &player_ptr->current_floor_ptr->m_list[who], MD_WRONGDOER_NAME);
    } else {
        x1 = target_x;
        y1 = target_y;
    }

    y_saver = y1;
    x_saver = x1;
    y2 = target_y;
    x2 = target_x;

    if (flag & (PROJECT_THRU)) {
        if ((x1 == x2) && (y1 == y2)) {
            flag &= ~(PROJECT_THRU);
        }
    }

    if (flag & (PROJECT_BREATH)) {
        breath = true;
        if (flag & PROJECT_HIDE) {
            old_hide = true;
        }
        flag |= PROJECT_HIDE;
    }

    for (dist = 0; dist < 32; dist++) {
        gm[dist] = 0;
    }

    dist = 0;
    if (flag & (PROJECT_BEAM)) {
        gy[grids] = y1;
        gx[grids] = x1;
        grids++;
    }

    switch (typ) {
    case AttributeType::LITE:
    case AttributeType::LITE_WEAK:
        if (breath || (flag & PROJECT_BEAM)) {
            flag |= (PROJECT_LOS);
        }
        break;
    case AttributeType::DISINTEGRATE:
        flag |= (PROJECT_GRID);
        if (breath || (flag & PROJECT_BEAM)) {
            flag |= (PROJECT_DISI);
        }
        break;
    default:
        break;
    }

    /* Calculate the projection path */
    path_n = projection_path(player_ptr, path_g, (project_length ? project_length : get_max_range(player_ptr)), y1, x1, y2, x2, flag);
    handle_stuff(player_ptr);

    if (typ == AttributeType::SEEKER) {
        int j;
        int last_i = 0;
        project_m_n = 0;
        project_m_x = 0;
        project_m_y = 0;
        auto oy = y1;
        auto ox = x1;
        for (int i = 0; i < path_n; ++i) {
            POSITION ny = get_grid_y(path_g[i]);
            POSITION nx = get_grid_x(path_g[i]);
            gy[grids] = ny;
            gx[grids] = nx;
            grids++;

            if (delay_factor > 0) {
                if (!blind && !(flag & (PROJECT_HIDE))) {
                    if (panel_contains(ny, nx) && player_has_los_bold(player_ptr, ny, nx)) {
                        uint16_t p = bolt_pict(oy, ox, ny, nx, typ);
                        TERM_COLOR a = PICT_A(p);
                        auto c = PICT_C(p);
                        print_rel(player_ptr, c, a, ny, nx);
                        move_cursor_relative(ny, nx);
                        term_fresh();
                        term_xtra(TERM_XTRA_DELAY, delay_factor);
                        lite_spot(player_ptr, ny, nx);
                        term_fresh();
                        if (flag & (PROJECT_BEAM)) {
                            p = bolt_pict(ny, nx, ny, nx, typ);
                            a = PICT_A(p);
                            c = PICT_C(p);
                            print_rel(player_ptr, c, a, ny, nx);
                        }

                        visual = true;
                    } else if (visual) {
                        term_xtra(TERM_XTRA_DELAY, delay_factor);
                    }
                }
            }

            if (affect_item(player_ptr, 0, 0, ny, nx, dam, AttributeType::SEEKER)) {
                res.notice = true;
            }

            if (!player_ptr->current_floor_ptr->grid_array[ny][nx].is_mirror()) {
                oy = ny;
                ox = nx;
                continue;
            }

            monster_target_y = ny;
            monster_target_x = nx;
            remove_mirror(player_ptr, ny, nx);
            next_mirror(player_ptr, &oy, &ox, ny, nx);
            path_n = i + projection_path(player_ptr, &(path_g[i + 1]), (project_length ? project_length : get_max_range(player_ptr)), ny, nx, oy, ox, flag);
            auto y = ny;
            auto x = nx;
            for (j = last_i; j <= i; j++) {
                y = get_grid_y(path_g[j]);
                x = get_grid_x(path_g[j]);
                if (affect_monster(player_ptr, 0, 0, y, x, dam, AttributeType::SEEKER, flag, true, cap_mon_ptr)) {
                    res.notice = true;
                }

                if (!who && (project_m_n == 1) && !jump && (player_ptr->current_floor_ptr->grid_array[project_m_y][project_m_x].m_idx > 0)) {
                    auto *m_ptr = &player_ptr->current_floor_ptr->m_list[player_ptr->current_floor_ptr->grid_array[project_m_y][project_m_x].m_idx];
                    if (m_ptr->ml) {
                        if (!player_ptr->effects()->hallucination()->is_hallucinated()) {
                            monster_race_track(player_ptr, m_ptr->ap_r_idx);
                        }

                        health_track(player_ptr, player_ptr->current_floor_ptr->grid_array[project_m_y][project_m_x].m_idx);
                    }
                }

                (void)affect_feature(player_ptr, 0, 0, y, x, dam, AttributeType::SEEKER);
            }

            last_i = i;
            oy = y;
            ox = x;
        }

        for (int i = last_i; i < path_n; i++) {
            POSITION py, px;
            py = get_grid_y(path_g[i]);
            px = get_grid_x(path_g[i]);
            if (affect_monster(player_ptr, 0, 0, py, px, dam, AttributeType::SEEKER, flag, true, cap_mon_ptr)) {
                res.notice = true;
            }
            if (!who && (project_m_n == 1) && !jump) {
                if (player_ptr->current_floor_ptr->grid_array[project_m_y][project_m_x].m_idx > 0) {
                    auto *m_ptr = &player_ptr->current_floor_ptr->m_list[player_ptr->current_floor_ptr->grid_array[project_m_y][project_m_x].m_idx];
                    if (m_ptr->ml) {
                        if (!player_ptr->effects()->hallucination()->is_hallucinated()) {
                            monster_race_track(player_ptr, m_ptr->ap_r_idx);
                        }
                        health_track(player_ptr, player_ptr->current_floor_ptr->grid_array[project_m_y][project_m_x].m_idx);
                    }
                }
            }

            (void)affect_feature(player_ptr, 0, 0, py, px, dam, AttributeType::SEEKER);
        }

        return res;
    } else if (typ == AttributeType::SUPER_RAY) {
        int j;
        int second_step = 0;
        project_m_n = 0;
        project_m_x = 0;
        project_m_y = 0;
        auto oy = y1;
        auto ox = x1;
        for (int i = 0; i < path_n; ++i) {
            POSITION ny = get_grid_y(path_g[i]);
            POSITION nx = get_grid_x(path_g[i]);
            gy[grids] = ny;
            gx[grids] = nx;
            grids++;
            {
                if (delay_factor > 0) {
                    if (panel_contains(ny, nx) && player_has_los_bold(player_ptr, ny, nx)) {
                        uint16_t p;
                        TERM_COLOR a;
                        p = bolt_pict(oy, ox, ny, nx, typ);
                        a = PICT_A(p);
                        auto c = PICT_C(p);
                        print_rel(player_ptr, c, a, ny, nx);
                        move_cursor_relative(ny, nx);
                        term_fresh();
                        term_xtra(TERM_XTRA_DELAY, delay_factor);
                        lite_spot(player_ptr, ny, nx);
                        term_fresh();
                        if (flag & (PROJECT_BEAM)) {
                            p = bolt_pict(ny, nx, ny, nx, typ);
                            a = PICT_A(p);
                            c = PICT_C(p);
                            print_rel(player_ptr, c, a, ny, nx);
                        }

                        visual = true;
                    } else if (visual) {
                        term_xtra(TERM_XTRA_DELAY, delay_factor);
                    }
                }
            }

            if (affect_item(player_ptr, 0, 0, ny, nx, dam, AttributeType::SUPER_RAY)) {
                res.notice = true;
            }
            if (!cave_has_flag_bold(player_ptr->current_floor_ptr, ny, nx, FloorFeatureType::PROJECT)) {
                if (second_step) {
                    continue;
                }
                break;
            }

            if (player_ptr->current_floor_ptr->grid_array[ny][nx].is_mirror() && !second_step) {
                monster_target_y = ny;
                monster_target_x = nx;
                remove_mirror(player_ptr, ny, nx);
                auto y = ny;
                auto x = nx;
                for (j = 0; j <= i; j++) {
                    y = get_grid_y(path_g[j]);
                    x = get_grid_x(path_g[j]);
                    (void)affect_feature(player_ptr, 0, 0, y, x, dam, AttributeType::SUPER_RAY);
                }

                path_n = i;
                second_step = i + 1;
                path_n += projection_path(
                    player_ptr, &(path_g[path_n + 1]), (project_length ? project_length : get_max_range(player_ptr)), y, x, y - 1, x - 1, flag);
                path_n += projection_path(player_ptr, &(path_g[path_n + 1]), (project_length ? project_length : get_max_range(player_ptr)), y, x, y - 1, x, flag);
                path_n += projection_path(
                    player_ptr, &(path_g[path_n + 1]), (project_length ? project_length : get_max_range(player_ptr)), y, x, y - 1, x + 1, flag);
                path_n += projection_path(player_ptr, &(path_g[path_n + 1]), (project_length ? project_length : get_max_range(player_ptr)), y, x, y, x - 1, flag);
                path_n += projection_path(player_ptr, &(path_g[path_n + 1]), (project_length ? project_length : get_max_range(player_ptr)), y, x, y, x + 1, flag);
                path_n += projection_path(
                    player_ptr, &(path_g[path_n + 1]), (project_length ? project_length : get_max_range(player_ptr)), y, x, y + 1, x - 1, flag);
                path_n += projection_path(player_ptr, &(path_g[path_n + 1]), (project_length ? project_length : get_max_range(player_ptr)), y, x, y + 1, x, flag);
                path_n += projection_path(
                    player_ptr, &(path_g[path_n + 1]), (project_length ? project_length : get_max_range(player_ptr)), y, x, y + 1, x + 1, flag);

                oy = y;
                ox = x;
                continue;
            }
            oy = ny;
            ox = nx;
        }

        for (int i = 0; i < path_n; i++) {
            POSITION py = get_grid_y(path_g[i]);
            POSITION px = get_grid_x(path_g[i]);
            (void)affect_monster(player_ptr, 0, 0, py, px, dam, AttributeType::SUPER_RAY, flag, true, cap_mon_ptr);
            if (!who && (project_m_n == 1) && !jump) {
                if (player_ptr->current_floor_ptr->grid_array[project_m_y][project_m_x].m_idx > 0) {
                    auto *m_ptr = &player_ptr->current_floor_ptr->m_list[player_ptr->current_floor_ptr->grid_array[project_m_y][project_m_x].m_idx];
                    if (m_ptr->ml) {
                        if (!player_ptr->effects()->hallucination()->is_hallucinated()) {
                            monster_race_track(player_ptr, m_ptr->ap_r_idx);
                        }
                        health_track(player_ptr, player_ptr->current_floor_ptr->grid_array[project_m_y][project_m_x].m_idx);
                    }
                }
            }

            (void)affect_feature(player_ptr, 0, 0, py, px, dam, AttributeType::SUPER_RAY);
        }

        return res;
    }

    int k;
    auto oy = y1;
    auto ox = x1;
    for (k = 0; k < path_n; ++k) {
        POSITION ny = get_grid_y(path_g[k]);
        POSITION nx = get_grid_x(path_g[k]);
        if (flag & PROJECT_DISI) {
            if (cave_stop_disintegration(player_ptr->current_floor_ptr, ny, nx) && (rad > 0)) {
                break;
            }
        } else if (flag & PROJECT_LOS) {
            if (!cave_los_bold(player_ptr->current_floor_ptr, ny, nx) && (rad > 0)) {
                break;
            }
        } else {
            if (!cave_has_flag_bold(player_ptr->current_floor_ptr, ny, nx, FloorFeatureType::PROJECT) && (rad > 0)) {
                break;
            }
        }
        if (flag & (PROJECT_BEAM)) {
            gy[grids] = ny;
            gx[grids] = nx;
            grids++;
        }

        if (delay_factor > 0) {
            if (!blind && !(flag & (PROJECT_HIDE | PROJECT_FAST))) {
                if (panel_contains(ny, nx) && player_has_los_bold(player_ptr, ny, nx)) {
                    uint16_t p;
                    TERM_COLOR a;
                    p = bolt_pict(oy, ox, ny, nx, typ);
                    a = PICT_A(p);
                    auto c = PICT_C(p);
                    print_rel(player_ptr, c, a, ny, nx);
                    move_cursor_relative(ny, nx);
                    term_fresh();
                    term_xtra(TERM_XTRA_DELAY, delay_factor);
                    lite_spot(player_ptr, ny, nx);
                    term_fresh();
                    if (flag & (PROJECT_BEAM)) {
                        p = bolt_pict(ny, nx, ny, nx, typ);
                        a = PICT_A(p);
                        c = PICT_C(p);
                        print_rel(player_ptr, c, a, ny, nx);
                    }

                    visual = true;
                } else if (visual) {
                    term_xtra(TERM_XTRA_DELAY, delay_factor);
                }
            }
        }

        oy = ny;
        ox = nx;
    }

    path_n = k;
    POSITION by = oy;
    POSITION bx = ox;
    if (breath && !path_n) {
        breath = false;
        gm_rad = rad;
        if (!old_hide) {
            flag &= ~(PROJECT_HIDE);
        }
    }

    gm[0] = 0;
    gm[1] = grids;
    dist = path_n;
    int dist_hack = dist;
    project_length = 0;

    /* If we found a "target", explode there */
    if (dist <= get_max_range(player_ptr)) {
        if ((flag & (PROJECT_BEAM)) && (grids > 0)) {
            grids--;
        }

        /*
         * Create a conical breath attack
         *
         *       ***
         *   ********
         * D********@**
         *   ********
         *       ***
         */
        if (breath) {
            flag &= ~(PROJECT_HIDE);
            breath_shape(player_ptr, path_g, dist, &grids, gx, gy, gm, &gm_rad, rad, y1, x1, by, bx, typ);
        } else {
            for (dist = 0; dist <= rad; dist++) {
                for (auto y = by - dist; y <= by + dist; y++) {
                    for (auto x = bx - dist; x <= bx + dist; x++) {
                        if (!in_bounds2(player_ptr->current_floor_ptr, y, x)) {
                            continue;
                        }
                        if (distance(by, bx, y, x) != dist) {
                            continue;
                        }

                        switch (typ) {
                        case AttributeType::LITE:
                        case AttributeType::LITE_WEAK:
                            if (!los(player_ptr, by, bx, y, x)) {
                                continue;
                            }
                            break;
                        case AttributeType::DISINTEGRATE:
                            if (!in_disintegration_range(player_ptr->current_floor_ptr, by, bx, y, x)) {
                                continue;
                            }
                            break;
                        default:
                            if (!projectable(player_ptr, by, bx, y, x)) {
                                continue;
                            }
                            break;
                        }

                        gy[grids] = y;
                        gx[grids] = x;
                        grids++;
                    }
                }

                gm[dist + 1] = grids;
            }
        }
    }

    if (!grids) {
        return res;
    }

    if (!blind && !(flag & (PROJECT_HIDE)) && (delay_factor > 0)) {
        for (int t = 0; t <= gm_rad; t++) {
            for (int i = gm[t]; i < gm[t + 1]; i++) {
                auto y = gy[i];
                auto x = gx[i];
                if (panel_contains(y, x) && player_has_los_bold(player_ptr, y, x)) {
                    uint16_t p;
                    TERM_COLOR a;
                    drawn = true;
                    p = bolt_pict(y, x, y, x, typ);
                    a = PICT_A(p);
                    auto c = PICT_C(p);
                    print_rel(player_ptr, c, a, y, x);
                }
            }

            move_cursor_relative(by, bx);
            term_fresh();
            if (visual || drawn) {
                term_xtra(TERM_XTRA_DELAY, delay_factor);
            }
        }

        if (drawn) {
            for (int i = 0; i < grids; i++) {
                auto y = gy[i];
                auto x = gx[i];
                if (panel_contains(y, x) && player_has_los_bold(player_ptr, y, x)) {
                    lite_spot(player_ptr, y, x);
                }
            }

            move_cursor_relative(by, bx);
            term_fresh();
        }
    }

    update_creature(player_ptr);

    if (flag & PROJECT_KILL) {
        see_s_msg = (who > 0) ? is_seen(player_ptr, &player_ptr->current_floor_ptr->m_list[who])
                              : (!who ? true : (player_can_see_bold(player_ptr, y1, x1) && projectable(player_ptr, player_ptr->y, player_ptr->x, y1, x1)));
    }

    if (flag & (PROJECT_GRID)) {
        dist = 0;
        for (int i = 0; i < grids; i++) {
            if (gm[dist + 1] == i) {
                dist++;
            }
            auto y = gy[i];
            auto x = gx[i];
            if (breath) {
                int d = dist_to_line(y, x, y1, x1, by, bx);
                if (affect_feature(player_ptr, who, d, y, x, dam, typ)) {
                    res.notice = true;
                }
            } else {
                if (affect_feature(player_ptr, who, dist, y, x, dam, typ)) {
                    res.notice = true;
                }
            }
        }
    }

    update_creature(player_ptr);
    if (flag & (PROJECT_ITEM)) {
        dist = 0;
        for (int i = 0; i < grids; i++) {
            if (gm[dist + 1] == i) {
                dist++;
            }

            auto y = gy[i];
            auto x = gx[i];
            if (breath) {
                int d = dist_to_line(y, x, y1, x1, by, bx);
                if (affect_item(player_ptr, who, d, y, x, dam, typ)) {
                    res.notice = true;
                }
            } else {
                if (affect_item(player_ptr, who, dist, y, x, dam, typ)) {
                    res.notice = true;
                }
            }
        }
    }

    if (flag & (PROJECT_KILL)) {
        project_m_n = 0;
        project_m_x = 0;
        project_m_y = 0;
        dist = 0;
        for (int i = 0; i < grids; i++) {
            int effective_dist;
            if (gm[dist + 1] == i) {
                dist++;
            }

            auto y = gy[i];
            auto x = gx[i];
            if (grids <= 1) {
                auto *m_ptr = &player_ptr->current_floor_ptr->m_list[player_ptr->current_floor_ptr->grid_array[y][x].m_idx];
                monster_race *ref_ptr = &r_info[m_ptr->r_idx];
                if ((flag & PROJECT_REFLECTABLE) && player_ptr->current_floor_ptr->grid_array[y][x].m_idx && (ref_ptr->flags2 & RF2_REFLECTING) && ((player_ptr->current_floor_ptr->grid_array[y][x].m_idx != player_ptr->riding) || !(flag & PROJECT_PLAYER)) && (!who || dist_hack > 1) && !one_in_(10)) {
                    POSITION t_y, t_x;
                    int max_attempts = 10;
                    do {
                        t_y = y_saver - 1 + randint1(3);
                        t_x = x_saver - 1 + randint1(3);
                        max_attempts--;
                    } while (max_attempts && in_bounds2u(player_ptr->current_floor_ptr, t_y, t_x) && !projectable(player_ptr, y, x, t_y, t_x));

                    if (max_attempts < 1) {
                        t_y = y_saver;
                        t_x = x_saver;
                    }

                    if (is_seen(player_ptr, m_ptr)) {
                        sound(SOUND_REFLECT);
                        if ((m_ptr->r_idx == MON_KENSHIROU) || (m_ptr->r_idx == MON_RAOU)) {
                            msg_print(_("「北斗神拳奥義・二指真空把！」", "The attack bounces!"));
                        } else if (m_ptr->r_idx == MON_DIO) {
                            msg_print(_("ディオ・ブランドーは指一本で攻撃を弾き返した！", "The attack bounces!"));
                        } else {
                            msg_print(_("攻撃は跳ね返った！", "The attack bounces!"));
                        }
                    } else if (who <= 0) {
                        sound(SOUND_REFLECT);
                    }

                    if (is_original_ap_and_seen(player_ptr, m_ptr)) {
                        ref_ptr->r_flags2 |= RF2_REFLECTING;
                    }

                    if (player_bold(player_ptr, y, x) || one_in_(2)) {
                        flag &= ~(PROJECT_PLAYER);
                    } else {
                        flag |= PROJECT_PLAYER;
                    }

                    project(player_ptr, player_ptr->current_floor_ptr->grid_array[y][x].m_idx, 0, t_y, t_x, dam, typ, flag);
                    continue;
                }
            }

            /* Find the closest point in the blast */
            if (breath) {
                effective_dist = dist_to_line(y, x, y1, x1, by, bx);
            } else {
                effective_dist = dist;
            }

            if (player_ptr->riding && player_bold(player_ptr, y, x)) {
                if (flag & PROJECT_PLAYER) {
                    if (flag & (PROJECT_BEAM | PROJECT_REFLECTABLE | PROJECT_AIMED)) {
                        /*
                         * A beam or bolt is well aimed
                         * at the PLAYER!
                         * So don't affects the mount.
                         */
                        continue;
                    } else {
                        /*
                         * The spell is not well aimed,
                         * So partly affect the mount too.
                         */
                        effective_dist++;
                    }
                }

                /*
                 * This grid is the original target.
                 * Or aimed on your horse.
                 */
                else if (((y == y2) && (x == x2)) || (flag & PROJECT_AIMED)) {
                    /* Hit the mount with full damage */
                }

                /*
                 * Otherwise this grid is not the
                 * original target, it means that line
                 * of fire is obstructed by this
                 * monster.
                 */
                /*
                 * A beam or bolt will hit either
                 * player or mount.  Choose randomly.
                 */
                else if (flag & (PROJECT_BEAM | PROJECT_REFLECTABLE)) {
                    if (one_in_(2)) {
                        /* Hit the mount with full damage */
                    } else {
                        flag |= PROJECT_PLAYER;
                        continue;
                    }
                }

                /*
                 * The spell is not well aimed, so
                 * partly affect both player and
                 * mount.
                 */
                else {
                    effective_dist++;
                }
            }

            if (affect_monster(player_ptr, who, effective_dist, y, x, dam, typ, flag, see_s_msg, cap_mon_ptr)) {
                res.notice = true;
            }
        }

        /* Player affected one monster (without "jumping") */
        if (!who && (project_m_n == 1) && !jump) {
            auto x = project_m_x;
            auto y = project_m_y;
            if (player_ptr->current_floor_ptr->grid_array[y][x].m_idx > 0) {
                auto *m_ptr = &player_ptr->current_floor_ptr->m_list[player_ptr->current_floor_ptr->grid_array[y][x].m_idx];
                if (m_ptr->ml) {
                    if (!player_ptr->effects()->hallucination()->is_hallucinated()) {
                        monster_race_track(player_ptr, m_ptr->ap_r_idx);
                    }

                    health_track(player_ptr, player_ptr->current_floor_ptr->grid_array[y][x].m_idx);
                }
            }
        }
    }

    if (flag & (PROJECT_KILL)) {
        dist = 0;
        for (int i = 0; i < grids; i++) {
            int effective_dist;
            if (gm[dist + 1] == i) {
                dist++;
            }

            auto y = gy[i];
            auto x = gx[i];
            if (!player_bold(player_ptr, y, x)) {
                continue;
            }

            /* Find the closest point in the blast */
            if (breath) {
                effective_dist = dist_to_line(y, x, y1, x1, by, bx);
            } else {
                effective_dist = dist;
            }

            if (player_ptr->riding) {
                if (flag & PROJECT_PLAYER) {
                    /* Hit the player with full damage */
                }

                /*
                 * Hack -- When this grid was not the
                 * original target, a beam or bolt
                 * would hit either player or mount,
                 * and should be choosen randomly.
                 *
                 * But already choosen to hit the
                 * mount at this point.
                 *
                 * Or aimed on your horse.
                 */
                else if (flag & (PROJECT_BEAM | PROJECT_REFLECTABLE | PROJECT_AIMED)) {
                    /*
                     * A beam or bolt is well aimed
                     * at the mount!
                     * So don't affects the player.
                     */
                    continue;
                } else {
                    /*
                     * The spell is not well aimed,
                     * So partly affect the player too.
                     */
                    effective_dist++;
                }
            }

            if (affect_player(who, player_ptr, who_name, effective_dist, y, x, dam, typ, flag, project)) {
                res.notice = true;
                res.affected_player = true;
            }
        }
    }

    if (player_ptr->riding) {
        GAME_TEXT m_name[MAX_NLEN];
        monster_desc(player_ptr, m_name, &player_ptr->current_floor_ptr->m_list[player_ptr->riding], 0);
        if (rakubadam_m > 0) {
            if (process_fall_off_horse(player_ptr, rakubadam_m, false)) {
                msg_format(_("%^sに振り落とされた！", "%^s has thrown you off!"), m_name);
            }
        }

        if (player_ptr->riding && rakubadam_p > 0) {
            if (process_fall_off_horse(player_ptr, rakubadam_p, false)) {
                msg_format(_("%^sから落ちてしまった！", "You have fallen from %s."), m_name);
            }
        }
    }

    return res;
}
