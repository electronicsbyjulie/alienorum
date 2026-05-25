# Alienorum

A cross-platform desktop planetarium app made using the ImGui library. https://github.com/ocornut/imgui

<img width="1364" height="880" alt="image" src="https://github.com/user-attachments/assets/5262307d-ba07-440e-b709-d32afd47d725" />

The stars are alien suns.

## Features

* OpenGL rendering;
* Sky atlas with pan and zoom, RA/dec grid, and constellation lines;
* Automatic downloading and use of professional star catalogs;
* Spaceflight feature with time dilation and warp speed capability;
* View space from any vantage point;
* Time travel (in-universe only) - set the view for any date by stepping forward/backward;
* Information shown when hovering over a celestial object, including names, coordinates, distance, and magnitude;
* Ability to select an individual star and teleport to its position;
* Universe saved to portable, customizable JSON file, allowing defining your own planets/stars;
* More coming soon...

## Initial Run

The first time you run Alienorum, you will see a splash screen featuring our alien mascot (maybe the alien
should have a name?) and a dynamic loading message. The application will begin by downloading star catalogs;
if you run it in a terminal you'll be able to see the download process. This might take a while, but it's a
one-time thing.

After downloading finishes, the application will load a selection of stars from the catalogs. When displaying
stars and during spaceflight, Alienorum dynamically hides stars that are too far away or too dim, that way it
can load hundreds of thousands of stars and still smoothly animate views of space.

## Spaceflight

To begin a spaceflight, point the view in the direction you wish to go, and press the + (plus sign) key.
You can monitor your speed using the status pane at left. Most likely you will want to zoom past the stars,
so just hold the + key down until you get very very close to the speed of light. Notice the clock in the
status pane speeds up.

As you're speeding up, you can use the mouse to have a look around. Once you press the + key the first time,
it sets your direction of travel and any further use of + or - will affect only your speed and not your direction.
You can see the Sun and inner planets zooming away behind you as you keep accelerating.

Notice that due to time dilation, it takes a long time to get up to interstellar speeds. A really really long
time! You would be forgiven for wondering how the developer of this app can be so cruel, but it's relativity
that's this cruel. The universe is really really really big. Even worse, relativity says the closer you get to
the cosmic speed limit, the more energy you have to dump into your engines to keep accelerating.

At least there's unlimited in-app spaceflight fuel. If you have some way to hold down that + key, the simulation
will eventually reach interstellar speed. But we're not going to make you go through all that trouble - we've
added a cheat code.

## Warp Speed

Point the view in the direction you want to go and press W. Notice that you are now traveling at slightly over
warp 1, and that the clock is keeping normal time. Press + several more times until you see the stars begin to
move. Warp speed is a frequent sci-fi trope to get around the limitations of relativity, often involving some
kind of astrophysics shortcut that bypasses actually moving through normal space. For our purposes, we can just
ignore relativity and show what spaceflight and flyby sequences would look like without it.

Even with the help of warp speed, it still takes getting up to a few million to a few tens of millions of times
the speed of light just to see the stars rush by. Space is really really really really REALLY big.

Sublight speed can be useful for moving between nearby solar system objects, while warp is useful for longer 
trips up to and including interstellar flight.

To stop spaceflight, press X.

## Hovering, Selecting, Tracking

Hovering the mouse over a celestial object displays information about that object in the right hand pane.
Clicking will select the object, resulting in a green circle around it. To clear object selection, press Shift+S.

After slecting an object, pressing O will teleport to that object. The local reference plane will update in the 
view, so you might be looking in a different direction than before.

You can also use the search box in the left pane to find objects. You can search by friendly name (e.g. Polaris),
Bayer-Flamsteed name (Alp UMi or 1 UMi), HD designation, HIP designation, or Gliese number. It uses a fuzzy 
search algorithm that sometimes gets caught on names with similar letters - if you search Proxima it returns
Porrima (Gam Vir), but "proxima cent" finds the right star. After clicking Find or pressing Enter, the search 
result will be selected.

To track an object, first select it then press T. The view will remain centered on the object, its info will
remain in the right pane, and it will not be possible to hover over any other object for info. To stop tracking,
press Shift+T.

## Flyby

One trick that can make for an impressive display is to do a flyby. Start by finding the object of interest,
and use the mouse to point the view just off center of the object. (Dragging with the left button gives coarse
panning; with the right button, fine panning; and with the middle button, ultrafine panning). Press + or W, then
select the object of interest and track it with T.

Then speed up to approach the object, watching its distance in the right pane. Try not to come in too fast; as
long as you are tracking the object, the app will automatically slow your approach as you get closer to the
target. Otherwise it's very easy to overshoot and zip right past it. If your speed is just right, you can float
by the target and watch it seem to roll across the background stars.

## Editing and Saving Objects

To edit a celestial object, select it and press Shift+E. An edit window will appear at lower right, offering several
object properties that can be set in-program. All changes will take effect right away.

To add a new object, first select its center of orbit and then press Shift+A. A dialog will ask what kind of object
to create. Currently, only Star, Planet, and Moon will work. After clicking Go, the new object will be added with a
few default parameters and you will see an edit window to fill in all its other properties.

To export your objects you've created or modified, press U. To load them again next session, first rename your 
`universe.json` file to something else, let's say `my_universe.json`, then the next time you run Alienorum, run it
from the command line with the `load` argument like this: `./bin/alienorum load my_universe.json`. Note Alienorum
will not touch your custom file; any changes made will be written to `universe.json` only, so make sure to either
copy them to your custom universe file or rename the default `universe.json` to your filename.

You can also edit the universe JSON files to modify other parameters not included in the window. Just make sure the
file is still valid JSON after any edits. There are third party apps that will check a JSON file to make sure it's
valid and find any errors.

## Keyboard Shortcuts

The current full list of keyboard shortcuts is:

### View Controls

```
B           Increase brightness
Shift+B     Decrease brightness
`           Increase gamma
~           Decrease gamma
{scroll}    Zoom
%           Reset default brightness and zoom
Shift+R     Toggle red light mode
```

## Info and Tracking

```
{click}     Select object
Shift+S     Clear selection
T           Track selected object
Shift+T     Clear tracking
```

### Image Elements

```
C           Show/hide constellation lines (only within 10 l.y. of Sun)
G           Show/hide RA/Dec lines
L           Show/hide labels
Shift+O     Show/hide orbits
N           Show/hide info panel
S           Show/hide status panel
!           Hide all annotations (realism mode)
```

### Motion and Location

```
O           Go to object
R           Return to default view, from Earth, at current time
W           Warp speed
X           Full stop
+           Increase speed
-           Decrease speed (no effect if already stopped)
```

### Time Seeking

```
Z           Advance one century
Shift+Z     Rewind one century
Y           Advance one year
Shift+Y     Rewind one year
M           Advance one month
Shift+M     Rewind one month
D           Advance one day
Shift+D     Rewind one day
H           Advance one hour
Shift+H     Rewind one hour
I           Advance one minute
Shift+I     Rewind one minute
@           Return to present moment
```

### Misc.

Shift+A     Add new object in orbit around current object
Shift+E     Edit current object
U           Export user-added and user-modified objects to universe.dat
