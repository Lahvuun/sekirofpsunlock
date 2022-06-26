# Set FPS for Sekiro under Linux

Use it like so: `./sekiro-set-fps <pid> <fps>`

For example, you can find pid in a subshell and set fps to 120: `./sekiro-set-fps $(pidof sekiro.exe) 120`

If it doesn't work, make sure you have the read and write permissions to `/proc/<pid>/mem`.

Inspired by https://github.com/uberhalit/SekiroFpsUnlockAndMore
# Set custom resolution
`./sekiro-set-resolution <pid> <screen-width> <game-width> <game-height>`

Get the PID in a subshell and set resolution to 2560x1080: `./sekiro-set-resolution $(pidof sekiro.exe) 2560 2560 1080`

`<screen-width>` should equal to your screen's (viewport's) width, while the `game-` parameters are the resolution you want your game to run at.

Unlike when patching the FPS, the patched resolution is not immediately used by the game — you need to make it change the display mode, which can be accomplished by switching from fullscreen to windowed and back in the game's settings, or by switching to a different resolution and then back.

Alternatively, you can do what SekiroFpsUnlockAndMore does on Windows and patch the resolution before the game first sets the display mode. This can be accomplished by a script like the following:
```sh
#!/bin/sh
pid=$(pidof sekiro.exe)
pidof_exit=$?
while [ $pidof_exit -eq 1 ]
do
	pid=$(pidof sekiro.exe)
	pidof_exit=$?
done
./sekiro-set-resolution $pid 2560 2560 1080
```
It will repeatedly search for a process named "sekiro.exe", and, as soon as it finds one, run the patcher.

Keep in mind that you can only patch the resolution once. Subsequent attempts will fail until you restart the game. It's possible to have it working, but the current resolution would also have to be specified, so the command would become something like `./sekiro-set-resolution $(pidof sekiro.exe) 2560 1920 1080 2560 1080`. I don't see why anyone would want to change the resolution more than once per play session, so I didn't bother supporting that.

# Autopatching
I imagine a lot of people would like all patches to apply at the start of the game. To this end I have provided the shell script named `waitandpatch.sh`. It will wait for sekiro.exe and run the patcher you provide it, like so: `./waitandpatch.sh ./sekiro-set-resolution 2560 2560 1080`. It can, of course, be called repeatedly, so if you want to set a custom resolution as well as change the FPS lock, you can do `./waitandpatch.sh ./sekiro-set-resolution 2560 2560 1080 && ./waitandpatch.sh ./sekiro-set-fps 120`. Run the command in your terminal, then start the game from Steam.

And, for the laziest, you can run the game and patch everything with a single (albeit lengthy) command: `steam steam://rungameid/814380 & ./waitandpatch.sh ./sekiro-set-resolution 2560 2560 1080 && ./waitandpatch.sh ./sekiro-set-fps 120`!

`sekiro-set-resolution` should be executed first, since it needs to patch the game very early.
