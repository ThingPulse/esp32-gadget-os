#include "ui/StatusIcons.h"

#include "EventBus.h"
#include "services/WifiService.h"

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