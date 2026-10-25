#include "ui/I18n.h"
#include "apps/PlanesApp.h"
#include "AppRegistry.h"
#include "services/PlanesService.h"
#include "ui/NearbyPlanesView.h"
#include "ui/UiNav.h"
#include "ui/UiSessionState.h"

APP_REGISTER_COLOR_RANK("Planes", LV_SYMBOL_GPS, planes_app_launch, 0x339af0, 20);

namespace {
struct PlanesAppCtx {
    NearbyPlanesView nearby;
    lv_obj_t *screen = nullptr, *loading_line = nullptr;
    lv_timer_t *refresh_timer = nullptr;
    unsigned long planes_revision = 0;
    bool was_fetching = false, closing = false;
};
static PlanesAppCtx app_ctx;

static void planes_app_update_loading() {
    if (PlanesService::instance().is_fetching())
        lv_obj_clear_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN);
}
static void planes_app_update_ui() {
    if (!app_ctx.closing) nearby_planes_update(app_ctx.nearby);
}
static void planes_app_timer_cb(lv_timer_t *) {
    if (app_ctx.closing) return;
    auto &service = PlanesService::instance();
    nearby_planes_tick(app_ctx.nearby);
    const bool fetching = service.is_fetching();
    const auto revision = service.data_revision();
    if ((!fetching && app_ctx.was_fetching) || revision != app_ctx.planes_revision)
        planes_app_update_ui();
    app_ctx.was_fetching = fetching;
    app_ctx.planes_revision = revision;
    planes_app_update_loading();
}
static void planes_app_refresh_event(lv_event_t *) {
    if (app_ctx.closing) return;
    PlanesService::instance().refresh_now();
    planes_app_update_loading();
    nearby_planes_tick(app_ctx.nearby);
}
static void planes_app_screen_delete_event(lv_event_t *) {
    auto &service = PlanesService::instance();
    service.pause();
    service.reset_search_area();
    if (app_ctx.refresh_timer) lv_timer_delete(app_ctx.refresh_timer);
    app_ctx = PlanesAppCtx{};
}
static void nav_back_event(lv_event_t *event) {
    if (app_ctx.closing) return;
    auto *target = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
    if (!target || !lv_obj_is_valid(target)) return;
    app_ctx.closing = true;
    PlanesService::instance().pause();
    if (app_ctx.refresh_timer) {
        lv_timer_delete(app_ctx.refresh_timer);
        app_ctx.refresh_timer = nullptr;
    }
    UiSessionState::set_route("home");
    ui_load_screen(target, true);
}
}

void planes_app_launch() {
    auto *home = lv_screen_active();
    auto *screen = lv_obj_create(nullptr);
    app_ctx.screen = screen;
    app_ctx.closing = false;
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1e2030), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, planes_app_screen_delete_event, LV_EVENT_DELETE, nullptr);

    auto *bar = lv_obj_create(screen);
    lv_obj_set_size(bar, lv_pct(100), 44);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x14162a), 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    auto *back = lv_button_create(bar);
    lv_obj_set_size(back, 48, 44);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, nav_back_event, LV_EVENT_CLICKED, home);
    auto *back_label = lv_label_create(back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);
    auto *title = lv_label_create(bar);
    lv_label_set_text(title, i18n::tr("Nearby aircraft"));
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 56, 0);
    auto *refresh = lv_button_create(bar);
    lv_obj_set_size(refresh, 44, 40);
    lv_obj_align(refresh, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_event_cb(refresh, planes_app_refresh_event, LV_EVENT_CLICKED, nullptr);
    auto *refresh_label = lv_label_create(refresh);
    lv_label_set_text(refresh_label, LV_SYMBOL_REFRESH);
    lv_obj_center(refresh_label);

    auto *loading = lv_obj_create(bar);
    app_ctx.loading_line = loading;
    lv_obj_set_size(loading, 76, 3);
    lv_obj_set_pos(loading, -76, 41);
    lv_obj_set_style_bg_color(loading, lv_color_hex(0x64c8ff), 0);
    lv_obj_set_style_border_width(loading, 0, 0);
    lv_obj_clear_flag(loading, LV_OBJ_FLAG_SCROLLABLE);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, loading);
    lv_anim_set_exec_cb(&animation, [](void *obj, int32_t x) { lv_obj_set_x(static_cast<lv_obj_t *>(obj), x); });
    lv_anim_set_values(&animation, -76, 320);
    lv_anim_set_duration(&animation, 900);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);

    auto *content = lv_obj_create(screen);
    lv_obj_set_pos(content, 0, 44);
    lv_obj_set_size(content, lv_pct(100), lv_display_get_vertical_resolution(nullptr) - 44);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    nearby_planes_create(app_ctx.nearby, content);
    app_ctx.nearby.range_changed = []() {
        auto &service = PlanesService::instance();
        if (service.is_configured())
            service.set_search_area(service.home_latitude(), service.home_longitude(), app_ctx.nearby.radius_km, true);
        planes_app_update_ui();
        if (!service.is_fetching()) service.refresh_now();
        planes_app_update_loading();
    };
    auto &service = PlanesService::instance();
    if (service.is_configured())
        service.set_search_area(service.home_latitude(), service.home_longitude(), app_ctx.nearby.radius_km, true);
    service.resume();
    app_ctx.was_fetching = service.is_fetching();
    app_ctx.planes_revision = service.data_revision();
    app_ctx.refresh_timer = lv_timer_create(planes_app_timer_cb, 100, nullptr);
    planes_app_update_ui();
    planes_app_update_loading();
    ui_load_screen(screen, false);
    UiSessionState::set_route("planes");
}

#if !defined(ARDUINO)
bool planes_app_tutorial_ready(unsigned minimum_points) {
    if (!app_ctx.screen || app_ctx.closing || app_ctx.nearby.count < 2) return false;
    const auto &service = PlanesService::instance();
    for (int n = 0; n < app_ctx.nearby.count; ++n) {
        const auto &plane = service.planes()[app_ctx.nearby.indices[n]];
        if (plane.on_ground || plane.route.find("->") == std::string::npos) return false;
        bool ready = false;
        for (const auto &trail : app_ctx.nearby.trails)
            if (plane.icao24 == trail.icao && trail.count >= minimum_points) ready = true;
        if (!ready) return false;
    }
    return true;
}
#endif
