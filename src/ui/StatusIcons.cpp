#include "ui/StatusIcons.h"
#include "ui/WeatherIconSet.h"

#include "EventBus.h"
#include "services/TimeService.h"
#include "services/WeatherService.h"
#include "services/WifiService.h"
#include <cstdio>

struct WifiIconCtx {
    lv_obj_t *label;
    SubscriptionId connecting_sub;
    SubscriptionId connected_sub;
    SubscriptionId disconnected_sub;
};

static void update_wifi_icon(lv_obj_t *label)
{
    if (!label || !lv_obj_is_valid(label))
        return;

    WifiService &wifi = WifiService::instance();
    lv_label_set_text(label, LV_SYMBOL_WIFI);

    if (wifi.is_connected())
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    else if (wifi.is_connecting())
        lv_obj_set_style_text_color(label, lv_color_hex(0xd9a441), 0);
    else
        lv_obj_set_style_text_color(label, lv_color_hex(0x666666), 0);
}

static void wifi_status_event(const SystemEvent &event, void *user_data)
{
    (void)event;
    WifiIconCtx *ctx = static_cast<WifiIconCtx *>(user_data);
    if (!ctx)
        return;

    update_wifi_icon(ctx->label);
}

static void wifi_icon_deleted(lv_event_t *e)
{
    WifiIconCtx *ctx = static_cast<WifiIconCtx *>(lv_event_get_user_data(e));
    if (!ctx)
        return;

    EventBus::instance().unsubscribe(ctx->connecting_sub);
    EventBus::instance().unsubscribe(ctx->connected_sub);
    EventBus::instance().unsubscribe(ctx->disconnected_sub);
    delete ctx;
}

lv_obj_t *create_wifi_status_icon(lv_obj_t *parent, lv_coord_t x_offset)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, x_offset, 0);

    WifiIconCtx *ctx = new WifiIconCtx{};
    ctx->label = label;
    ctx->connecting_sub = EventBus::instance().subscribe(EventType::WifiConnecting, wifi_status_event, ctx);
    ctx->connected_sub = EventBus::instance().subscribe(EventType::WifiConnected, wifi_status_event, ctx);
    ctx->disconnected_sub = EventBus::instance().subscribe(EventType::WifiDisconnected, wifi_status_event, ctx);

    lv_obj_add_event_cb(label, wifi_icon_deleted, LV_EVENT_DELETE, ctx);
    update_wifi_icon(label);
    return label;
}

struct TimeLabelCtx {
    lv_obj_t *label;
    lv_timer_t *timer;
    SubscriptionId sync_sub;
};

static void update_time_label(lv_obj_t *label)
{
    if (!label || !lv_obj_is_valid(label))
        return;

    TimeService &time_service = TimeService::instance();
    std::string value = time_service.time_hhmm();
    lv_label_set_text(label, value.c_str());
    lv_obj_set_style_text_color(label, time_service.is_synchronized() ? lv_color_white() : lv_color_hex(0x888888), 0);
}

static void time_tick_cb(lv_timer_t *timer)
{
    TimeLabelCtx *ctx = static_cast<TimeLabelCtx *>(lv_timer_get_user_data(timer));
    if (!ctx)
        return;

    update_time_label(ctx->label);
}

static void time_sync_event(const SystemEvent &event, void *user_data)
{
    (void)event;
    TimeLabelCtx *ctx = static_cast<TimeLabelCtx *>(user_data);
    if (!ctx)
        return;

    update_time_label(ctx->label);
}

static void time_label_deleted(lv_event_t *e)
{
    TimeLabelCtx *ctx = static_cast<TimeLabelCtx *>(lv_event_get_user_data(e));
    if (!ctx)
        return;

    EventBus::instance().unsubscribe(ctx->sync_sub);
    if (ctx->timer)
        lv_timer_delete(ctx->timer);
    delete ctx;
}

