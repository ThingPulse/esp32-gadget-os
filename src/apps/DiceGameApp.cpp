#include "ui/I18n.h"
#include "apps/DiceGameApp.h"

#include "AppRegistry.h"
#include "ui/UiNav.h"
#include "ui/UiSessionState.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

APP_REGISTER_EXPERIMENTAL_COLOR_RANK("Dice Score", LV_SYMBOL_LOOP,
                                     dice_game_app_launch, 0x845ef7, 30);

namespace {

constexpr int MAX_PLAYERS = 4;
constexpr int DICE_COUNT = 5;
constexpr int CATEGORY_COUNT = 11;

const char *const CATEGORY_NAMES[CATEGORY_COUNT] = {
    "Ones", "Twos", "Threes", "Fours", "Fives", "Sixes",
    "3 kind", "4 kind", "Full", "Straight", "Five"};

const uint32_t PLAYER_COLORS[MAX_PLAYERS] = {
    0x22b8cf, 0x82c91e, 0xff6b6b, 0xf59f00};

struct DiceGameCtx {
  lv_obj_t *screen = nullptr;
  lv_obj_t *body = nullptr;
  lv_obj_t *turn_label = nullptr;
  lv_obj_t *roll_button = nullptr;
  lv_obj_t *roll_label = nullptr;
  lv_obj_t *dice_buttons[DICE_COUNT]{};
  lv_obj_t *dice_labels[DICE_COUNT]{};
  lv_obj_t *score_cells[MAX_PLAYERS][CATEGORY_COUNT]{};
  lv_obj_t *score_labels[MAX_PLAYERS][CATEGORY_COUNT]{};
  lv_obj_t *total_labels[MAX_PLAYERS]{};
  lv_obj_t *score_confirmation = nullptr;
  lv_obj_t *pending_score_cell = nullptr;
  int8_t scores[MAX_PLAYERS][CATEGORY_COUNT]{};
  uint8_t dice[DICE_COUNT]{};
  bool held[DICE_COUNT]{};
  uint8_t player_count = 1;
  uint8_t active_player = 0;
  uint8_t rolls = 0;
  uint8_t animation_step = 0;
  bool rolling = false;
  bool pending_score_cancelled = false;
  int8_t pending_score_player = -1;
  int8_t pending_score_category = -1;
  lv_timer_t *roll_timer = nullptr;
  uint32_t random_state = 0x92d68ca2;
};

static DiceGameCtx game;
static void show_player_selection();

static lv_color_t player_color(int player)
{
  return lv_color_hex(PLAYER_COLORS[player]);
}

static uint32_t next_random()
{
  uint32_t x = game.random_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  game.random_state = x;
  return x;
}

static void stop_roll_animation()
{
  if (game.roll_timer) {
    lv_timer_delete(game.roll_timer);
    game.roll_timer = nullptr;
  }
  game.rolling = false;
}

static void cancel_score_confirmation()
{
  if (game.score_confirmation && lv_obj_is_valid(game.score_confirmation))
    lv_obj_delete(game.score_confirmation);
  game.score_confirmation = nullptr;
  game.pending_score_cell = nullptr;
  game.pending_score_player = -1;
  game.pending_score_category = -1;
  game.pending_score_cancelled = false;
}

static int score_for_category(int category)
{
  int counts[7] = {};
  int sum = 0;
  for (int i = 0; i < DICE_COUNT; ++i) {
    ++counts[game.dice[i]];
    sum += game.dice[i];
  }

  if (category < 6) return counts[category + 1] * (category + 1);
  if (category == 6) {
    for (int value = 1; value <= 6; ++value)
      if (counts[value] >= 3) return sum;
    return 0;
  }
  if (category == 7) {
    for (int value = 1; value <= 6; ++value)
      if (counts[value] >= 4) return sum;
    return 0;
  }
  if (category == 8) {
    bool pair = false;
    bool triple = false;
    for (int value = 1; value <= 6; ++value) {
      pair |= counts[value] == 2;
      triple |= counts[value] == 3;
    }
    return pair && triple ? 25 : 0;
  }
  if (category == 9) {
    const bool low = counts[1] && counts[2] && counts[3] && counts[4] && counts[5];
    const bool high = counts[2] && counts[3] && counts[4] && counts[5] && counts[6];
    return low || high ? 30 : 0;
  }
  for (int value = 1; value <= 6; ++value)
    if (counts[value] == 5) return 50;
  return 0;
}

static int total_for_player(int player)
{
  int total = 0;
  for (int category = 0; category < CATEGORY_COUNT; ++category)
    if (game.scores[player][category] >= 0) total += game.scores[player][category];
  return total;
}

static bool game_finished()
{
  for (int player = 0; player < game.player_count; ++player)
    for (int category = 0; category < CATEGORY_COUNT; ++category)
      if (game.scores[player][category] < 0) return false;
  return true;
}

static void refresh_game_ui()
{
  if (!game.turn_label || !lv_obj_is_valid(game.turn_label)) return;

  char text[48] = {};
  if (game.rolling)
    std::snprintf(text, sizeof(text), i18n::tr("Player %d  -  rolling..."), game.active_player + 1);
  else
    std::snprintf(text, sizeof(text), i18n::tr("Player %d  -  roll %d of 3"),
                  game.active_player + 1, game.rolls);
  lv_label_set_text(game.turn_label, text);
  lv_obj_set_style_bg_color(game.turn_label, player_color(game.active_player), 0);

  for (int die = 0; die < DICE_COUNT; ++die) {
    if (game.dice[die] == 0) {
      lv_label_set_text(game.dice_labels[die], LV_SYMBOL_LOOP);
    } else {
      char value[4] = {};
      std::snprintf(value, sizeof(value), "%u", (unsigned)game.dice[die]);
      lv_label_set_text(game.dice_labels[die], value);
    }
    lv_obj_set_style_border_width(game.dice_buttons[die], game.held[die] ? 3 : 1, 0);
    lv_obj_set_style_border_color(game.dice_buttons[die],
                                  game.held[die] ? lv_color_hex(0xffd43b)
                                                 : lv_color_hex(0x737990), 0);
  }

  for (int player = 0; player < game.player_count; ++player) {
    for (int category = 0; category < CATEGORY_COUNT; ++category) {
      char score[8] = {};
      lv_obj_t *cell = game.score_cells[player][category];
      if (game.scores[player][category] >= 0) {
        std::snprintf(score, sizeof(score), "%d", game.scores[player][category]);
        lv_obj_set_style_text_color(game.score_labels[player][category], lv_color_white(), 0);
        // A committed score is filled but flat: it is information, no longer
        // an action the player can take.
        lv_obj_set_style_bg_opa(cell, LV_OPA_50, 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_border_color(cell, player_color(player), 0);
        lv_obj_set_style_radius(cell, 0, 0);
        lv_obj_set_style_shadow_width(cell, 0, 0);
      } else if (player == game.active_player && game.rolls > 0 && !game.rolling) {
        std::snprintf(score, sizeof(score), "%d", score_for_category(category));
        lv_obj_set_style_text_color(game.score_labels[player][category], lv_color_white(), 0);
        // Available categories deliberately look like small raised buttons.
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cell, 2, 0);
        lv_obj_set_style_border_color(cell, lv_color_white(), 0);
        lv_obj_set_style_radius(cell, 5, 0);
        lv_obj_set_style_shadow_width(cell, 5, 0);
        lv_obj_set_style_shadow_color(cell, player_color(player), 0);
        lv_obj_set_style_shadow_opa(cell, LV_OPA_30, 0);
      } else {
        std::snprintf(score, sizeof(score), "-");
        lv_obj_set_style_text_color(game.score_labels[player][category],
                                    lv_color_hex(0x737990), 0);
        lv_obj_set_style_bg_opa(cell, player == game.active_player ? LV_OPA_20 : LV_OPA_10, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_radius(cell, 0, 0);
        lv_obj_set_style_shadow_width(cell, 0, 0);
      }
      lv_label_set_text(game.score_labels[player][category], score);
    }
    char total[12] = {};
    std::snprintf(total, sizeof(total), "P%d %d", player + 1, total_for_player(player));
    lv_label_set_text(game.total_labels[player], total);
  }

  if (game.rolling) {
    lv_label_set_text(game.roll_label, i18n::tr("Rolling..."));
    lv_obj_add_state(game.roll_button, LV_STATE_DISABLED);
  } else if (game.rolls >= 3) {
    lv_label_set_text(game.roll_label, i18n::tr("Choose score"));
    lv_obj_add_state(game.roll_button, LV_STATE_DISABLED);
  } else {
    lv_label_set_text(game.roll_label, game.rolls == 0 ? i18n::tr("Roll dice") : i18n::tr("Roll again"));
    lv_obj_clear_state(game.roll_button, LV_STATE_DISABLED);
  }
}

static void show_finished_dialog()
{
  int winner = 0;
  int best = total_for_player(0);
  for (int player = 1; player < game.player_count; ++player) {
    const int total = total_for_player(player);
    if (total > best) {
      best = total;
      winner = player;
    }
  }

  lv_obj_t *box = lv_msgbox_create(nullptr);
  char message[80] = {};
  std::snprintf(message, sizeof(message), i18n::tr("Player %d wins with %d points!"), winner + 1, best);
  lv_msgbox_add_title(box, i18n::tr("Game finished"));
  lv_msgbox_add_text(box, message);
  lv_msgbox_add_close_button(box);
  lv_obj_set_width(box, 280);
  lv_obj_center(box);
}

static void roll_animation_cb(lv_timer_t *)
{
  for (int die = 0; die < DICE_COUNT; ++die)
    if (!game.held[die]) game.dice[die] = (uint8_t)(next_random() % 6 + 1);

  ++game.animation_step;
  if (game.animation_step >= 8) {
    stop_roll_animation();
    ++game.rolls;
  }
  refresh_game_ui();
}

static void roll_clicked(lv_event_t *event)
{
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || game.rolls >= 3 || game.rolling) return;
  game.rolling = true;
  game.animation_step = 0;
  game.roll_timer = lv_timer_create(roll_animation_cb, 70, nullptr);
  lv_timer_ready(game.roll_timer);
  refresh_game_ui();
}

static void die_clicked(lv_event_t *event)
{
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || game.rolls == 0 ||
      game.rolls >= 3 || game.rolling) return;
  const int die = (int)(intptr_t)lv_event_get_user_data(event);
  if (die < 0 || die >= DICE_COUNT) return;
  game.held[die] = !game.held[die];
  refresh_game_ui();
}

