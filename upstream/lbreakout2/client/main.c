/***************************************************************************
                          main.c  -  description
                             -------------------
    begin                : Don Sep  6 12:02:57 CEST 2001
    copyright            : (C) 2001 by Michael Speck
    email                : kulkanie@gmx.net
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifdef _WIN32
#include <fcntl.h>
#endif

#ifdef __unix__
#include <unistd.h>
#endif

#include "lbreakout.h"
#include "../game/game.h"
#include "file.h"
#include "chart.h"
#include "config.h"
#include "shrapnells.h"
#include "event.h"
#include "game.h"
#include "manager.h"
#include "editor.h"
#include "help.h"
#include "theme.h"
#include "client.h"
#include "../gui/gui.h"
#include "setselect.h"

#define CHART_FILE_NAME "lbreakout2.hscr"

extern int stk_quit_request;
extern SDL_Surface *stk_display;
extern Config config;
extern StkFont *font;
extern char **theme_names;
extern int theme_count;
extern SDL_Surface *mbkgnd, *cr_bkgnd;
extern char *edit_set;
extern char **levelset_names_local; /* non-network levelsets */
extern int levelset_count_local;
extern int gameSeed;
#ifdef RP_HOST_EXHAUSTIVE
extern void client_game_finalize_exhaustive_test(void);
extern size_t rp_psram_debug_mark(void);
extern void rp_psram_debug_dump_since(size_t mark);
#endif

/* used to compile net messages */
char msgbuf[MAX_MSG_SIZE];
int msglen = 0;

FILE *hi_dir_chart_file = NULL;

