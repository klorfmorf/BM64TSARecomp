#ifndef __BANJO_LAUNCHER_H__
#define __BANJO_LAUNCHER_H__

#include "recompui/recompui.h"

#define RIGHT_POSITION_START 60.0f

namespace banjo {
    void launcher_animation_setup(recompui::LauncherMenu *menu);
    void launcher_animation_update(recompui::LauncherMenu *menu);

    constexpr float launcher_options_right_position_start = RIGHT_POSITION_START;
    constexpr float launcher_options_right_position_end = 0.0f + 24.0f;
    constexpr float launcher_options_top_offset = -10.0f;
    constexpr float launcher_options_title_offset = 120.0f;
}

#endif
