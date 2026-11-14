#pragma once
#include "services/StockDataProvider.h"
#include <lvgl.h>
#include "ui/ChartShade.h"
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

using chart_shade::shade;
}
