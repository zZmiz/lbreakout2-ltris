
#include "rp_platform.h"

extern "C" int ltris_game_main(int argc, char **argv);

int main()
{
    if (!rp_platform_init("/games/ltris", "LTris")) rp_exit(2);
    char name[] = "ltris";
    char *argv[] = {name, nullptr};
    int status=ltris_game_main(1, argv);
    if (!status) rp_system_game_finished();
    rp_exit(status);
}
