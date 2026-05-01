project('nethack', 'c',
  version : '3.7',
  default_options : ['c_std=c11', 'warning_level=0']
)

cc = meson.get_compiler('c')
src_root = meson.source_root()

# ---------------------------------------------------------------------------
# Common compile flags / defines
# ---------------------------------------------------------------------------
playground = src_root / 'playground'

nh_defs = [
  '-DDLB',
  '-DHACKDIR="' + playground + '"',
  '-DDEFAULT_WINDOW_SYS="hack2bridge"',
  '-DSYSCF',
  '-DSYSCF_FILE="' + playground + '/sysconf"',
  '-DSECURE',
  '-DTIMED_DELAY',
  '-DDUMPLOG',
  '-DCONFIG_ERROR_SECURE=FALSE',
  '-DCOMPRESS="/bin/gzip"',
  '-DCOMPRESS_EXTENSION=".gz"',
  '-DSELF_RECOVER',
  '-DSIG_RET_TYPE=__sighandler_t',
  '-DNOSTATICFN',
  '-DWANT_WIN_TTY',
  '-DHACK2BRIDGE_GRAPHICS',
]

lua_defs = ['-DLUA_USE_LINUX']

inc_nh        = include_directories('include')
inc_lua       = include_directories('submodules/lua')
inc_hack2b    = include_directories('win/hack2bridge')

# ---------------------------------------------------------------------------
# Generate include/nhlua.h  (wraps Lua headers, adds NORETURN to lua_error)
# The original build generates this from Makefile.top's lua_support target.
# We recreate the same output here.  Written to the source include/ dir so
# all sources that do #include "nhlua.h" find it via inc_nh.
# ---------------------------------------------------------------------------
gen_nhlua = custom_target('nhlua_h',
  output  : 'nhlua.h',
  input   : ['submodules/lua/lua.h'],
  command : [
    find_program('gen_nhlua.sh'),
    src_root,
    meson.current_build_dir(),
  ],
  build_by_default : true,
)

# ---------------------------------------------------------------------------
# Lua static library  (all *.c except lua.c, luac.c, ltests.c, onelua.c)
# ---------------------------------------------------------------------------
lua_sources = files(
  'submodules/lua/lapi.c',
  'submodules/lua/lauxlib.c',
  'submodules/lua/lbaselib.c',
  'submodules/lua/lcode.c',
  'submodules/lua/lcorolib.c',
  'submodules/lua/lctype.c',
  'submodules/lua/ldblib.c',
  'submodules/lua/ldebug.c',
  'submodules/lua/ldo.c',
  'submodules/lua/ldump.c',
  'submodules/lua/lfunc.c',
  'submodules/lua/lgc.c',
  'submodules/lua/linit.c',
  'submodules/lua/liolib.c',
  'submodules/lua/llex.c',
  'submodules/lua/lmathlib.c',
  'submodules/lua/lmem.c',
  'submodules/lua/loadlib.c',
  'submodules/lua/lobject.c',
  'submodules/lua/lopcodes.c',
  'submodules/lua/loslib.c',
  'submodules/lua/lparser.c',
  'submodules/lua/lstate.c',
  'submodules/lua/lstring.c',
  'submodules/lua/lstrlib.c',
  'submodules/lua/ltable.c',
  'submodules/lua/ltablib.c',
  'submodules/lua/ltm.c',
  'submodules/lua/lundump.c',
  'submodules/lua/lutf8lib.c',
  'submodules/lua/lvm.c',
  'submodules/lua/lzio.c',
)

liblua = static_library('lua',
  lua_sources,
  c_args : lua_defs,
  include_directories : inc_lua,
)

# ---------------------------------------------------------------------------
# hacklib static library  (used by makedefs and nethack)
# ---------------------------------------------------------------------------
libhacklib = static_library('hacklib',
  ['src/hacklib.c', 'util/panic.c', gen_nhlua],
  c_args : nh_defs,
  include_directories : inc_nh,
)

# ---------------------------------------------------------------------------
# makedefs executable
# makedefs.c #includes mdlib.c directly; monst.c, objects.c, alloc.c,
# date.c, panic.c are compiled as separate TUs (OMONOBJ + OALLOC + ODATE).
# hacklib provides eos(), nh_snprintf() etc. used by mdlib.c at link time.
# ---------------------------------------------------------------------------
makedefs_sources = files(
  'util/makedefs.c',
  'src/monst.c',
  'src/objects.c',
  'src/alloc.c',
  'src/date.c',
  'util/panic.c',
)

makedefs_exe = executable('makedefs',
  [makedefs_sources, gen_nhlua],
  c_args : nh_defs,
  include_directories : [inc_nh, inc_lua],
  link_with : [libhacklib, liblua],
  link_args : ['-lm', '-ldl', '-rdynamic'],
)

# ---------------------------------------------------------------------------
# Generated headers from makedefs
#
# makedefs uses OLD_MAKEDEFS_OPTIONS and writes to ../include/ relative to
# its CWD.  We run it from the source util/ dir so ../include/ resolves to
# the source include/ directory.  After running, we copy the result to the
# meson build directory so Meson can track the output as a proper target.
# ---------------------------------------------------------------------------
gen_onames = custom_target('onames_h',
  output  : 'onames.h',
  input   : [],
  command : [
    'sh', '-c',
    'cd "$0/util" && "$1" -o && cp "$0/include/onames.h" "$2/onames.h"',
    src_root, makedefs_exe, meson.current_build_dir(),
  ],
  build_by_default : true,
)

