# Alienorum

A cross-platform desktop planetarium app made using the ImGui library. https://github.com/ocornut/imgui

<img width="260" height="144" alt="image" src="https://github.com/user-attachments/assets/7a42c429-6c14-4b70-8095-4d48eb478d94" /> <img width="260" height="144" alt="image" src="https://github.com/user-attachments/assets/33745731-b6e0-4ed2-adfa-7b9b3173fb23" /> <img width="260" height="144" alt="image" src="https://github.com/user-attachments/assets/285dea8d-8609-41a0-b741-53ddf8a7471e" />
<img width="260" height="144" alt="image" src="https://github.com/user-attachments/assets/7ca4673d-8507-4b97-91e4-a9be9ad7f827" /> <img width="260" height="144" alt="image" src="https://github.com/user-attachments/assets/296ef493-0c65-4654-84ea-e5db9e327a24" /> <img width="260" height="144" alt="image" src="https://github.com/user-attachments/assets/8278e35e-6429-4bd8-bda4-73361b642d30" />
<img width="260" height="144" alt="image" src="https://github.com/user-attachments/assets/c6efd128-458d-4cf7-93f2-38e714e89c71" /> <img width="260" height="144" alt="image" src="https://github.com/user-attachments/assets/769edb76-9810-4fa3-98ac-f47991bdcddb" /> <img width="260" height="144" alt="image" src="https://github.com/user-attachments/assets/3b464155-d74d-496e-8d71-5f76d0fd4414" />

The stars are alien suns.


## Dependencies

Alienorum can be built and run on Linux, Mac, and Windows. Before building, please make sure to install
all the dependency packages:

Linux:
   apt-get install -y libsdl2-dev libsdl2-image-dev libjpeg-dev libpng-dev build-essential libcurl4-openssl-dev

Mac OS:
   brew install sdl2 sdl2_image jpeg png

MSYS2 (Run in MINGW64 environment):
   pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-libjpeg-turbo mingw-w64-x86_64-libpng mingw-w64-x86_64-curl

