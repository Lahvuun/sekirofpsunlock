# Set FPS for Sekiro under Linux

Use it like so:
`sekiro-set-fps <pid> <fps>`

For example, you can find pid in a subshell and set fps to 120:
`sekiro-set-fps $(pidof sekiro.exe) 120`

If it doesn't work, make sure you have the read and write permissions to /proc/<pid>/mem.

Inspired by https://github.com/uberhalit/SekiroFpsUnlockAndMore