lv_obj_t *create_time_status_label(lv_obj_t *parent, lv_coord_t x_offset)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, x_offset, 0);

    TimeLabelCtx *ctx = new TimeLabelCtx{};
    ctx->label = label;
    ctx->sync_sub = EventBus::instance().subscribe(EventType::TimeSynchronized, time_sync_event, ctx);
    ctx->timer = lv_timer_create(time_tick_cb, 1000, ctx);

    lv_obj_add_event_cb(label, time_label_deleted, LV_EVENT_DELETE, ctx);
    update_time_label(label);
    return label;
}

struct WeatherCenterCtx {
    lv_obj_t *root;
    lv_obj_t *weather_row;
    lv_obj_t *icon;
    lv_obj_t *temp_label;
    lv_obj_t *fallback_label;
    std::string fallback;
    SubscriptionId weather_updated_sub;
    SubscriptionId weather_cleared_sub;
};

static void update_weather_center_label(WeatherCenterCtx *ctx)
{
    if (!ctx || !ctx->root || !lv_obj_is_valid(ctx->root))
        return;

    WeatherService &weather = WeatherService::instance();
    if (weather.has_weather()) {
        weather_icon_update(ctx->icon, weather_icon_kind_from_code(weather.weather_code()), 16);
        const char unit = weather.is_metric() ? 'C' : 'F';
        char buf[24] = {0};
        std::snprintf(buf, sizeof(buf), "%.1f%c", weather.temperature_value(), unit);
        lv_label_set_text(ctx->temp_label, buf);

        lv_obj_add_flag(ctx->fallback_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->weather_row, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(ctx->fallback_label, ctx->fallback.c_str());
        lv_obj_add_flag(ctx->weather_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->fallback_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void weather_center_event(const SystemEvent &event, void *user_data)
{
    (void)event;
    WeatherCenterCtx *ctx = static_cast<WeatherCenterCtx *>(user_data);
    update_weather_center_label(ctx);
}

static void weather_center_deleted(lv_event_t *e)
{
    WeatherCenterCtx *ctx = static_cast<WeatherCenterCtx *>(lv_event_get_user_data(e));
    if (!ctx)
        return;

    EventBus::instance().unsubscribe(ctx->weather_updated_sub);
    EventBus::instance().unsubscribe(ctx->weather_cleared_sub);
    delete ctx;
}

lv_obj_t *create_weather_center_label(lv_obj_t *parent, const char *fallback_text, lv_coord_t x_offset)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_align(root, LV_ALIGN_CENTER, x_offset, 0);
    lv_obj_set_size(root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_pad_column(root, 4, 0);
    lv_obj_set_layout(root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *weather_row = lv_obj_create(root);
    lv_obj_set_size(weather_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(weather_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(weather_row, 0, 0);
    lv_obj_set_style_bg_opa(weather_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(weather_row, 0, 0);
    lv_obj_set_style_pad_column(weather_row, 3, 0);
    lv_obj_set_layout(weather_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(weather_row, LV_FLEX_FLOW_ROW);

    lv_obj_t *icon = weather_icon_create(weather_row, WeatherIconKind::Clear, 16);
    lv_obj_t *temp_label = lv_label_create(weather_row);
    lv_obj_set_style_text_color(temp_label, lv_color_white(), 0);

    lv_obj_t *fallback_label = lv_label_create(root);
    lv_obj_set_style_text_color(fallback_label, lv_color_white(), 0);

    WeatherCenterCtx *ctx = new WeatherCenterCtx{};
    ctx->root = root;
    ctx->weather_row = weather_row;
    ctx->icon = icon;
    ctx->temp_label = temp_label;
    ctx->fallback_label = fallback_label;
    ctx->fallback = fallback_text ? fallback_text : "";
    ctx->weather_updated_sub = EventBus::instance().subscribe(EventType::WeatherUpdated, weather_center_event, ctx);
    ctx->weather_cleared_sub = EventBus::instance().subscribe(EventType::WeatherCleared, weather_center_event, ctx);

    lv_obj_add_event_cb(root, weather_center_deleted, LV_EVENT_DELETE, ctx);
    update_weather_center_label(ctx);
    return root;
}