Additionally, for those running Windows, since the star catalogs all download as compressed .gz files and Windows
has incomplete support for gzip, Alienorum will require 7zip (https://www.7-zip.org/download.html) to be installed.


## Features

* OpenGL rendering;
* Sky atlas with pan and zoom, RA/dec grid, and constellation lines;
* Automatic downloading and use of professional star catalogs;
* Spaceflight feature with time dilation and warp speed capability;
* View space from any vantage point;
* Time travel (in-universe only) - set the view for any date by stepping forward/backward;
* Information shown when hovering over a celestial object, including names, coordinates, distance, and magnitude;
* Realtime satellite positions;
* Exoplanets;
* Ability to select an individual star/planet/moon/satellite and teleport to its position;
* Universe saved to portable, customizable JSON file, allowing defining your own planets/stars;
* Custom texture map generation;
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


## View Modes

### Horizon

Horizon mode allows seeing the sky and horizon from the surface of any planet, moon, or asteroid. This mode can be
accessed by selecting it from the dropdown in the status window, pressing `_` (underscore), or "crashing" into an
object during spaceflight. (Crashing into a star will not have the same effect.)

While in horizon mode, there are controls to edit the current latitude and longitude. These will be saved to your
`user.json` file if you change the theme in the themes dropdown. Next to the latitude button is an `...` button
that produces a list of locales if any locales are known for your current location. On Earth this includes cities
and a handful of observatories.

You can also navigate the surface by holding down `End` (to go forward) or `Home` (to
go backward) at a brisk walking speed. If you desire to go faster, simultaneously holding down `Shift` increases
your speed tenfold and `Ctrl` one hundredfold. Both `Shift` keys and both `Ctrl` keys may be combined to produce
a total of one million times the default walking speed.


### Sun Clock

Sun clock mode shows the entire surface of the planet or moon as a cylindrical projection lit up in areas that are
in daylight. It is pannable and zoomable. Resolution is limited for performance reasons, but you can zoom in much
closer than the resolution of most texture maps.


## Satellites

To add a satellite, press ^ (Shift+6 on US keyboards). A searchable list will appear; you can search by satellite
name, e.g. ISS or HST, or by category, e.g. gps-ops or iridium-next. The search is not case sensitive. Once a
satellite has been loaded, it will be selected and, if having clicked the first button, the satellite will be
centered in the view. Pressing O takes you to the satellite, where the orbit center (usually Earth) appears at the
nadir of the view. The resulting effect is if you choose an LEO (low Earth orbit) satellite and O to it, you will
see the Earth along the bottom of the view as if you were up in the satellite positioned so that Earth is "down".
LEO views make great background views to leave open; they slowly change over time and you can enable realism mode
by pressing ! (Shift+1 on US keyboards) and close all dialog windows for maximum immersion.

Alienorum auto-downloads satellite data from CelesTrak (https://celestrak.org/). Automatic download is set to only
happen at most once per day. You can override this by opening `catalogs/sat/sources.json` and changing one of the
"LastAccessed" days to a date in the past like "2000-01-01 00:00:00", then deleting whichever local .csv file that
JSON entry is for. The next time you run Alienorum it will re-fetch the file. CAUTION: CelesTrak's data only update
every two hours, and the site admin has limited bandwidth. Fetching the same file(s) too frequently may result in
CelesTrak blocking your IP. This can be avoided by letting Alienorum manage the downloads.


## Editing and Saving Objects

To edit a celestial object, select it and press Shift+E. An edit window will appear at lower right, offering several
object properties that can be set in-program. All changes will take effect right away.

To add a new object, first select its center of orbit and then press Shift+A. A dialog will ask what kind of object
to create. Currently, only Star, Planet, and Moon will work. After clicking Go, the new object will be added with a
few default parameters and you will see an edit window to fill in all its other properties.

Alienorum generates fictitious texture maps of objects that don't have a map file in the `maps/` folder. While
editing, you can save, refresh, and regenerate these fictitious maps. Save creates file with the object's name in
`maps/`, and refresh reloads the existing file or creates a new texture if the file is absent. The regenerate
button creates a high resolution texture, never the same as the existing texture, but suitable for world building
(10000 x 5000 pixels). This will take a moment to complete so be sure to let it finish before saving or retrying.

To export your objects you've created or modified, press U. To load them again next session, first rename your 
`universe.json` file to something else, let's say `my_universe.json`, then the next time you run Alienorum, run it
from the command line with the `load` argument like this: `./bin/alienorum load my_universe.json`. Note Alienorum
will not touch your custom file; any changes made will be written to `universe.json` only, so make sure to either
copy them to your custom universe file or rename the default `universe.json` to your filename.

Any object properties not included in the edit window can be edited in the .json file.

You can also edit the universe JSON files to modify other parameters not included in the window. Just make sure the
file is still valid JSON after any edits. There are third party apps that will check a .json file to make sure it's
valid and find any errors.


## Setting Your Latitude/Longitude

The default lat/lon coordinates are those for Babylon, where archaeological evidence exists for astronomical knowledge
in ancient times. To set your own location as the default, create a file in the alienorum root folder called `user.json`
and add the following lines, changing the numbers to your own location:

```
{
    "Latitude": 45.52,
    "Longitude": -122.68
}
```


## Keyboard Shortcuts

The current full list of keyboard shortcuts is:


### View Controls

```
B           Increase brightness
Shift+B     Decrease brightness
`           Increase gamma
~           Decrease gamma
*           Zoom in
/           Zoom out
{scroll}    Zoom
%           Reset default brightness and zoom
Shift+R     Toggle red light mode
Q           Increase texture rendering quality. WARNING: Use cautiously; the value can get too high quickly and render the app unusable!
Shift+Q     Improve performance by decreasing texture rendering quality
F11         Toggle fullscreen
&           Sky atlas mode
_           Horizon mode
$           Sun clock mode
\           Sky map mode
Shift+J     Toggle upside-down mode when viewing from satellites
3           Cycle forward through themes
Shift+#     Cycle backward through themes
```


## Info and Tracking

```
{click}     Select object
Shift+S     Clear selection
T           Track selected object / cease tracking and select
Shift+T     Clear tracking
```


### Image Elements

```
C           Show/hide constellation lines (only within 10 l.y. of Sun)
G           Show/hide RA/Dec lines
L           Show/hide labels
A           Label brightest stars
V           Label intrinsically bright stars
Shift+N     Label nearby stars
F           Label Flamsteed stars
Shift+F     Label Bayer stars
Shift+G     Label Gould (Uranometria) stars
Shift+C     Label Sunlike stars
Shift+P     Label stars with planets
Shift+L     Label stars with planets in habitable zone
Shift+X     Label stars with known poles
P           Label local system objects
Shift+O     Show/hide orbits
J           Show/hide satellites
!           Hide all annotations (realism mode)
1           Bring back default image elements
,           Hide mouse cursor until next mouse move (e.g. for taking screenshots)
```


### Dialogs
```
N           Show/hide info panel
S           Show/hide status panel
E           Show/hide system explorer
0           Show/hide stellar neighborhood
```


### Motion and Location

```
O           Go to object (selected or tracked)
R           Return to default view, from Earth, at current time
W           Warp speed
X           Full stop
+           Increase speed
-           Decrease speed (no effect if already stopped)
{arrows}    Steering
{Home}      Accelerate backward
{End}       Accelerate forward
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
^           Add satellite from downloaded lists
.           Add asteroid/minor planet from astorb
Shift+E     Edit current object
U           Export user-added and user-modified objects to universe.json
Shift+U     Save current user settings (theme, home lat/lon)
F3          Search
F4          Load user objects from an external JSON file

IMPORTANT: After loading a universe with F4, subsequently saving your changes with U will not overwrite the
external file; it will save to universe.json. Make sure to either copy your changes to the external JSON file
or back up your old file and rename universe.json to the working filename. It's also a good idea to check
universe.json to make sure it has all the custom celestial objects.


## Troubleshooting

Some of Alienorum's status messages and error mesages are output to the terminal (the command line), so it is
recommended to run the app in a command prompt if you notice any unexpected behavior.

If a JSON file fails to load, you can use any third party JSON syntax checker to find and fix whatever might
be wrong with it. JSON files can be edited in any text editor.

If you see a message in your terminal that reads `Bump map must have same resolution as surface map.`, check
and see if you have both the normal `maps/Moon_surf.png` map and the full sized `maps/Moon_surf.jpg` map. If
so, then it's safe to delete the .jpg file. Alternatively, if you wish to keep the higher resolution, you can
delete the .png instead (it's just a scaled down version of the .jpg) and use your favorite image editor to
resample the `maps/Moon_bump.jpg` map up to 2048x1024 resolution.

