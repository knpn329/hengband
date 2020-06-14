﻿/*
 * @author
 * 2002/01/12 mogami
 * 2020/05/16 Hourier
 * @details
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * This software may be copied and distributed for educational, research,
 * and not for profit purposes provided that this copyright and statement
 * are included in all such copies.  Other copyrights may also apply.
 */

#include "system/angband.h"
#include "util/util.h"
#include "cmd-io/cmd-dump.h"
#include "cmd-io/cmd-menu-content-table.h"
#include "cmd-io/macro-util.h"
#include "core/asking-player.h"
#include "core/output-updater.h"
#include "core/stuff-handler.h"
#include "dungeon/quest.h"
#include "floor/floor.h"
#include "game-option/cheat-options.h"
#include "game-option/disturbance-options.h"
#include "game-option/input-options.h"
#include "game-option/map-screen-options.h"
#include "game-option/option-flags.h"
#include "game-option/special-options.h"
#include "io/files-util.h"
#include "io/input-key-acceptor.h"
#include "io/input-key-processor.h"
#include "io/input-key-requester.h"
#include "io/signal-handlers.h"
#include "io/write-diary.h"
#include "locale/japanese.h"
#include "main/music-definitions-table.h"
#include "main/sound-of-music.h"
#include "monster-race/monster-race-hook.h"
#include "player/player-class.h"
#include "system/system-variables.h"
#include "term/gameterm.h"
#include "term/screen-processor.h"
#include "term/term-color-types.h"
#include "util/quarks.h"
#include "util/string-processor.h"
#include "view/display-main-window.h"
#include "view/display-messages.h"
#include "world/world.h"

void roff_to_buf(concptr str, int maxlen, char *tbuf, size_t bufsize)
{
    int read_pt = 0;
    int write_pt = 0;
    int line_len = 0;
    int word_punct = 0;
    char ch[3];
    ch[2] = '\0';

    while (str[read_pt]) {
#ifdef JP
        bool kinsoku = FALSE;
        bool kanji;
#endif
        int ch_len = 1;
        ch[0] = str[read_pt];
        ch[1] = '\0';
#ifdef JP
        kanji = iskanji(ch[0]);

        if (kanji) {
            ch[1] = str[read_pt + 1];
            ch_len = 2;

            if (strcmp(ch, "。") == 0 || strcmp(ch, "、") == 0 || strcmp(ch, "ィ") == 0 || strcmp(ch, "ー") == 0)
                kinsoku = TRUE;
        } else if (!isprint(ch[0]))
            ch[0] = ' ';
#else
        if (!isprint(ch[0]))
            ch[0] = ' ';
#endif

        if (line_len + ch_len > maxlen - 1 || str[read_pt] == '\n') {
            int word_len = read_pt - word_punct;
#ifdef JP
            if (kanji && !kinsoku)
                /* nothing */;
            else
#endif
                if (ch[0] == ' ' || word_len >= line_len / 2)
                read_pt++;
            else {
                read_pt = word_punct;
                if (str[word_punct] == ' ')
                    read_pt++;
                write_pt -= word_len;
            }

            tbuf[write_pt++] = '\0';
            line_len = 0;
            word_punct = read_pt;
            continue;
        }

        if (ch[0] == ' ')
            word_punct = read_pt;

#ifdef JP
        if (!kinsoku)
            word_punct = read_pt;
#endif

        if ((size_t)(write_pt + 3) >= bufsize)
            break;

        tbuf[write_pt++] = ch[0];
        line_len++;
        read_pt++;
#ifdef JP
        if (kanji) {
            tbuf[write_pt++] = ch[1];
            line_len++;
            read_pt++;
        }
#endif
    }

    tbuf[write_pt] = '\0';
    tbuf[write_pt + 1] = '\0';
    return;
}

/*
 * Convert string to lower case
 */
void str_tolower(char *str)
{
    for (; *str; str++) {
#ifdef JP
        if (iskanji(*str)) {
            str++;
            continue;
        }
#endif
        *str = (char)tolower(*str);
    }
}
