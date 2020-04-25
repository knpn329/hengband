﻿#pragma once

#define MAX_LINELEN 1024
#define MAX_AUTOPICK_DEFAULT 200
#define MAX_YANK MAX_LINELEN
#define MAX_LINES 3000

#define PT_DEFAULT 0
#define PT_WITH_PNAME 1

#define MARK_MARK     0x01
#define MARK_BY_SHIFT 0x02

#define LSTAT_BYPASS        0x01
#define LSTAT_EXPRESSION    0x02
#define LSTAT_AUTOREGISTER  0x04

#define QUIT_WITHOUT_SAVE 1
#define QUIT_AND_SAVE     2

#define DESCRIPT_HGT 3

/*!
 * @struct autopick_type
 * @brief 自動拾い/破壊設定データの構造体 / A structure type for entry of auto-picker/destroyer
 */
typedef struct {
	concptr name;          /*!< 自動拾い/破壊定義の名称一致基準 / Items which have 'name' as part of its name match */
	concptr insc;          /*!< 対象となったアイテムに自動で刻む内容 / Items will be auto-inscribed as 'insc' */
	BIT_FLAGS flag[2];       /*!< キーワードに関する汎用的な条件フラグ / Misc. keyword to be matched */
	byte action;        /*!< 対象のアイテムを拾う/破壊/放置するかの指定フラグ / Auto-pickup or Destroy or Leave items */
	byte dice;          /*!< 武器のダイス値基準値 / Weapons which have more than 'dice' dice match */
	byte bonus;         /*!< アイテムのボーナス基準値 / Items which have more than 'bonus' magical bonus match */
} autopick_type;

/*
 * Struct for yank buffer
 */
typedef struct chain_str {
	struct chain_str *next;
	char s[1];
} chain_str_type;

/*
 * Data struct for text editor
 */
typedef struct {
	int wid, hgt;
	int cx, cy;
	int upper, left;
	int old_wid, old_hgt;
	int old_cy;
	int old_upper, old_left;
	int mx, my;
	byte mark;

	object_type *search_o_ptr;
	concptr search_str;
	concptr last_destroyed;

	chain_str_type *yank;
	bool yank_eol;

	concptr *lines_list;
	byte states[MAX_LINES];

	u16b dirty_flags;
	int dirty_line;
	int filename_mode;
	int old_com_id;

	bool changed;
} text_body_type;

typedef struct {
	concptr name;
	int level;
	int key;
	int com_id;
} command_menu_type;

/*
 *  List for auto-picker/destroyer entries
 */
extern int max_autopick;
extern int max_max_autopick;
extern autopick_type *autopick_list;
extern object_type autopick_last_destroyed_object;

void autopick_free_entry(autopick_type *entry);
void free_text_lines(concptr *lines_list);
