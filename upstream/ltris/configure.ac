AC_PREREQ([2.71])
AC_INIT([ltris],[1.3.2])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

AC_USE_SYSTEM_EXTENSIONS
AM_GNU_GETTEXT([external])

# Checks for programs.
AC_PROG_CC
AC_PROG_RANLIB

# Checks for libraries.
AM_PATH_SDL(1.0.0,, AC_MSG_ERROR(lib SDL is needed))

CFLAGS="$CFLAGS -Wall -Wno-int-conversion -Wno-format -Wno-implicit-function-declaration"

dnl Check Zarus install
AC_ARG_ENABLE( zaurus,
[  --enable-zaurus         Compile for Zaurus], CFLAGS="$CFLAGS -DZAURUS")
AC_ARG_ENABLE( softfloat,
[  --enable-softfloat      Use Softfloat for CPUs without FPU], CFLAGS="$CFLAGS -msoft-float"; LIBS="$LIBS $float")

if test "x$host" = "xi686-w64-mingw32"; then

	# set CFLAGS and LIBS directly for windows port as we need to e.g. 
	# skip -mwindows flag
	
	CFLAGS="-DWIN32 -I/usr/i686-w64-mingw32/sys-root/mingw/include/SDL -D_GNU_SOURCE=1 -Dmain=SDL_main"
	LIBS="-L/usr/i686-w64-mingw32/sys-root/mingw/lib -lmingw32 -lSDLmain -lSDL -mwindows -lintl"

else

	CFLAGS="$CFLAGS $($SDL_CONFIG --cflags)"
	LIBS="$LIBS $($SDL_CONFIG --libs)"

fi

dnl debugging
AC_ARG_ENABLE( debug,
[  --enable-debug       Add debug symbols, no optimization.], 
CFLAGS="$CFLAGS -O0 -g")

dnl check SDL_mixer
AC_CHECK_LIB(SDL_mixer, main,
CFLAGS="$CFLAGS -DSOUND";LIBS="$LIBS -lSDL_mixer", 
AC_MSG_RESULT(SDL_Mixer NOT found: Audio disabled))

dnl set sound buffer size
buf_flag="-DAUDIO_BUFFER_SIZE=256"
AC_ARG_WITH( buffer-size,
[  --with-buffer-size=BUFSIZE       Set this value to 2048 if you encounter sound delay.],
buf_flag="-DAUDIO_BUFFER_SIZE=$withval",)
AC_SUBST(buf_flag)

dnl allow changing config dir
configdir=".lgames"
AC_ARG_WITH( configdir,
[  --with-configdir=CONFIGDIR       Set directory for config file in home directory (default: .lgames).],
configdir="$withval",)
CFLAGS="$CFLAGS -DCONFIG_DIR_NAME=\\\"$configdir\\\""

dnl set localedir
localedir=$datadir/locale
locdir_flag="-DLOCALEDIR=\\\"$localedir\\\""

dnl installation path
inst_dir=$datadir/ltris
hi_dir=$localstatedir

dnl check if installation was disabled
dis_flag=""
AC_ARG_ENABLE( install,
[  --disable-install           No installation. Played from the source directory.], 
inst_dir="." hi_dir="." dis_flag="-DDISABLE_INSTALL"
locdir_flag="-DLOCALEDIR=\\\"../dict\\\"" 
    rm -rf dict; mkdir dict
    for lang in `cat po/LINGUAS`; do
        mkdir dict/$lang; mkdir dict/$lang/LC_MESSAGES
        ln -s ../../../po/$lang.gmo dict/$lang/LC_MESSAGES/$PACKAGE.mo
    done
)

inst_flag="-DSRC_DIR=\\\"$inst_dir\\\""
hi_inst_flag="-DHI_DIR=\\\"$hi_dir\\\""

AC_SUBST(inst_dir)
AC_SUBST(hi_dir)
AC_SUBST(inst_flag)
AC_SUBST(hi_inst_flag)
AC_SUBST(dis_flag)
AC_SUBST(locdir_flag)

AC_CONFIG_FILES([Makefile src/Makefile icons/Makefile src/sounds/Makefile src/gfx/Makefile po/Makefile.in m4/Makefile ])
AC_OUTPUT
