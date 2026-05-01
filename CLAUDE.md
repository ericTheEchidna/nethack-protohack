# nethack-protohack

Fork of NetHack tracking the `NetHack-3.7` development branch. This repo
will host the `hack2bridge` Protohack windowport alongside the existing
NetHack windowports (tty, curses, X11, Qt).

## Remotes

- `origin` — `github.com/ericTheEchidna/nethack-protohack` (your fork)
- `upstream` — `github.com/NetHack/NetHack` (NetHack DevTeam)

## Before any windowport work

Always sync from upstream first:

```
git fetch upstream
git merge upstream/NetHack-3.7
```

The 3.7 branch is active development. Unsynced windowport work risks
conflicts with NetHack internals that have moved.

## Windowport location

The hack2bridge windowport will live at `win/hack2bridge/`, alongside
the existing `win/tty/`, `win/curses/`, `win/X11/`, etc. It implements
NetHack's `struct window_procs` and emits Protohack wire events on stdout.

See `../protohack/docs/ARCHITECTURE.md` for the full windowport design.
See `../protohack/docs/DESIGN.md` for the protocol philosophy.

## Build

CMake build targeting Linux, macOS, and Windows. Not yet set up — this is
the first task once the windowport is ported from `../../Hack2-1`.
