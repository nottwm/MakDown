# Image and sound preview

Double-click an image or sound file in the explorer (or drop it on the window, or `makdown picture.png`) and it opens in a tab on the right,
next to the markdown preview - like VS Code. Right-click > **Preview** does the same for `.svg` (which otherwise opens as text).

**Images** (png, jpg, gif, bmp, webp, ico, tiff, ... whatever Qt's image plugins read): transparency on a checkerboard, size / format /
file size at the top, **Fit**, **100%**, **-** / **+**, Ctrl+wheel zoom, drag to pan, click toggles fit / 100%, keys `+ - 0 1`. Animated GIF / WebP play.

**Sound** (mp3, wav, ogg, opus, flac, m4a, aac, ...): play / pause (Space), seek bar (Left / Right = 5 s), volume, mute, repeat, cover art
and title / artist when the file has them. Needs QtMultimedia (`MULTIMEDIA=auto` in the Makefile); without it sound files open in the system player.
