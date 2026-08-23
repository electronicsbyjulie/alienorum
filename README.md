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
   apt-get install -y libsdl2-dev libsdl2-image-dev libjpeg-dev libpng-dev build-essential libcurl4-openssl-dev libarchive-dev

Mac OS:
   brew install sdl2 sdl2_image jpeg png archive

MSYS2 (Run in MINGW64 environment):
   pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-libjpeg-turbo mingw-w64-x86_64-libpng mingw-w64-x86_64-curl mingw-w64-x86_64-libarchive


## Features

* OpenGL rendering;
* Spaceship mode with pan and zoom, RA/dec grid, and constellation lines;
* Automatic downloading and use of professional star catalogs;
* Spaceflight feature with time dilation and warp speed capability;
* View space from any vantage point;
* Time travel (in-universe only) - set the view for any date by stepping forward/backward;
* Information shown when hovering over a celestial object, including names, coordinates, distance, and magnitude;
* Asteroids and comets;
* Realtime satellite positions;
* Exoplanets including fictional texture maps, atmospheres, and rings;
* Customizable texture generation for fictional world building;
* Ability to select an individual star/planet/moon/satellite and teleport to its position;
* Stellar proper motions, orbits, and variability;
* Unified star catalog with an internal star designation system based on magnitude and color;
* Universe saved to portable, customizable JSON file, allowing defining your own planets/stars;
* Custom texture map generation;
* More coming soon...

Internal AlienorumIDs for stars dimmer than magnitude 8 should be considered provisional and subject to change.

Unofficial proposed names are included for the four planets in the 82 Eridani system. These names are not in use
by any authority at the time of this writing. The names were invented here to avoid confusion between two
different lettering schemes for the planets in this system.


## Initial Run

The first time you run Alienorum, you will see a splash screen featuring our alien mascot (maybe the mascot
should have a name, anyone?) and a dynamic loading message. The application will begin by downloading star
catalogs and other celestial object data. This might take a while, but it's a one-time thing.

After downloading finishes, the application will load a selection of stars from the catalogs. When displaying
stars and during spaceflight, Alienorum dynamically hides stars that are too far away or too dim, that way it
can load hundreds of thousands of stars and still smoothly animate views of space.


## Menu

To access the menu system, press F2. Every menu command has a keyboard shortcut. For certain functions such as
spaceflight, you will definitely want to memorize the keyboard shortcuts since accessing everything through a
menu is by nature not fast enough to effectively control the app while zooming past planets.


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

### Spaceship

The default mode. Simulates a view from space as if looking out from inside a spaceship. If located at a star,
planet, moon, etc., it will look as if you were at the center of the object with the object itself invisible.


### Planetfall

Planetfall mode allows seeing the sky and horizon from the surface of any planet, moon, or asteroid. This mode can be
accessed by selecting it from the dropdown in the status window, pressing `_` (underscore), or "crashing" into an
object during spaceflight. (Crashing into a star will not have the same effect.)

While in planetfall mode, there are controls to edit the current latitude and longitude. These will be saved to your
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


### Sky Map

A pannable, zoomable atlas of the full sky and all objects in it, arranged by right ascension (X axis) and
declination (Y axis).


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

IMPORTANT: After loading a universe with F4, subsequently saving your changes with U will not overwrite the
external file; it will save to universe.json. Make sure to either copy your changes to the external JSON file
or back up your old file and rename universe.json to the working filename. It's also a good idea to check
universe.json to make sure it has all the custom celestial objects.


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


## Troubleshooting

Some of Alienorum's status messages and error mesages are output to the terminal (the command line), so it is
recommended to run the app in a command prompt if you notice any unexpected behavior. Note the Windows desktop app
from the installer exe does run in a terminal every time, so you will always be able to see any error messages in
that case. Some of the errors are normal because our source data are incomplete; if the stars and constellations
and stuff look right on your screen, then the app is working.

If a JSON file fails to load, you can use any third party JSON syntax checker to find and fix whatever might
be wrong with it. JSON files can be edited in any text editor.