static bool pointer_inside(lv_event_t *event, lv_obj_t *object)
{
  lv_indev_t *indev = lv_event_get_indev(event);
  if (!indev || !object || !lv_obj_is_valid(object)) return false;
  lv_point_t point{};
  lv_indev_get_point(indev, &point);
  lv_area_t area{};
  lv_obj_get_coords(object, &area);
  return point.x >= area.x1 && point.x <= area.x2 &&
         point.y >= area.y1 && point.y <= area.y2;
}

static void show_score_confirmation(int player, int category, lv_obj_t *cell)
{
  cancel_score_confirmation();
  game.pending_score_player = (int8_t)player;
  game.pending_score_category = (int8_t)category;
  game.pending_score_cell = cell;

  lv_obj_t *popup = lv_label_create(lv_layer_top());
  game.score_confirmation = popup;
  char text[80] = {};
  std::snprintf(text, sizeof(text), i18n::tr("%s: %d points\nRelease to confirm"),
                i18n::tr(CATEGORY_NAMES[category]), score_for_category(category));
  lv_label_set_text(popup, text);
  lv_obj_set_size(popup, 230, 62);
  lv_obj_align(popup, LV_ALIGN_TOP_MID, 0, 48);
  lv_obj_set_style_bg_color(popup, player_color(player), 0);
  lv_obj_set_style_bg_opa(popup, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(popup, lv_color_white(), 0);
  lv_obj_set_style_border_width(popup, 2, 0);
  lv_obj_set_style_radius(popup, 10, 0);
  lv_obj_set_style_pad_top(popup, 9, 0);
  lv_obj_set_style_text_align(popup, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(popup, lv_color_white(), 0);
  lv_obj_clear_flag(popup, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_set_style_border_color(cell, lv_color_hex(0xffd43b), 0);
  lv_obj_set_style_border_width(cell, 3, 0);
}

static void commit_score(int player, int category)
{
  if (player != game.active_player || category >= CATEGORY_COUNT ||
      game.scores[player][category] >= 0) return;

  game.scores[player][category] = (int8_t)score_for_category(category);
  if (game_finished()) {
    refresh_game_ui();
    show_finished_dialog();
    return;
  }

  game.active_player = (uint8_t)((game.active_player + 1) % game.player_count);
  game.rolls = 0;
  for (int die = 0; die < DICE_COUNT; ++die) {
    game.dice[die] = 0;
    game.held[die] = false;
  }
  refresh_game_ui();
}

static void score_gesture(lv_event_t *event)
{
  const lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t *cell = (lv_obj_t *)lv_event_get_target(event);
  const int encoded = (int)(intptr_t)lv_event_get_user_data(event);
  const int player = (encoded >> 8) & 0xff;
  const int category = encoded & 0xff;

  if (code == LV_EVENT_PRESSED) {
    if (game.rolls == 0 || game.rolling || player != game.active_player ||
        category >= CATEGORY_COUNT || game.scores[player][category] >= 0) return;
    show_score_confirmation(player, category, cell);
  } else if (code == LV_EVENT_PRESSING && cell == game.pending_score_cell &&
             !game.pending_score_cancelled && !pointer_inside(event, cell)) {
    game.pending_score_cancelled = true;
    if (game.score_confirmation && lv_obj_is_valid(game.score_confirmation)) {
      lv_label_set_text(game.score_confirmation, i18n::tr("Selection cancelled\nRelease to continue"));
      lv_obj_set_style_bg_color(game.score_confirmation, lv_color_hex(0x495057), 0);
    }
  } else if (code == LV_EVENT_CLICKED && cell == game.pending_score_cell) {
    // LVGL emits CLICKED after a release that still belongs to this cell. It
    // is the reliable confirmation event on both SDL and the touch device.
    const bool confirm = !game.pending_score_cancelled;
    const int selected_player = game.pending_score_player;
    const int selected_category = game.pending_score_category;
    cancel_score_confirmation();
    if (confirm) commit_score(selected_player, selected_category);
    else refresh_game_ui();
  } else if (code == LV_EVENT_PRESS_LOST && cell == game.pending_score_cell) {
    cancel_score_confirmation();
    refresh_game_ui();
  }
}

static lv_obj_t *plain_panel(lv_obj_t *parent)
{
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  return obj;
}

static void create_game_board()
{
  stop_roll_animation();
  cancel_score_confirmation();
  lv_obj_clean(game.body);
  std::memset(game.score_cells, 0, sizeof(game.score_cells));
  std::memset(game.score_labels, 0, sizeof(game.score_labels));
  std::memset(game.total_labels, 0, sizeof(game.total_labels));
  std::memset(game.held, 0, sizeof(game.held));
  std::memset(game.dice, 0, sizeof(game.dice));
  std::memset(game.scores, -1, sizeof(game.scores));
  game.active_player = 0;
  game.rolls = 0;

  game.turn_label = lv_label_create(game.body);
  lv_obj_set_pos(game.turn_label, 8, 4);
  lv_obj_set_size(game.turn_label, 304, 28);
  lv_obj_set_style_text_color(game.turn_label, lv_color_white(), 0);
  lv_obj_set_style_text_align(game.turn_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(game.turn_label, 5, 0);
  lv_obj_set_style_radius(game.turn_label, 8, 0);
  lv_obj_set_style_bg_opa(game.turn_label, LV_OPA_COVER, 0);

  for (int die = 0; die < DICE_COUNT; ++die) {
    lv_obj_t *button = lv_button_create(game.body);
    lv_obj_set_pos(button, 8 + die * 62, 38);
    lv_obj_set_size(button, 56, 52);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xf8f9fa), 0);
    lv_obj_set_style_radius(button, 9, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, die_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)die);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_color(label, lv_color_hex(0x20243a), 0);
    lv_obj_set_style_text_font(label, i18n::font24(), 0);
    lv_obj_center(label);
    game.dice_buttons[die] = button;
    game.dice_labels[die] = label;
  }

  const lv_coord_t table_x = 8;
  const lv_coord_t table_y = 96;
  const lv_coord_t table_w = 304;
  const lv_coord_t name_w = 82;
  const lv_coord_t score_w = (table_w - name_w) / game.player_count;
  const lv_coord_t header_h = 24;
  const lv_coord_t row_h = 23;

  lv_obj_t *category_header = plain_panel(game.body);
  lv_obj_set_pos(category_header, table_x, table_y);
  lv_obj_set_size(category_header, name_w, header_h);
  lv_obj_set_style_bg_color(category_header, lv_color_hex(0x25283c), 0);
  lv_obj_t *category_header_label = lv_label_create(category_header);
  lv_label_set_text(category_header_label, i18n::tr("Score"));
  lv_obj_set_style_text_color(category_header_label, lv_color_hex(0xb7bdd7), 0);
  lv_obj_center(category_header_label);

  for (int player = 0; player < game.player_count; ++player) {
    lv_obj_t *header = plain_panel(game.body);
    lv_obj_set_pos(header, table_x + name_w + player * score_w, table_y);
    lv_obj_set_size(header, score_w, header_h);
    lv_obj_set_style_bg_color(header, player_color(player), 0);
    lv_obj_t *label = lv_label_create(header);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    game.total_labels[player] = label;
  }

  for (int category = 0; category < CATEGORY_COUNT; ++category) {
    const lv_coord_t y = table_y + header_h + category * row_h;
    lv_obj_t *name = plain_panel(game.body);
    lv_obj_set_pos(name, table_x, y);
    lv_obj_set_size(name, name_w, row_h - 1);
    lv_obj_set_style_bg_color(name, lv_color_hex(category & 1 ? 0x292d42 : 0x30344b), 0);
    lv_obj_t *label = lv_label_create(name);
    lv_label_set_text(label, i18n::tr(CATEGORY_NAMES[category]));
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 5, 0);

    for (int player = 0; player < game.player_count; ++player) {
      lv_obj_t *cell = lv_button_create(game.body);
      lv_obj_set_pos(cell, table_x + name_w + player * score_w, y);
      lv_obj_set_size(cell, score_w - 1, row_h - 1);
      lv_obj_set_style_bg_color(cell, player_color(player), 0);
      lv_obj_set_style_border_width(cell, 0, 0);
      lv_obj_set_style_radius(cell, 0, 0);
      lv_obj_set_style_shadow_width(cell, 0, 0);
      lv_obj_set_style_pad_all(cell, 0, 0);
      lv_obj_add_event_cb(cell, score_gesture, LV_EVENT_ALL,
                          (void *)(intptr_t)((player << 8) | category));
      lv_obj_t *score = lv_label_create(cell);
      lv_obj_center(score);
      game.score_cells[player][category] = cell;
      game.score_labels[player][category] = score;
    }
  }

  game.roll_button = lv_button_create(game.body);
  lv_obj_set_pos(game.roll_button, 50, 382);
  lv_obj_set_size(game.roll_button, 220, 46);
  lv_obj_set_style_bg_color(game.roll_button, lv_color_hex(0x845ef7), 0);
  lv_obj_set_style_radius(game.roll_button, 12, 0);
  lv_obj_add_event_cb(game.roll_button, roll_clicked, LV_EVENT_CLICKED, nullptr);
  game.roll_label = lv_label_create(game.roll_button);
  lv_obj_set_style_text_color(game.roll_label, lv_color_white(), 0);
  lv_obj_center(game.roll_label);
  refresh_game_ui();
}

