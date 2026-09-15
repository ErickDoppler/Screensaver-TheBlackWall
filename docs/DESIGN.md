# The Black Wall - design notes

## Scene

World units are roughly metres. The wall is the plane `z = 0`, infinite along
`x`, standing on a floor at `y = 0`. The camera starts 7.5 units in front of
it at eye height 1.7, looking slightly upward so the fade into the void is
visible. The background is the *space color*.

* **Infinity left/right**: the wall grid is generated 80 units wide around the
  camera and fogged into the space color with distance, so the edge is never
  visible. Grid columns snap to multiples of the pixel spacing, so pixels stay
  fixed in the world while the camera slides.
* **Infinity upward**: pixel alpha decays as `exp(-y / 6)`; the grid stops at
  18 units where alpha is about 5 %.
* **Floor**: a single dark quad, fogged with distance, with a soft glow of the
  wall color at the foot of the wall that ripples in step with the wave. The
  wall is drawn a second time mirrored (`y -> -y`) at low alpha as a cheap
  reflection.

## Wall geometry on the GPU

There are no vertex buffers. The wall is one `glDrawArrays(GL_POINTS, 0,
cols * rows)` call; the vertex shader derives the grid cell from
`gl_VertexID`, displaces it in `z`, computes fade, fog and twinkle, and sizes
the point sprite by perspective. The fragment shader shapes the sprite:
square, soft round, or a glyph from a 16-glyph 8x8 atlas (Matrix code).

**Anti-moire**: a regular grid of point sprites beats against the pixel grid
wherever a cell shrinks to a few pixels, worst at grazing angles where the
columns compress. The cure is to stop drawing a grid out there at all. The
cell's true on-screen footprint is measured by projecting the neighbouring
column and row; once its smaller dimension drops under about 4 px the sprite
is grown until it covers its own cell, so neighbours merge into one
continuous sheet of light, and its alpha is divided by the same area ratio so
the wall's brightness does not change with distance. Near the camera the
cells are resolvable, nothing happens, and the panel stays crisp. Dithering
was tried first and rejected: it removed the moire but left the wall grainy.

That alone still left arcs at dense settings, where the cells are a few
pixels but the dots are barely one: a hard-edged sprite that small snaps to
whole pixels as it moves, and a grid of those beats. So no sprite is drawn
below 2 px, and `wall.frag` softens every sprite's edge by about one screen
pixel, whatever its shape or size (`aa = 1.1 / size`). A big sprite keeps a
crisp edge, a small one melts into a smooth blob, continuously in between:
there is no size at which the wall changes character, and no edge sharp
enough to snap to the pixel grid. The lost area is given back so brightness
does not drift with distance. An earlier attempt that switched small sprites
to a different shape was rejected: it fixed the moire but made the wall look
edgy and inconsistent across distance.

**Dot brightness**: with the brightness now energy-correct, a small dot on a
wide spacing lights only a little of the wall's area, and high density with
small pixels would go nearly black. Dots are individually brightened as they
get sparser, `clamp(0.35 / coverage, 1, 20)` where coverage is
`(point size / spacing)^2`, so the sliders change the texture of the wall
rather than how bright it is. At the default settings the factor is 1.

Pixel size and density are independent. **Pixel size** picks the dot itself,
on a log scale from 0.02 world units (about 2 x 2 screen pixels at the wall)
up to 0.30, a chunky panel LED; it is not a fraction of anything.
**Density** then says how far apart those dots sit, measured in the dots
themselves:

    gap     = (100 - density) * dot + smallest dot
    spacing = dot + gap

So at 100 % the dots stand one smallest-dot gap apart whatever their size,
and every percent below that opens another dot's width of space between
them: at 0 % the gap is a hundred dots wide. The useful range is therefore
the top of the slider, by design. The grid is capped at 4800 x 700 points,
and invisible points are pushed out of the clip volume before rasterization.

## Scenery

* **Floor debris** (`debris.vert`): a coarse 3-unit grid of cells around the
  camera, about 45 % of them holding one dim pixel at a hashed offset. Five
  or six are in view at a time and stay fixed in the world, so the eye reads
  the camera's motion over the floor.