int main(int argc, char *argv[])
{
    int result = ACTION_NONE;
    int leave = 0;
    char *editor_file = 0;
    char path[512];
    const char *set_name = NULL;
#ifdef LGAMES_PORT
    char *selected_set_copy = NULL;
#endif
    SDL_Surface *loading;
#ifdef __unix__
    gid_t realgid;

    hi_dir_chart_file = fopen(HI_DIR "/" CHART_FILE_NAME, "r+");

    /* This is where we drop our setuid/setgid privileges.
     */
    realgid = getgid();
    if (setresgid(-1, realgid, realgid) != 0) {
        perror("Could not drop setgid privileges.  Aborting.");
        exit(1);
    }
#endif
   
/* i18n */
#ifdef ENABLE_NLS
    setlocale (LC_ALL, "");
    bindtextdomain (PACKAGE, LOCALEDIR);
    textdomain (PACKAGE);
#endif
    
#ifdef _WIN32    
    /* Get Windows to open files in binary mode instead of default text mode */
    _fmode = _O_BINARY;
#endif    
    
    /* lbreakout info */
    printf( "LBreakout2 %s\nCopyright 2001-2011 Michael Speck\nPublished under GNU GPL\n---\n", VERSION );
    printf( "Looking up data in: %s\n", SRC_DIR );
    printf( "Looking up highscores in: %s\n", HI_DIR );
    printf( "Looking up custom levels in: %s/%s/lbreakout2-levels\n", (getenv( "HOME" )?getenv( "HOME" ):"."), CONFIG_DIR_NAME );
#ifndef AUDIO_ENABLED
    printf( "Compiled without sound and music\n" );
#endif

    set_random_seed(); /* set random seed */

    config_load();
    
    stk_init( SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK );
    if ( config.fullscreen )
        stk_display_open( SDL_SWSURFACE | SDL_FULLSCREEN, 640, 480, 16 );
    else
        stk_display_open( SDL_SWSURFACE, 640, 480, 16 );
    stk_audio_open( 0,0,0,config.audio_buffer_size );
    SDL_WM_SetCaption( "LBreakout2", 0 );
    SDL_SetEventFilter( event_filter );
    stk_audio_enable_sound( config.sound );
    stk_audio_set_sound_volume( config.volume * 16 );
    
    /* load a little loading pic */
    stk_surface_set_path( SRC_DIR "/gfx" );
    loading = stk_surface_load( SDL_SWSURFACE, "loading.png" );
    stk_surface_blit( loading, 0,0,-1,-1, stk_display, 
                      (stk_display->w-loading->w)/2, 
                      (stk_display->h-loading->h)/2 );
    stk_display_update( STK_UPDATE_ALL );
    
    /* load the GUI graphics from SRC_DIR/gui_theme */
    stk_surface_set_path( SRC_DIR );
    stk_audio_set_path( SRC_DIR );
    gui_init( "gui_theme" );

    stk_surface_set_path( SRC_DIR "/gfx" );
    stk_audio_set_path( SRC_DIR "/sounds" );
    
    /* load resources */
    /* for simplicity all functions are kept but anything
     * that is now themeable is loaded in
     * theme_load instead of the original function
     * (deleting resources works analouge)
     */
    theme_get_list();
    if ( config.theme_count != theme_count ) {
        if ( config.theme_id >= theme_count )
            config.theme_id = 0;
        config.theme_count = theme_count;
    }
    theme_load( theme_names[config.theme_id] );
    /* old functions still with initialzations of
     * lists or variables 
     */
    client_game_create();
    hint_load_res();
    chart_load();
    manager_create();
    client_create();
    exp_load();
#ifndef LGAMES_PORT
    editor_create();
    help_create();
    setselect_create();
#endif
#ifdef RP_HOST_EXHAUSTIVE
    /* Exercise the same game initializer used after the AddOn selector for
     * every visible entry while the host shim enforces the real 8 MiB PSRAM
     * ceiling. This is test-only and is not present in device builds. */
    if (getenv("RP_HOST_EXHAUSTIVE_RUN")) {
    for (int set_id = 0; set_id < levelset_count_local; ++set_id) {
        size_t allocation_mark = rp_psram_debug_mark();
        gameSeed = set_id + 1;
        printf("ADDON TEST %d/%d: %s (free=%zu)\n", set_id + 1,
               levelset_count_local, levelset_names_local[set_id],
               rp_psram_available());
        if (!client_game_init_local(levelset_names_local[set_id])) {
            printf("ADDON FAIL: %s\n", levelset_names_local[set_id]);
            return EXIT_FAILURE;
        }
        client_game_finalize_exhaustive_test();
        if (set_id == 0) rp_psram_debug_dump_since(allocation_mark);
        printf("ADDON PASS: %s (free=%zu)\n", levelset_names_local[set_id],
               rp_psram_available());
    }
    printf("ADDON EXHAUSTIVE PASS: %d entries\n", levelset_count_local);
    goto cleanup;
    }
#endif
    /* run game */
    manager_fade( STK_FADE_IN );
    while( !leave && !stk_quit_request ) {
        result = manager_run();
        switch( result ) {
            case ACTION_QUIT: leave = 1; break;
            case ACTION_RESUME_0:
#ifndef LGAMES_PORT
                manager_fade( STK_FADE_OUT );
#endif
		if ( client_game_resume_local( 0 ) )
			client_game_run();
                client_game_finalize();
                manager_fade( STK_FADE_IN );
                break;
            case ACTION_PLAY_LBR:
#ifndef LGAMES_PORT
                manager_fade( STK_FADE_OUT );
#endif
                /* Leave the presented menu visible during level preparation.
                 * client_game_run publishes the complete game without fades. */
                gameSeed = rand(); /* set random seed for next FREAKOUT/BonusLevels */
		if ( client_game_init_local( "LBreakout2" ) )
			client_game_run();
                client_game_finalize();
                manager_fade( STK_FADE_IN );
                break;
            case ACTION_PLAY_CUSTOM:
#ifdef LGAMES_PORT
                setselect_create();
#endif
                /* run select dialog first */
                if ((set_name = setselect_run()) == NULL) {
#ifdef LGAMES_PORT
                    setselect_delete();
#endif
			manager_show();
			break;
			}
#ifdef LGAMES_PORT
                /* The selector owns set_name and keeps a full-screen backing
                 * surface plus preview resources. Preserve the name, then
                 * release that UI before allocating the selected game. */
                selected_set_copy = strdup(set_name);
                setselect_delete();
                set_name = selected_set_copy;
#endif
#ifndef LGAMES_PORT
                manager_fade( STK_FADE_OUT );
#endif
                gameSeed = rand(); /* set random seed for next FREAKOUT/BonusLevels */
                if (gameSeed==0) gameSeed=1; /* not allowed because.... A HACK!!! 0 means to have
                                                no bonus levels to save a parameter */
		if ( client_game_init_local( set_name ) )
			client_game_run();
                client_game_finalize();
#ifdef LGAMES_PORT
                free(selected_set_copy);
                selected_set_copy = NULL;
#endif
                manager_fade( STK_FADE_IN );
                break;
            case ACTION_EDIT:
                /* new set? */
                if ( strequal( NEW_SET, edit_set ) ) {
                    editor_file = calloc( 16, sizeof( char ) );
                    snprintf( path, sizeof(path)-1, "%s/%s/lbreakout2-levels", getenv( "HOME" )? getenv("HOME"):".", CONFIG_DIR_NAME );
                    if ( !enter_string( font, _("Set Name:"), editor_file, 12 ) || !file_check( path, editor_file, "w" ) ) {
                        free( editor_file );
                        break;
                    }
                    else
                        manager_update_set_list();
                }
                else
                    editor_file = strdup( edit_set );
#ifdef LGAMES_PORT
                editor_create();
#endif
                if ( editor_init( editor_file ) ) {
                    manager_fade( STK_FADE_OUT );
                    editor_run();
                    editor_clear();
                    manager_fade( STK_FADE_IN );
                }
                free( editor_file ); editor_file = 0;
#ifdef LGAMES_PORT
                editor_delete();
#endif
                break;
			case ACTION_QUICK_HELP:
#ifdef LGAMES_PORT
                help_create();
#endif
				help_run();
#ifdef LGAMES_PORT
                help_delete();
#endif
				break;
            case ACTION_CLIENT:
                manager_fade( STK_FADE_OUT );
                client_run();
                manager_fade( STK_FADE_IN );
                break;
            default: break;
        }
    }
cleanup:
    manager_fade( STK_FADE_OUT );
    /* delete stuff */
    setselect_delete();
    help_delete();
    manager_delete();
    chart_save();
    chart_delete();
    editor_delete();
    exp_delete();
    client_game_delete();
    hint_delete_res();
    theme_delete();
    theme_delete_list();
    stk_surface_free( &loading );
    
    config_save();
    
    if (hi_dir_chart_file)
        fclose(hi_dir_chart_file);

    return EXIT_SUCCESS;
}
