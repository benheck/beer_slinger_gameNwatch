# beer_slinger_gameNwatch
An LCD-segment accurate recreation of "Tapper" for the new STM32-based Nintendo Game &amp; Watch

"Beer Slinger" attempts to follow most "LCD rules" except it likely exceeds the segment limit. It uses the multiple layers of the video driver to create semi-transparency and acheive an authentic, crappy retro-reflective LCD look.<br>
The black LCD segments are drawn on the top layer (and this is the layer that is buffered during play) the background graphics are drawn on the bottom layer, and the base solid background color is gray. I used the True RNG in the ARM to sprinkle specular highlights on the background to make it LCD-like.

## Setup Instructions

- Setup STM32CubeIDE for use with Nintendo Game & Watch (stack smashing and many others more talented than I have instrutions on how to do this).
- Setup this project folder structure in the code.
- Use an STM debugger and the pins on the G&W to program (I used the snap-off top portion of an STM Nucleo board)

## GAME CONTROLS 

### DURING ATTRACT MODE:
Tap "GAME" - selects between Game A and B, and music on/off. Game B is a harder version of Game A.<br>
Hold "TIME" and press left/right - selects starting level, max 20.<br>
Hold "PAUSE" and press left/right - adjust volume between 0 and 100. 100 is pretty loud and will eat your battery quickly.<br>
Tap "A" or "B" - start game at currently selected level.<br>

### DURING PLAY:
Up/down - move the barkeep between rows of tables. If you are dashing for tips and press up/down, barkeep instantly moves back to end of bar.<br>
Left/right - dash to pick up tips. If you collect 5 tips you can spawn dancing girls (press "B") to stop patrons from moving for 4 seconds.<br>
"A" - hold to pour beer, release to sling beer. If you start a pour on an empty row, move barkeep up or down to cancel the pour.<br>
"B" - Spawns distracting dancing girls if you collect 5 tips. This removes 5 tips so it counts against your end of level tip bonus. If you collect 5 tips, don't spawn the girls, then collect 5 more tips, you still only get 1 girl spawn. You must use the girl spawn to earn another one.<br>

## POWER ON/OFF:

The gray power button on the right-hand side puts the unit into sleep mode and wakes it up. The CPU is still running in ultra-low power mode, so it may eventually drain the battery and require recharging.

## GENERAL STRATEGY:

When the bar isn't busy/early on in levels, collect at least 5 tips so you can spawn the girls when it gets busy later on.<br>
Game B increases how many customers drink 2 beers, and increases chance of them leaving no tip.<br>
You can catch returning mugs while you dash for tips so it's a good idea to combine those activities.<br>
There is code to prevent 2 customers from slinging back mugs at the same time, since that's pretty unfair. Other than that, it's very rogue-like and will keep getting harder until you fail.<br>

Have fun, and don't overserve anyone!