* **City horizon** (`city.vert`, *Distant city on the opposite horizon*,
  default on, color configurable, default blue): a skyline of lit windows
  750 units behind the camera (far plane 2500). Building heights come from a
  per-building hash, the skyline wobbles slowly like a mirage, and a faint
  reflection is drawn below the horizon. The generated strip is 2800 units
  wide and its alpha follows a gaussian in x around the camera, so it fades
  out long before the strip ends and no edge is ever visible while sliding.
  Sprites under 4 px are brightened so the distant city stays legible. The
  camera's distance from the wall is clamped to 20, so the city is never
  reachable and the wall never leaves view. Yaw is unrestricted so the user
  can turn around to see it.
* **The figures** (`man.vert`, `models.h`): seven characters, each the vertex
  cloud of a GLB model in `res/models`, converted at build time by
  `tools/gltf2points.c` (cgltf, MIT) into embedded int16 tables (up to 40k
  points each) so the `.scr` stays one file. They are drawn as tiny pixels
  only: each point flickers, and thin horizontal "scan" bands drift upward
  through the body. Colors by nature: engrams (Johnny, Alt) bright crimson,
  the half-human Songbird gold, the AIs (Brendan, Skippy) bright yellow, the
  humans (David, Jackie) blue. Heights: 1.8 like Johnny, except Brendan 3 m
  and Alt 5 m; taller figures are brightened in proportion. Facing per figure
  is a packed setting (`ghost-yaws`, 3 bits each, 45-degree steps) chosen in
  the pose tool: `TheBlackWall.scr /w --pose` (also the *Position the
  ghosts...* button in the settings dialog); Up/Down pick the figure,
  Left/Right rotate, Enter saves, Esc cancels. The *Show ghosts* checkbox
  turns the figures off.

  Timeline (`sim.c`): every 100 units of travel along the wall (keys or
  automatic side movement alike) a figure, never the same as the last one,
  is placed ahead in the direction of travel, 10 units from the wall, and
  stands still. How far ahead scales with the camera's speed (see **Timing
  the catch**), but it *fades in by distance, not by time*, over a band that
  sits outside the distance at which the wall reacts to it (trigger + 35 down
  to trigger + 12, never closer than 45 down to 30). At a walk that is the
  45-30 band; at a run, where the wall reaches for a figure 128 units out,
  the figure appears at 237 and fades in over 163-140, so it is a distant
  speck that grows rather than something taken while still invisible. When the camera comes within 10 units, the whole visible wall
  swells out toward the figure ("surge", `uSurge` in `wall.vert`): a gaussian
  profile 25 units wide centred on the figure, whose top leans out 35 % more
  than its base (a breaking-wave curl) with a slow ripple rolling across it.
  It rises to half the distance over 5 s, holds and breathes for 2 s, then
  rolls on to 13 units over 3.5 s, dissolving the figure as the front at body
  height passes it. A body-sized "scar" of bright pixels tinted with the city
  color is left on the wall (fading with a 4 s time constant) while the wall
  settles back over 5 s. Nothing is aimed: it is meant to read as inevitable
  motion, not an attack. The travel counter then restarts. Testing:
  `--figure-every -1` summons a figure at once, `--model <n>` picks the
  figure the pose tool starts with.

* **Infinite wall**: the fine grid covers 40 units either side of the camera;
  beyond it three detail rings each double the spacing and the extent
  (40-80, 80-160, 160-320 units), skipping the band the previous level
  covers. A doubling step is invisible at that distance whatever the density
  and pixel size, unlike a single coarse far grid. Each ring is brightened
  (2^(0.8 k)) to keep the glow continuous. The wall fades into darkness with
  distance and is fully dark at 200 units (smoothstep from 30 to 200); the
  floor and debris use the same fade.

