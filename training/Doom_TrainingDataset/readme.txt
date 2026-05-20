1. All the command line args supported by doom are published on wiki page.
https://freezing-vinyl-b94.notion.site/3634854f6244805180faf7191fdc344a?v=3634854f624480d3b0ae000cbe90f6b4&pvs=143

-------------------------------------------------------------------------------------------------------------------------------

2. Each folder have readme. You can simply put your command line arg and file name. 
(That is sufficient to track the difficulty, level, and map.

-------------------------------------------------------------------------------------------------------------------------------

3. Sample cmd to play doom.
src/chocolate-doom \
-iwad ../Doom_STM32N6570_DK/wad/DOOM1.WAD \
-window-3 \
-warp11 \
-skill1 \
-nosfx-nomusic \
-record bd_run1

Overview: 
-iwad ../Doom_STM32N6570_DK/wad/DOOM1.WAD \	: Plays the IWAD Game file for GUI
-window-3					: Size of window - 3				
-warp11 					: Starts Doom at Level 1 Map 1
-skill1 					: Difficulty Level - Basic (1) (Increase the number to increase the difficulty)
-nosfx-nomusic 					: No sound effects , no music		
-record bd_run1 				: Record the Doom Game Play -  bd_run1 (Name of file)

-------------------------------------------------------------------------------------------------------------------------------

4. Benchmark Demo :
src/chocolate-doom \
-iwad ../Doom_STM32N6570_DK/wad/DOOM1.WAD \
-timedemo bd_run1


Overview: 
-record bd_run1 : Record the Doom Game Play -  bd_run1 (Name of file)

-------------------------------------------------------------------------------------------------------------------------------