static void player_count_clicked(lv_event_t *event)
{
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  game.player_count = (uint8_t)(intptr_t)lv_event_get_user_data(event);
  create_game_board();
}

static void show_player_selection()
{
  lv_obj_t *title = lv_label_create(game.body);
  lv_label_set_text(title, i18n::tr("How many players?"));
  lv_obj_set_style_text_font(title, i18n::font24(), 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);

  lv_obj_t *hint = lv_label_create(game.body);
  lv_label_set_text(hint, i18n::tr("Pass the device after each turn"));
  lv_obj_set_style_text_color(hint, lv_color_hex(0xb7bdd7), 0);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 82);

  for (int player = 0; player < MAX_PLAYERS; ++player) {
    lv_obj_t *button = lv_button_create(game.body);
    const int col = player % 2;
    const int row = player / 2;
    lv_obj_set_pos(button, 28 + col * 140, 130 + row * 112);
    lv_obj_set_size(button, 124, 92);
    lv_obj_set_style_bg_color(button, player_color(player), 0);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_add_event_cb(button, player_count_clicked, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(player + 1));
    char value[16] = {};
    std::snprintf(value, sizeof(value), i18n::tr("%d player%s"), player + 1, player && !i18n::german() ? "s" : "");
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, value);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
  }
}

