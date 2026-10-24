#pragma once

void planes_app_launch(void);
#if !defined(ARDUINO)
bool planes_app_tutorial_ready(unsigned minimum_points);
#endif
