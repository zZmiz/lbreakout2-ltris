# Use ls | grep -v Makefile | sed -e "s/$/ \\\\/" for update
EXTRA_DIST = 1010011010 \
Abc \
Afl \
Africa \
Alex \
Alexandre \
AllNighter \
AllWorld \
America \
Arcade \
Arkanoid \
Asia \
Astra \
Astro \
Belief \
BeOS-4ever \
Bertrand \
BigBang \
Biolab428 \
Blues \
Bombs \
CalendarFun \
Celine \
Chaos \
Choices \
Circle \
Classique \
Color \
Composers \
Cool \
Crossing \
D2K \
Daves \
Demons \
Europe \
Family \
Fantaisie \
FatalDoomsday \
FatalDoomsday2 \
Flower \
Fly \
Football \
Freek \
Frozen \
FunCity \
Gatinha \
Germany \
GreenWall \
GreenWall2 \
GreenWall3 \
GreenWall4 \
Hans \
HartzIV \
HereWeGo \
Heroes \
Hieroglyphs \
HighBall \
Holidays \
Hommage \
Horror \
Impossibility \
Invisible \
IoriYagami \
Iris \
IrrenhausAh \
Izusiowe \
JediAdventure \
JustForFun \
Kazan-1 \
Kevin \
KungFu \
Lapis \
Lattices \
Lattsville \
LBreakout1 \
LBreakout2 \
LinuxFun \
Lord \
Madness \
Maze \
Megadoomer \
MegaMan2 \
Memory \
MontyHall \
N_Arenas \
Nations \
Negundoid \
NES \
NewBricks \
NewOERG1 \
N_Snapshots \
ObiWan \
Oceania \
OpenSource \
OurWorld \
Pabelo \
Patience \
Pawset \
PCGames \
Ph33r \
P.I.T.A. \
PlayAgain \
Popcorn \
RichQuick \
Runes \
R-World \
Sadistic \
Shimitar \
Silly \
SkillShot \
Smilies \
SnyMaxx \
StarWars \
Symmetry \
Symmetry-MKII \
T.C.S. \
TheEnd \
TheGauntlet \
TimeWaster \
TrickShot \
Tricky \
Twilight \
Vader \
WallBricks \
Weirdness \
Wizi \
Wolvie \
WonderWalls \
WTF \
X \
Yexter \
Yoda \
Zigies \
Zooligan \
Zufallswelt

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(inst_dir)/levels
	@for file in $(EXTRA_DIST); do \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(inst_dir)/levels/$$file; \
	done

win32-install-script:
	@for file in $(EXTRA_DIST); do \
	  echo "Source: \"`pwd -W`\\$$file\"; DestDir: \"{app}\\levels\"; CopyMode: alwaysoverwrite" >> @win32_inst_script@; \
	done