gen_pm = custom_target('pm_h',
  output  : 'pm.h',
  input   : [],
  command : [
    'sh', '-c',
    'cd "$0/util" && "$1" -p && cp "$0/include/pm.h" "$2/pm.h"',
    src_root, makedefs_exe, meson.current_build_dir(),
  ],
  build_by_default : true,
)

# ---------------------------------------------------------------------------
# Core nethack sources  (HOBJ list minus hacklib.c, which is in the lib)
# ---------------------------------------------------------------------------
core_sources = files(
  'src/allmain.c',
  'src/alloc.c',
  'src/apply.c',
  'src/artifact.c',
  'src/attrib.c',
  'src/ball.c',
  'src/bones.c',
  'src/botl.c',
  'src/calendar.c',
  'src/cfgfiles.c',
  'src/cmd.c',
  'src/coloratt.c',
  'src/dbridge.c',
  'src/decl.c',
  'src/detect.c',
  'src/dig.c',
  'src/display.c',
  'src/dlb.c',
  'src/do.c',
  'src/do_name.c',
  'src/do_wear.c',
  'src/dog.c',
  'src/dogmove.c',
  'src/dokick.c',
  'src/dothrow.c',
  'src/drawing.c',
  'src/dungeon.c',
  'src/earlyarg.c',
  'src/eat.c',
  'src/end.c',
  'src/engrave.c',
  'src/exper.c',
  'src/explode.c',
  'src/extralev.c',
  'src/files.c',
  'src/fountain.c',
  'src/getpos.c',
  'src/glyphs.c',
  'src/hack.c',
  'src/iactions.c',
  'src/insight.c',
  'src/invent.c',
  'src/isaac64.c',
  'src/light.c',
  'src/lock.c',
  'src/mail.c',
  'src/makemon.c',
  'src/mcastu.c',
  'src/mdlib.c',
  'src/mhitm.c',
  'src/mhitu.c',
  'src/minion.c',
  'src/mklev.c',
  'src/mkmap.c',
  'src/mkmaze.c',
  'src/mkobj.c',
  'src/mkroom.c',
  'src/mon.c',
  'src/mondata.c',
  'src/monmove.c',
  'src/monst.c',
  'src/mplayer.c',
  'src/mthrowu.c',
  'src/muse.c',
  'src/music.c',
  'src/nhlua.c',
  'src/nhlsel.c',
  'src/nhlobj.c',
  'src/nhmd4.c',
  'src/objects.c',
  'src/o_init.c',
  'src/objnam.c',
  'src/options.c',
  'src/pager.c',
  'src/pickup.c',
  'src/pline.c',
  'src/polyself.c',
  'src/potion.c',
  'src/pray.c',
  'src/priest.c',
  'src/quest.c',
  'src/questpgr.c',
  'src/read.c',
  'src/rect.c',
  'src/region.c',
  'src/report.c',
  'src/restore.c',
  'src/rip.c',
  'src/rnd.c',
  'src/role.c',
  'src/rumors.c',
  'src/save.c',
  'src/selvar.c',
  'src/sfbase.c',
  'src/sfstruct.c',
  'src/shk.c',
  'src/shknam.c',
  'src/sit.c',
  'src/sounds.c',
  'src/sp_lev.c',
  'src/spell.c',
  'src/stairs.c',
  'src/symbols.c',
  'src/sys.c',
  'src/steal.c',
  'src/steed.c',
  'src/strutil.c',
  'src/teleport.c',
  'src/timeout.c',
  'src/topten.c',
  'src/track.c',
  'src/trap.c',
  'src/u_init.c',
  'src/uhitm.c',
  'src/utf8map.c',
  'src/vault.c',
  'src/vision.c',
  'src/weapon.c',
  'src/were.c',
  'src/wield.c',
  'src/windows.c',
  'src/wizard.c',
  'src/wizcmds.c',
  'src/worm.c',
  'src/worn.c',
  'src/write.c',
  'src/zap.c',
  'src/version.c',
  'src/date.c',
  # sys sources
  'sys/share/ioctl.c',
  'sys/share/unixtty.c',
  'sys/unix/unixmain.c',
  'sys/unix/unixunix.c',
  'sys/unix/unixres.c',
  # tty window port
  'win/tty/getline.c',
  'win/tty/termcap.c',
  'win/tty/topl.c',
  'win/tty/wintty.c',
  # hack2bridge window port
  'win/hack2bridge/hack2bridge.c',
  # regex
  'sys/share/posixregex.c',
)

# ---------------------------------------------------------------------------
# nethack executable
# ---------------------------------------------------------------------------
dep_m       = cc.find_library('m')
dep_dl      = cc.find_library('dl')
dep_ncurses = cc.find_library('ncurses')

nethack_exe = executable('nethack',
  # Include generated headers as sources so Meson builds them first.
  # They are not compiled (they're .h files) but this establishes the
  # build-order dependency.
  [core_sources, gen_nhlua, gen_onames, gen_pm],
  c_args : nh_defs,
  include_directories : [inc_nh, inc_lua, inc_hack2b],
  link_with : [libhacklib, liblua],
  dependencies : [dep_m, dep_dl, dep_ncurses],
  link_args : ['-rdynamic'],
)