static void nav_back_event(lv_event_t *event)
{
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  stop_roll_animation();
  cancel_score_confirmation();
  UiSessionState::set_route("home");
  lv_obj_t *home = (lv_obj_t *)lv_event_get_user_data(event);
  if (home && lv_obj_is_valid(home)) ui_load_screen(home, true);
}

static void restart_event(lv_event_t *event)
{
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !game.body ||
      !lv_obj_is_valid(game.body)) return;
  stop_roll_animation();
  cancel_score_confirmation();
  lv_obj_clean(game.body);
  show_player_selection();
}

static void screen_deleted(lv_event_t *)
{
  stop_roll_animation();
  cancel_score_confirmation();
}

}  // namespace

void dice_game_app_launch(void)
{
  lv_obj_t *home = lv_screen_active();
  game = DiceGameCtx{};
  game.random_state ^= lv_tick_get();

  lv_obj_t *screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  game.screen = screen;
  lv_obj_add_event_cb(screen, screen_deleted, LV_EVENT_DELETE, nullptr);

  lv_obj_t *bar = plain_panel(screen);
  lv_obj_set_size(bar, 320, 44);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x14162a), 0);

  lv_obj_t *back = lv_button_create(bar);
  lv_obj_set_pos(back, 2, 0);
  lv_obj_set_size(back, 52, 44);
  lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_add_event_cb(back, nav_back_event, LV_EVENT_CLICKED, home);
  lv_obj_t *back_label = lv_label_create(back);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
  lv_obj_center(back_label);

  lv_obj_t *title = lv_label_create(bar);
  lv_label_set_text(title, i18n::tr("Dice Score"));
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_center(title);

  lv_obj_t *restart = lv_button_create(bar);
  lv_obj_set_pos(restart, 268, 0);
  lv_obj_set_size(restart, 50, 44);
  lv_obj_set_style_bg_opa(restart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(restart, 0, 0);
  lv_obj_add_event_cb(restart, restart_event, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *restart_label = lv_label_create(restart);
  lv_label_set_text(restart_label, LV_SYMBOL_REFRESH);
  lv_obj_set_style_text_color(restart_label, lv_color_white(), 0);
  lv_obj_center(restart_label);

  game.body = plain_panel(screen);
  lv_obj_set_pos(game.body, 0, 44);
  lv_obj_set_size(game.body, 320, 436);
  lv_obj_set_style_bg_color(game.body, lv_color_hex(0x1e2030), 0);
  show_player_selection();

  ui_load_screen(screen, false);
  UiSessionState::set_route("dice_game");
}
