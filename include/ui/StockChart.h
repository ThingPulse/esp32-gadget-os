#pragma once
#include "services/StockDataProvider.h"
#include <lvgl.h>
#include <algorithm>
#include <cmath>

namespace stock_chart {
// Monotone cubic Hermite interpolation: round the joins without adding price
// extrema. Missing observations remain gaps in both the line and shaded area.
inline double sample(const stocks::Quote &q, size_t i, double t) {
    const double a=q.samples[i].price;
    if(t==0 || i+1>=q.count) return a;
    const double b=q.samples[i+1].price;
    if(!std::isfinite(a) || !std::isfinite(b)) return NAN;
    const double delta=b-a;
    auto tangent=[](double left,double right) {
        return left*right<=0?0.0:2*left*right/(left+right);
    };
    const double m0=i && std::isfinite(q.samples[i-1].price)?tangent(a-q.samples[i-1].price,delta):delta;
    const double m1=i+2<q.count && std::isfinite(q.samples[i+2].price)?tangent(delta,q.samples[i+2].price-b):delta;
    const double t2=t*t,t3=t2*t;
    const double value=(2*t3-3*t2+1)*a+(t3-2*t2+t)*m0+(-2*t3+3*t2)*b+(t3-t2)*m1;
    return std::max(std::min(a,b),std::min(std::max(a,b),value));
}

inline void shade(lv_event_t *event) {
    auto *task=lv_event_get_draw_task(event);
    auto *line=lv_draw_task_get_line_dsc(task);
    if(!line) return;
    if(line->base.part==LV_PART_MAIN) {
        line->dash_width=3; line->dash_gap=5; line->opa=100;
        return;
    }
    if(line->base.part!=LV_PART_ITEMS || !line->points) return;
    auto *chart=lv_event_get_target_obj(event);
    lv_area_t bounds;lv_obj_get_coords(chart,&bounds);
    const int height=std::max(1,int(lv_area_get_height(&bounds)));
    auto opacity=[&](int y) {return lv_opa_t(std::max(0,std::min(90,90*(int(bounds.y2)-y)/height)));};
    auto gradient=[&](lv_grad_dsc_t &g,int top,int bottom) {
        g.dir=LV_GRAD_DIR_VER;g.stops_count=2;
        g.stops[0].color=g.stops[1].color=line->color;
        g.stops[0].frac=0;g.stops[1].frac=255;
        g.stops[0].opa=opacity(top);g.stops[1].opa=opacity(bottom);
    };
    for(int i=0;i<line->point_cnt-1;++i) {
        const auto a=line->points[i],b=line->points[i+1];
        if(a.y==LV_DRAW_LINE_POINT_NONE || b.y==LV_DRAW_LINE_POINT_NONE ||
           a.x==LV_DRAW_LINE_POINT_NONE || b.x==LV_DRAW_LINE_POINT_NONE || b.x<=a.x) continue;
        const int top=int(std::min(a.y,b.y)),bottom=int(std::max(a.y,b.y));
        if(top!=bottom) {
            lv_draw_triangle_dsc_t triangle;lv_draw_triangle_dsc_init(&triangle);
            triangle.p[0]=a;triangle.p[1]=b;
            triangle.p[2].x=a.y<b.y?a.x:b.x;triangle.p[2].y=bottom;
            gradient(triangle.grad,top,bottom);lv_draw_triangle(line->base.layer,&triangle);
        }
        lv_draw_rect_dsc_t fill;lv_draw_rect_dsc_init(&fill);
        gradient(fill.bg_grad,bottom,bounds.y2);
        lv_area_t area={int32_t(a.x),bottom,int32_t(b.x)-1,bounds.y2};
        lv_draw_rect(line->base.layer,&fill,&area);
    }
    if(line->point_cnt>0) {
        const auto last=line->points[line->point_cnt-1];
        if(last.y==LV_DRAW_LINE_POINT_NONE || last.x==LV_DRAW_LINE_POINT_NONE) return;
        lv_draw_rect_dsc_t dot;lv_draw_rect_dsc_init(&dot);dot.radius=LV_RADIUS_CIRCLE;dot.bg_color=line->color;
        lv_area_t halo={int32_t(last.x)-6,int32_t(last.y)-6,int32_t(last.x)+6,int32_t(last.y)+6};
        dot.bg_opa=45;lv_draw_rect(line->base.layer,&dot,&halo);
        lv_area_t center={int32_t(last.x)-3,int32_t(last.y)-3,int32_t(last.x)+3,int32_t(last.y)+3};
        dot.bg_opa=LV_OPA_COVER;lv_draw_rect(line->base.layer,&dot,&center);
    }
}
}
