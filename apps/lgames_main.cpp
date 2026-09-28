
#include "rp_platform.h"

int main()
{
    rp_system_dispatch_early();
    if (!rp_platform_init("/games", "LGames")) rp_exit(2);
    for (;;) {
        int selected=rp_system_game_list();
        if (selected<0) rp_exit(0);
        rp_system_launch(selected);
    }
}