* **Scar**: when a figure is swallowed, its own points are drawn again in
  "scar mode": projected flat onto the wall surface (following the wall's
  wave and surge), in the figure's color, so the shadow has exactly the
  figure's shape and size, with a slight fixed jitter and larger, dimmer
  sprites for soft, burnt-in edges. It forms point by point while the wave
  front passes through the figure (the figure's alpha and the shadow's birth
  fraction are both driven by the front's position), so nothing is "slapped
  on" afterwards. Its dots are the size of the wall's dots, at low alpha. It
  then stays whole and quiet for 5 s (no sparks at all) and dissolves over
  15 s: every point has its own death moment; just before it, the point
  flares (bigger, brighter) for a fraction of a second, then it is gone.

* **Figure sizes and turns**: Alt is 15 m tall; taller figures are brightened
  up to 8x since the same point count spreads over more area. Brendan has a
  built-in 90-degree counter-clockwise turn (`base_yaw` in `models.h`),
  applied on top of the facing saved in the pose tool.

* **Defaults**: mouse rotation on (a click exits), keys move the camera
  (exit on any key off), Esc always exits.

## The camera at rest

The camera stands 11 units from the wall with side movement on, so the drift
passes 1 unit from the figures (which stand at 10) and they sweep past in
full view; with side movement off it stands at 29, where the whole wave and
the traffic spikes fit in frame.

With side movement on, after 5 s the view turns toward the direction of
travel until the wall fills 70 % of the screen width, leaving the rest for
the space ahead where the next figure fades in. Yaw y puts the vanishing
point of the wall's +x direction at NDC `cot(y) / tan(fovx/2)`, so coverage
c means `cot(y) = (2c - 1) * tan(fovx/2)`; the turn eases in over ~2.5 s.
Any input at all - a key, a movement, a mouse look - hands the controls to
the user: the drift along the wall stops and the view is left alone. Both
resume after 20 s of no input (`IDLE_RESUME`), the turn easing back in after
the usual delay. The turn has to come back: a figure 40 units ahead along
the wall stands barely a metre off the camera's own path, so it is only in
frame when the view is turned along the wall.

Figures use the wall's pixel type and their dots scale with the wall's dot
size (35 % of it, clamped), so a change of density, pixel size or pixel type
carries to them. The blur slider also widens and dims their dots, on top of
the screen-space blur they already share.

**Inside the wall**: while the surge has swept over the camera
(`sim_wall_z` at the camera's position exceeds its distance from the wall)
the ghost tails are forced to a decay of 0.9916 per 60 Hz frame, a 1.97 s
time constant: three times the slider's longest. Wall points within 3 units
of the eye fade out, so the crossing reads as a wall passing through us
rather than a white sheet. The wave is made to reach past the viewer only
while they are at the default resting depth; once they step away it may pass
them by.

**Timing the catch**: the wall needs `BULGE + HOLD + WAVE` = 10.5 s from
noticing a figure to the crash, during which the camera keeps drifting. So
both the distance a figure appears at and the distance that triggers the
sequence scale with the camera's smoothed speed, and the crash lands while
the figure is still in front of the viewer instead of somewhere behind.

**Collision** is per point, not per figure. Each point of the body tests the
wall's own surface (`wallDisp`, the same function the wall shader uses)
against where it stands. A point the wall has passed becomes dust: it puffs
bright, is thrown off in a direction picked uniformly on a sphere, drifts
with drag (2.5 s time constant) and a slow sway, and then simply hangs. It
is never faded out. It disappears when the retreating wall passes it again
and scoops it up, outermost motes first: each mote is drawn into the wall
over its last stretch rather than blinking out, and its own threshold is
offset at random so they do not all go together. Only someone the wall has
swallowed sees the dust at all (`uDustVisible`); from outside the figure is
simply gone and its imprint is all that remains.

**Embers** (`ember.vert`): 16 tiny sparks are left where the figure stood.
They are armed when the wave ends and start only once the retreating wall
has actually drawn back past their place, plus 0.2 s. Each then drifts down
for about five seconds, shimmering fast and unevenly. Half go out in mid-air
at a random moment between one and four seconds; the rest reach the floor
and die there over three more. They are never more than 3.5 px across, and
they snap on and off at their own fast rate rather than pulsing, so they read
as sparks. Over a dark space they take the figure's colour; over a light one
they take the (dark) wall colour, where a bright spark would vanish. The
whole thing outlives the retreat, so it runs on its own clock in the Ghost
rather than on the phase. The dust clock is `wave_t`,
monotonic from the start of the wave through the retreat; a point's own
moment of being hit follows from where it stands between the wave's start
and its crash.

**The imprint is the wall, not a decal.** Nothing is drawn over the wall.
The figure's silhouette is rendered once into a small mask
(`mask.vert`, 160 x 320, in the plane of the wall, redone only when the
figure changes), and the wall shader samples it: a wall pixel inside the
silhouette whose own displacement has carried it out to where the figure
stood is marked, and later goes out on its own schedule with a spark. So the
marked dots are the wall's dots, at the wall's spacing, shape and brightness
gradient. A marked pixel is not filled with flat colour: `wall.frag` fills it
with snow, the way a television shows a dead channel. The grain is one screen
pixel, the finest the display can give, re-rolled 45 times a second, and it
runs from near black to far brighter than the wall around it.

## On foot

The eye rides at a standing height that Q/E adjust, with a crouch eased in
under it and a jump added on top. Shift is three times speed; Ctrl crouches
0.7 m at half speed and overrides Shift. Space jumps to a 0.5 m apex.

Jumping again within 0.35 s of landing, while moving, increments a chain.
From the third jump the horizontal speed doubles (so Shift and a chain
together are six times normal) and the jump grows: the apex climbs 0.5 -> 1.0
and gravity eases 9.8 -> 6.0, so it also stays up longer, 0.64 s to 1.15 s.
Half a second on the ground clears the chain. Verified with `--hop-test`,
which hops on every landing and logs each jump.

## The corridor

The camera may walk up to 1000 units away from the wall. Distance fade is
measured *along* the wall (|dx|), so stepping back never dims it; the fine
grid spans 240 units either side of the camera, past the 200-unit fade, so
no detail-level seam exists. The wall geometry is 56 units tall (16 fade
heights) so its top edge is never visible; beyond 50 units back the glow's
fade height grows with distance (up to 5x at 450 units) and the wall looms.
The city mirage keeps a constant 750 units behind the camera. Floor debris
follows the camera in depth, hashed on world cells.

Figures beyond 50 units back appear at the camera's depth (5 units nearer
the wall) instead of 10 units from the wall; the surge's reach, crash and
width scale with that depth so the wall still comes out to take them.

**Signal loss** (`glitch.frag`, a final screen-space pass):

| Distance from wall | Effect |
|--------------------|--------|
| 900 - 970          | random static sparkles in the wall, city and space colors, brighter with distance |
| 970 - 999          | plus broken-video distortion: torn rows, displaced blocks, channel split |
| >= 999             | TV-off collapse (0.8 s), then black with barely visible CCTV lines, 5-10 px, in the wall color |

Walking back below 999 plays the collapse in reverse. Testing: `--ghost-after -1` summons the figure immediately;
  `--yaw` aims the camera.

  Viewing geometry to keep in mind: the default camera walks 9 units from the
  wall facing it, so a figure 10 units from the wall is beside or behind the
  viewer's shoulder; the user turns (J/L, or mouse rotation) or steps back to
  see it.

## Simulation (src/sim.c)

The CPU sends only uniforms: wave parameters and up to 48 spikes.

**Wave** (CPU load): `z = A * sin(k * x + phase + 0.25 y) + 0.3 A * sin(2.3 k x + phase2)`

| CPU load | `k` (rad/unit)        | wavelength | amplitude `A`        |
|----------|-----------------------|------------|----------------------|
| 0 %      | 0.028                 | ~225 units | 0.9 (calm breathing) |
| 100 %    | 0.028 * 8 = 0.224     | ~28 units  | 0 (flat line)        |

`A = 0.9 * (1 - cpu)^1.6`, `k = 0.028 * (1 + 7 cpu)`; both follow the smoothed
load with a 2.5 s time constant so the wall never snaps.

**Spikes** (network): each spike is a bump `amp * env(t) * exp(-(r / width)^shape)`.

| Traffic          | count (whole zone) | amplitude | width | shape        | life      |
|------------------|--------------------|-----------|-------|--------------|-----------|
| < 2 kbps         | 0                  |           |       |              |           |
| 2 - 300 kbps     | 40-60              | 0.35      | 2.4   | 2 (gaussian) | 4 s       |
| >= 10 Mbps       | ~28                | 4.5       | 0.4   | 1 (cusp)     | 1.4 s     |

Between 300 kbps and 10 Mbps the parameters interpolate on a log scale.
Heavy-traffic spikes mostly push toward the viewer (+z) "like someone trying
to break the wall"; light ones breathe both ways. Spikes spawn across six
times the camera's visible half-width: full size within three times it,
then tapering to 20 % at the outer edge, so the activity is seen far along
the wall and thins out into the distance. Amplitude is capped so even the
largest spike stays comfortably inside the frame.

**Figures**: dealt from a shuffled deck so every character appears once
before any repeats (a new deck never starts with the one just shown); the
random seed differs per run. A figure the user walks away from is taken by
the wall once the camera is more than 50 units from it.

**Precision**: side movement runs for hours, so camera `x` is a double and
everything sent to the GPU is relative to an origin snapped every 64 units.
The wave phase is corrected on each snap to stay continuous.

## Light backgrounds

All glowing things (wall, reflection, debris, city, figures, scars) output
premultiplied color and are blended additively, which only works over a dark
space color. When the space color's luma exceeds 0.5 (inverted themes: white
space, black wall, grey floor) the same output is composited "over" the
background (`GL_ONE, GL_ONE_MINUS_SRC_ALPHA`) so dark pixels darken it, and
the ghost-tail accumulation runs on the inverted picture so darkness is what
persists. Nothing else changes.

