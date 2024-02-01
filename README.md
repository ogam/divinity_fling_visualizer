## Divinity Fling Visualizer
Ideally used for Larian games to figure out where you place your nodes in the world.
Currently only supports BG3 since that's the most recent game that doesn't include offical mod support tools.

## Why a random tool?
Currently all interactive maps have nice images and good references of where items and npcs are in the map but they aren't spatially correct.  
To do item flings we need to know how world coordinates from 1 spot in a level will look like in another level. Or to get a better understanding visually where we currently are setting up our next fling.  
Also this gives you the option to scan your current screen to see all possible areas you can fling to within your game screen region so you can see where your fling position gets snapped to the world.  

## Build
Currently only setup with MSVC, run build.bat and you should be all set.  
If it complains about not finding vcvars.bat then add your MSVC path to the list in build.bat.  

## Libraries
Uses Minicoro (https://github.com/edubart/minicoro)
Raylib (https://github.com/raysan5/raylib)
Dear Imgui (https://github.com/ocornut/imgui)
