/***************************************************************************
                          main.cpp  -  description
                             -------------------
    begin                : Die Feb 29 13:27:43 MET 2000
    copyright            : (C) 2000 by Michael Speck
    email                : 
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <unistd.h>
#include "ltris.h"
#include "event.h"
#include "chart.h"
#include "manager.h"
#include "tetris.h"
#include "hint.h"

int term_game = 0;
extern Sdl sdl;
extern Config config;
extern char gametype_names[GAME_TYPENUM][64];
extern char gametype_ids[GAME_TYPENUM][64];

extern int display_w, display_h, video_forced_w, video_forced_h;

int main(int argc, char *argv[])
{
    int c;
    int result = ACTION_NONE;
    int leave = 0;
    const SDL_VideoInfo* info;
    int test_alg = 0;

    /* i18n */
#ifdef ENABLE_NLS
    setlocale (LC_ALL, "");
    bindtextdomain (PACKAGE, LOCALEDIR);
    textdomain (PACKAGE);
#endif
    
    /* ltris info */
    printf( "LTris %s\nCopyright 2002-2024 Michael Speck\nPublished under GNU GPL\n---\n", VERSION );
    printf( "Looking up data in: %s\n", SRC_DIR );
#ifndef SOUND
    printf( "Compiled without sound and music\n" );
#endif

    set_random_seed(); /* set random seed */

    /* game ids - not translated to be fixed independant of language */
    strcpy(gametype_ids[0],"demo");
    strcpy(gametype_ids[1],"classic");
    strcpy(gametype_ids[2],"figures");
    strcpy(gametype_ids[3],"vshuman");
    strcpy(gametype_ids[4],"vscpu");
    strcpy(gametype_ids[5],"vshumanhuman");
    strcpy(gametype_ids[6],"vshumancpu");
    strcpy(gametype_ids[7],"vscpucpu");
    strcpy(gametype_ids[8],"training");
    /* game names - translated for display */
    strcpy(gametype_names[0],_("Demo"));
    strcpy(gametype_names[1],_("Normal"));
    strcpy(gametype_names[2],_("Figures"));
    strcpy(gametype_names[3],_("Vs Human"));
    strcpy(gametype_names[4],_("Vs CPU"));
    strcpy(gametype_names[5],_("Vs Human&Human"));
    strcpy(gametype_names[6],_("Vs Human&CPU"));
    strcpy(gametype_names[7],_("Vs CPU&CPU"));
    strcpy(gametype_names[8],_("Training"));
    config_load();

#ifndef LGAMES_PORT
    while ( ( c = getopt( argc, argv, "Ttfwr:" ) ) != -1 )
      {
        switch (c)
  	{
  	case 'f': config.fullscreen=1; break;
  	case 'w': config.fullscreen=0; break;
  	case 'r':
  		sscanf(optarg, "%dx%d", &video_forced_w, &video_forced_h);
  		printf("Trying to force resolution %dx%d\n",
  				video_forced_w, video_forced_h);
  		break;
  	case 't':
  		test_alg = 1; /* test CPU algorithm in console, single run */
  		break;
  	case 'T':
  		test_alg = 2; /* test CPU algorithm in console, test range */
  		break;
  	}
      }
#else
    (void)argc;
    (void)argv;
    (void)c;
#endif

    init_sdl(SDL_INIT_EVERYTHING);
    gamepad_open();
    if (gamepad_opened()) {
	    printf("  NOTE: Gamepad cannot be configured via menu yet but you\n");
	    printf("  can edit the gp_ entries in config file %s/%s.\n",
			    	    config.dir_name, CONFIG_FILE_NAME );
	    printf("  In case connection to gamepad gets lost, you can press F5 do reconnect.\n");
    }

    info = SDL_GetVideoInfo();
    printf("Display resolution: %d x %d\n", info->current_w, info->current_h);
    display_w = info->current_w;
    display_h = info->current_h;
    if (video_forced_h > 0) {
  	  if (video_forced_w > display_w || video_forced_h > display_h) {
  		  printf("Forced resolution out of bounds, ignoring it\n");
  		  video_forced_h = video_forced_w = 0;
  	  }
  	  if (video_forced_w < 640 || video_forced_h < 480) {
  		  video_forced_w = 0;
  		  video_forced_h = 0;
  	  }
    }

    set_video_mode( config.fullscreen );

    SDL_WM_SetCaption( "LTris", 0 );
    sdl.fade = config.fade;
    SDL_SetEventFilter( event_filter );
#ifdef SOUND
    audio_open();
    sound_enable( config.sound );
    sound_volume( config.volume * 16 );
#endif

    /* create */
    hint_load_res();
    manager_create();    
    tetris_create();
    chart_load();
    /* run game */
    manager_fade( FADE_IN );
    while( !leave && !term_game ) {

	    /* test algorithm on console? */
	    if (test_alg) {
		    tetris_init();
		    tetris_test_cpu_algorithm(test_alg);
		    tetris_clear();
		    break;
	    }

        result = manager_run();
        switch( result ) {
            case ACTION_QUIT:
        	    leave = 1;
        	    break;
            case ACTION_PLAY:
#ifndef LGAMES_PORT
                manager_fade( FADE_OUT );
#endif
                /* LGames: keep the last presented menu while constructing
                 * the game in SDL surfaces; tetris_run presents it when ready.
                 * Do not allocate/copy two full-screen fade buffers on entry. */
                if ( tetris_init() ) {
                    tetris_run();
                    tetris_clear();
                }
                manager_fade( FADE_IN );
                break;
            default: break;
        }
    }
    manager_fade( FADE_OUT );
    /* delete stuff */
    tetris_delete();
    manager_delete();
    chart_save();
    chart_delete();
    hint_delete_res();
    
    gamepad_close();
#ifdef SOUND
    audio_close();
#endif
    config_save();

    return EXIT_SUCCESS;
}