If you see a message in your terminal that reads `Bump map must have same resolution as surface map.`, check
and see if you have both the normal `maps/Moon_surf.png` map and the full sized `maps/Moon_surf.jpg` map. If
so, then it's safe to delete the .jpg file. Alternatively, if you wish to keep the higher resolution, you can
delete the .png instead (it's just a scaled down version of the .jpg) and use your favorite image editor to
resample the `maps/Moon_bump.jpg` map up to 2048x1024 resolution.


## For Developers

### Unit Tests

Unit tests are in place; to run them, run `make tests`. The "all" target deliberately does not make `tests`
but does build the unit tests' binary executables.

Every new PR will have to pass all unit tests before merge. Recommended but not required: when adding a
feature, it is a good idea to also add unit tests for it. The repo owner will check for testing gaps from time
to time and fix any gaps found.

### Version Numbering

The current version can be found in vcpkg.json. This is the version for the Windows installer (see below) as
well as any other releases that may be added in the future.

The repo owner will try to release a new version every week, typically around Sunday night MST.

Version numbering shall be incremented as follows:
- Every new release that does not increment the major or minor increments the patch number by one;
- Every new release that introduces a significant new feature, or integrates with a new third party module,
   increments the minor by one and resets the patch to zero.
- Every new release that breaks compatibility with any third party code or app or library, or changes the format
   or behavior of a command line argument, or changes an existing keyboard shortcut, or changes a top level menu
   item, shall increment the major by one and reset the minor and patch to zero.

Every attempt should be made to avoid breaking compatibility since we don't want the version number to skyrocket
into the double digits in just a few years without massive enhancements to the UX.

What counts as a feature? Anything significant to the user experience or anything substantial enough to warrant
a minor version update. For example, had version 1.0 not included planetary atmospheres, the addition of that
feature would have qualified for version 1.1, but the addition of a new text field for a previously ignored gas,
say hydrogen chloride, would not qualify.

### Building the Windows Installer

This is separate from the MSYS2 instructions above, which are for building and running natively on Windows.
`package/build_windows_installer.sh` instead cross-compiles a Windows .exe from a Linux machine and packages
it into `Alienorum-<version>-win64.exe`, a self-contained NSIS installer for non-programmer end users.

One-time setup on the Linux build machine:

```
sudo apt-get install -y g++-mingw-w64-x86-64 nsis
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

`vcpkg` (dependencies) and NSIS (the installer compiler) are not checked into the repo; the first line
installs NSIS system-wide, and the `git clone`/`bootstrap-vcpkg.sh` steps set up vcpkg once. Both are safe
to redo any time -- if `vcpkg/` is ever deleted, just rerun the two vcpkg lines.

Then, to build:

```
package/build_windows_installer.sh
```

This will:
* Use vcpkg to fetch and build SDL2, SDL2_image, curl, libpng, libjpeg-turbo, and libarchive for the
   `x64-mingw-static` triplet (this step can take 20-40 minutes the first time; later runs are cached and fast).
* Cross-compile `alienorum.exe` with mingw-w64, statically linked, so it carries no external DLL dependencies.
* Package it with the git-tracked contents of `assets/` and `ephemerides/`, the small tracked `catalogs/*`
  files, and a starter set of texture maps (see below) into an NSIS installer, written to
  `package/Alienorum-<version>-win64.exe`.

To cut a new release, bump `VERSION` in the `project(alienorum VERSION x.y.z ...)` line near the top of
`CMakeLists.txt` and rerun the script.

#### What gets bundled vs. downloaded

Alienorum downloads its large scientific catalogs and most texture maps itself at runtime (see `CatalogReader`
in `src/classes/cat.cpp` and the `SurfMap`/`CloudMap`/`BumpMap`/`NightMap` URLs in `catalogs/planets.json`).
The installer bundles the small git-tracked `catalogs/*` files that are actually read by live code and have
no live download URL (or one disabled with a leading `#`): `star_orbits.dat`, `planets.json`,
`soles_alienorum.dat`, `mainseq.dat`, `urls.dat`, and `sat/sources.json`. (`catalogs/WD_names.dat` is also
git-tracked but is not read anywhere in the source -- its one related code path is gated behind `if (0)` --
so it's deliberately left out rather than shipped unused.) It also bundles the git-tracked files in `maps/`
(currently the Moon, Mars, Earth, Pluto, Charon, and Uranus's rings), the git-tracked `.gz` files in
`ephemerides/` (osculating elements for the local solar system -- `Orbit::read_osc_elements()` in
`celestial.cpp` only ever reads these locally, there's no download fallback, so they have to ship), and the
git-tracked contents of `assets/` (fonts, icons, `themes.json`). `assets/` and `maps/` are staged via
`git ls-files`, specifically so nothing that's only sitting in the local working directory -- draft images,
alternate mascots, editor source files, unused fonts, whatever -- ends up in a build by accident.
(`ephemerides/` doesn't require that staging step: only `.gz` files are ever tracked there, and the app-generated
`.txt` decompressions are gitignored, so a plain `*.gz` pattern match in `CMakeLists.txt` is enough on its
own.) The build script regenerates `package/release-maps/` and `package/release-assets/` from `git ls-files`
on every run, so adding something to either bundle is just a matter of `git add`-ing it under `maps/` -- no
changes to the packaging scripts required.