## Post-processing

Rendered offscreen only when needed:

* **Ghost tail**: `acc = max(frame, acc * decay)` with
  `decay = lerp(0.80, 0.975, tail)^(dt * 60)` (frame-rate independent).
* **Blur**: separable 9-tap gaussian, radius up to 7 px at 1080p.

With both sliders at 0 the scene renders straight to the back buffer.

## Energy

* Frame rate is capped by the *Power* slider (default 60). When the cap
  divides the display refresh rate (60, 30, 20 on a 60 Hz panel) the swap is
  vsynced and, for caps below the refresh rate, the frame is held with a
  precise sleep until just before the n-th vblank, so every frame lands on a
  vblank and motion is even. Other caps use sleep pacing without vsync.
* Pixels keep a stable identity while the camera slides: twinkle phase and
  Matrix glyphs are hashed from the *world* column, not the grid slot.
  The *Pixel shimmer* slider deliberately re-rolls each pixel's brightness
  about 8 times a second (0 = off, the smooth default).
* System counters are read twice per second; everything else is smoothing.
* One draw call for the wall, one for its reflection, one for the floor.
* The preview mode (Windows settings dialog) caps density at 50 %, FPS at 20
  and disables ghost tails.

## Input rules (fullscreen)

| Setting                  | Behaviour                                                     |
|--------------------------|---------------------------------------------------------------|
| Exit on any button       | any key exits; otherwise keys steer the camera (see README)   |
| Mouse controlled rotation| relative mouse look; a click exits                            |
| Exit on mouse move       | motion beyond a threshold exits; sensitivity 100 % = 4 px, 0 % = 200 px; the first 0.5 s are ignored to swallow the cursor jump at start-up |
| Mouse click resets view  | click restores the default camera instead of exiting          |
| Esc                      | always exits                                                  |

Assumption: the design does not say how to leave with the mouse when rotation
is on, so a click exits in that mode.

## Platform shells

Windows (`platform_win32.c`): registry store, `GetSystemTimes` for CPU,
`GetIfTable2` (physical adapters, in+out octets) for network, the Win32
settings dialog with trackbars, owner-drawn color buttons and `ChooseColor`,
and a child window created inside the preview HWND for `/p`.

Linux (planned): XScreenSaver hack reading `-window-id`, `/proc/stat` and
`/proc/net/dev`, settings from the XScreenSaver XML-driven command line.
