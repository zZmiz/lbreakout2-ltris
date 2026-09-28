
#include "rp_platform.h"

extern "C" int lbreakout2_game_main(int argc, char **argv);

int main()
{
    if (!rp_platform_init("/games/lbreakout2", "LBreakout2")) rp_exit(2);
    char name[] = "lbreakout2";
    char *argv[] = {name, nullptr};
    int status=lbreakout2_game_main(1, argv);
    if (!status) rp_system_game_finished();
    rp_exit(status);
}
