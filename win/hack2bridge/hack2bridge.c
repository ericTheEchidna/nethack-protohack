/* NetHack 3.7  hack2bridge.c */
/* Hack2 bridge window port — protocol emission layer.             */
/* Emits structured '< ns.event k=v' lines on stdout; reads        */
/* '> ns.event k=v' commands from stdin.  A frontend (pygame, etc) */
/* launches nethack as a subprocess and speaks this protocol.       */
/* NetHack may be freely redistributed.  See license for details.  */

#include "hack.h"

#ifdef HACK2BRIDGE_GRAPHICS

#include "hack2bridge.h"
#include "func_tab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Set to 1 after the first anim.ack so that subsequent print_glyph calls
 * at non-passable positions (walls) are suppressed during beam animations.
 * Reset to 0 in display_nhwindow(WIN_MAP) at the end of each flush cycle. */
static int hack2b_in_animation = 0;

/* Emit a protocol-safe quoted string: writes the opening/closing double-quote
   and escapes any embedded '"' or '\\' characters.  Used by every protocol
   field that carries arbitrary text. */
static void hack2b_emit_quoted(FILE *fp, const char *s)
{
    const char *p;
    fputc('"', fp);
    if (s) {
        for (p = s; *p; p++) {
            if (*p == '"' || *p == '\\')
                fputc('\\', fp);
            fputc(*p, fp);
        }
    }
    fputc('"', fp);
}

/* ── Window id allocator and type table ──────────────────────────────────
 * NetHack core calls create_nhwindow(NHW_*) and stores the returned ids
 * in WIN_MESSAGE / WIN_STATUS / WIN_MAP / WIN_INVEN.  We hand out distinct
 * positive ids so those globals don't collide with WIN_ERR.
 */
#define HACK2B_MAX_WINS 64
static winid hack2b_next_winid   = 1;
static int   hack2b_win_type[HACK2B_MAX_WINS];     /* 0 = unused */
static int   hack2b_win_as_text[HACK2B_MAX_WINS];  /* 1 = menu promoted to text */

/* ── Status line counter ─────────────────────────────────────────────────
 * NetHack calls putstr(WIN_STATUS, ...) twice per update.
 * clear_nhwindow(WIN_STATUS) resets the index.
 */
static int hack2b_status_line_idx = 0;

/* Build a comma-separated list of active player conditions into buf (size bufsz). */
static void hack2b_build_props(char *buf, int bufsz)
{
    int len = 0;
    int cap;

#define ADDPROP(name) do {                                  \
    int nlen = (int)strlen(name);                          \
    if (len > 0 && len + 1 + nlen < bufsz)                \
        buf[len++] = ',';                                  \
    if (len + nlen < bufsz) {                              \
        memcpy(buf + len, name, (size_t)nlen);             \
        len += nlen;                                       \
    }                                                      \
} while(0)

    if (Stoned)    ADDPROP("stoned");
    if (Slimed)    ADDPROP("slimed");
    if (Strangled) ADDPROP("strangled");
    if (Sick) {
        if (u.usick_type & SICK_VOMITABLE)    ADDPROP("foodpois");
        if (u.usick_type & SICK_NONVOMITABLE) ADDPROP("termill");
    }

    switch (u.uhs) {
        case SATIATED: ADDPROP("satiated"); break;
        case HUNGRY:   ADDPROP("hungry");   break;
        case WEAK:     ADDPROP("weak");     break;
        case FAINTING: ADDPROP("fainting"); break;
        default: break;
    }

    cap = near_capacity();
    switch (cap) {
        case SLT_ENCUMBER: ADDPROP("burdened");   break;
        case MOD_ENCUMBER: ADDPROP("stressed");   break;
        case HVY_ENCUMBER: ADDPROP("strained");   break;
        case EXT_ENCUMBER: ADDPROP("overtaxed");  break;
        case OVERLOADED:   ADDPROP("overloaded"); break;
        default: break;
    }

    if (Blind)         ADDPROP("blind");
    if (Deaf)          ADDPROP("deaf");
    if (Stunned)       ADDPROP("stunned");
    if (Confusion)     ADDPROP("confused");
    if (Hallucination) ADDPROP("halluc");
    if (Invis)         ADDPROP("invisible");
    if (Levitation)    ADDPROP("levitating");
    if (Flying)        ADDPROP("flying");
    if (u.usteed)      ADDPROP("riding");

#undef ADDPROP

    buf[len] = '\0';
}

/* Emit a structured status.update event built from NetHack core globals. */
static void hack2b_emit_status_update(void)
{
    int hp_cur, hp_max;
    const char *title_str;
    char props_buf[512];

    if (Upolyd) {
        hp_cur = u.mh;
        hp_max = u.mhmax;
        title_str = mons[u.umonnum].pmnames[0];
    } else {
        hp_cur = u.uhp;
        hp_max = u.uhpmax;
        title_str = rank_of(u.ulevel, Role_switch, flags.female);
    }

    hack2b_build_props(props_buf, (int)sizeof props_buf);

    fprintf(stdout, "< status.update name=");
    hack2b_emit_quoted(stdout, svp.plname);
    fprintf(stdout, " title=");
    hack2b_emit_quoted(stdout, title_str ? title_str : "");
    fprintf(stdout,
            " st=%d dx=%d co=%d in=%d wi=%d ch=%d"
            " hp=%d hpmax=%d pw=%d pwmax=%d ac=%d"
            " xp=%d xp_pts=%ld dlvl=%d time=%ld props=",
            ACURR(A_STR), ACURR(A_DEX), ACURR(A_CON),
            ACURR(A_INT), ACURR(A_WIS), ACURR(A_CHA),
            hp_cur, hp_max,
            u.uen, u.uenmax, u.uac,
            u.ulevel, (long) u.uexp, depth(&u.uz), svm.moves);
    hack2b_emit_quoted(stdout, props_buf);
    fprintf(stdout, "\n");
    fflush(stdout);
}

/* ── Menu state (global; one menu at a time) ───────────────────────────── */
#define HACK2B_MAX_MENU_ITEMS 128
typedef struct {
    char   acc;
    ANY_P  identifier;
} hack2b_menu_entry;
static hack2b_menu_entry hack2b_menu_buf[HACK2B_MAX_MENU_ITEMS];
static int  hack2b_menu_count    = 0;
static char hack2b_menu_next_acc = 'a';

static char hack2b_acc_next(char cur)
{
    if (cur >= 'a' && cur < 'z') return (char)(cur + 1);
    if (cur == 'z')              return 'A';
    if (cur >= 'A' && cur < 'Z') return (char)(cur + 1);
    return 0;
}

/* Interface definition, for windows.c */
struct window_procs hack2bridge_procs = {
    WPID(hack2bridge),
    0L,
    0L,
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    hack2b_init_nhwindows,
    hack2b_player_selection,
    hack2b_askname,
    hack2b_get_nh_event,
    hack2b_exit_nhwindows,
    hack2b_suspend_nhwindows,
    hack2b_resume_nhwindows,
    hack2b_create_nhwindow,
    hack2b_clear_nhwindow,
    hack2b_display_nhwindow,
    hack2b_destroy_nhwindow,
    hack2b_curs,
    hack2b_putstr,
    hack2b_putmixed,
    hack2b_display_file,
    hack2b_start_menu,
    hack2b_add_menu,
    hack2b_end_menu,
    hack2b_select_menu,
    hack2b_message_menu,
    hack2b_mark_synch,
    hack2b_wait_synch,
#ifdef CLIPPING
    hack2b_cliparound,
#endif
#ifdef POSITIONBAR
    hack2b_update_positionbar,
#endif
    hack2b_print_glyph,
    hack2b_raw_print,
    hack2b_raw_print_bold,
    hack2b_nhgetch,
    hack2b_nh_poskey,
    hack2b_nhbell,
    hack2b_doprev_message,
    hack2b_yn_function,
    hack2b_getlin,
    hack2b_get_ext_cmd,
    hack2b_number_pad,
    hack2b_delay_output,
#ifdef CHANGE_COLOR
    hack2b_change_color,
#ifdef MAC
    hack2b_change_background,
    hack2b_set_font_name,
#endif
    hack2b_get_color_string,
#endif
    hack2b_outrip,
    genl_preference_update,
    genl_getmsghistory,
    genl_putmsghistory,
    genl_status_init,
    genl_status_finish,
    hack2b_status_enablefield,
    genl_status_update,
    genl_can_suspend_yes,
    hack2b_update_inventory,
    hack2b_ctrl_nhwindow,
};

/* ── Inbound command parser ──────────────────────────────────────────────── */

static int hack2b_unquote(const char *src, char *dst, int dstsz)
{
    int len = 0;
    if (!src || *src != '"')
        return 0;
    src++;
    while (*src && *src != '"' && len + 1 < dstsz) {
        if (*src == '\\' && *(src + 1))
            src++;
        dst[len++] = *src++;
    }
    dst[len] = '\0';
    return 1;
}

int hack2b_parse_command(const char *line, Hack2bCmd *cmd)
{
    const char *p;
    const char *kp;
    char *endptr;

    memset(cmd, 0, sizeof *cmd);
    cmd->type = HACK2B_CMD_UNKNOWN;

    if (!line || strncmp(line, "> ", 2) != 0)
        return 0;
    p = line + 2;

    if (strncmp(p, "player.key", 10) == 0) {
        kp = strstr(p, "key=");
        if (!kp) return 0;
        cmd->key = strtol(kp + 4, &endptr, 10);
        if (endptr == kp + 4 || cmd->key < 0 || cmd->key > 255) return 0;
        cmd->type = HACK2B_CMD_PLAYER_KEY;

    } else if (strncmp(p, "player.line", 11) == 0) {
        kp = strstr(p, "text=");
        if (!kp) return 0;
        if (!hack2b_unquote(kp + 5, cmd->text, (int)sizeof cmd->text)) return 0;
        cmd->type = HACK2B_CMD_PLAYER_LINE;

    } else if (strncmp(p, "menu.done", 9) == 0) {
        kp = strstr(p, "id=");
        if (!kp) return 0;
        cmd->id = strtol(kp + 3, &endptr, 10);
        if (endptr == kp + 3) return 0;
        kp = strstr(p, "items=");
        if (kp)
            hack2b_unquote(kp + 6, cmd->items, (int)sizeof cmd->items);
        cmd->type = HACK2B_CMD_MENU_DONE;

    } else if (strncmp(p, "menu.cancel", 11) == 0) {
        kp = strstr(p, "id=");
        if (kp) cmd->id = strtol(kp + 3, NULL, 10);
        cmd->type = HACK2B_CMD_MENU_CANCEL;

    } else if (strncmp(p, "text.dismiss", 12) == 0) {
        kp = strstr(p, "id=");
        if (!kp) return 0;
        cmd->id = strtol(kp + 3, NULL, 10);
        cmd->type = HACK2B_CMD_TEXT_DISMISS;

    } else if (strncmp(p, "anim.ack", 8) == 0) {
        cmd->type = HACK2B_CMD_ANIM_ACK;
    }

    return cmd->type != HACK2B_CMD_UNKNOWN;
}

Hack2bCmdType hack2b_read_command(Hack2bCmd *cmd)
{
    char buf[1024];
    char *nl;

    while (fgets(buf, (int)sizeof buf, stdin)) {
        nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
        if (hack2b_parse_command(buf, cmd))
            return cmd->type;
#ifdef HACK2B_DEBUG
        fprintf(stderr, "[hack2b] rejected command: %s\n", buf);
        fflush(stderr);
#endif
    }
    memset(cmd, 0, sizeof *cmd);
    cmd->type = HACK2B_CMD_UNKNOWN;
    return HACK2B_CMD_UNKNOWN;
}

/* ── Window initialisation ───────────────────────────────────────────────── */

void
hack2b_init_nhwindows(int *argcp UNUSED, char **argv UNUSED)
{
    iflags.window_inited = TRUE;
    iflags.perm_invent = TRUE; /* request inventory updates at moveloop start and after every change */
    fprintf(stdout,
            "< engine.hello version=%d caps=\"status_update inv_structured\"\n",
            HACK2B_PROTOCOL_VERSION);
    fflush(stdout);
}

/* ── Helpers for hack2b_player_selection ───────────────────────────────── */

static void
hack2b_setup_rolemenu(winid win, int race, int gend, int algn)
{
    anything any;
    int i;
    char thisch, lastch = '\0', rolenamebuf[50];

    any = cg.zeroany;
    for (i = 0; roles[i].name.m; i++) {
        if (!ok_role(i, race, gend, algn))
            continue;
        any.a_int = i + 1;
        thisch = lowc(*roles[i].name.m);
        if (thisch == lastch)
            thisch = highc(thisch);
        Strcpy(rolenamebuf, roles[i].name.m);
        if (roles[i].name.f) {
            if (gend == 1)
                Strcpy(rolenamebuf, roles[i].name.f);
            else if (gend < 0) {
                Strcat(rolenamebuf, "/");
                Strcat(rolenamebuf, roles[i].name.f);
            }
        }
        add_menu(win, &nul_glyphinfo, &any, thisch, 0, ATR_NONE, NO_COLOR,
                 an(rolenamebuf), MENU_ITEMFLAGS_NONE);
        lastch = thisch;
    }
}

static void
hack2b_setup_racemenu(winid win, int role, int gend, int algn)
{
    anything any;
    int i;

    any = cg.zeroany;
    for (i = 0; races[i].noun; i++) {
        if (!ok_race(role, i, gend, algn))
            continue;
        any.a_int = i + 1;
        add_menu(win, &nul_glyphinfo, &any, *races[i].noun,
                 highc(*races[i].noun), ATR_NONE, NO_COLOR, races[i].noun,
                 MENU_ITEMFLAGS_NONE);
    }
}

static void
hack2b_setup_gendmenu(winid win, int role, int race, int algn)
{
    anything any;
    int i;

    any = cg.zeroany;
    for (i = 0; i < ROLE_GENDERS; i++) {
        if (!ok_gend(role, race, i, algn))
            continue;
        any.a_int = i + 1;
        add_menu(win, &nul_glyphinfo, &any, *genders[i].adj,
                 highc(*genders[i].adj), ATR_NONE, NO_COLOR, genders[i].adj,
                 MENU_ITEMFLAGS_NONE);
    }
}

static void
hack2b_setup_algnmenu(winid win, int role, int race, int gend)
{
    anything any;
    int i;

    any = cg.zeroany;
    for (i = 0; i < ROLE_ALIGNS; i++) {
        if (!ok_align(role, race, gend, i))
            continue;
        any.a_int = i + 1;
        add_menu(win, &nul_glyphinfo, &any, *aligns[i].adj,
                 highc(*aligns[i].adj), ATR_NONE, NO_COLOR, aligns[i].adj,
                 MENU_ITEMFLAGS_NONE);
    }
}

/* ── hack2b_player_selection ─────────────────────────────────────────────── */

#define ROLE flags.initrole
#define RACE flags.initrace
#define GEND flags.initgend
#define ALGN flags.initalign

void
hack2b_player_selection(void)
{
    int k, n, choice;
    boolean picksomething;
    char pick4u;
    winid win;
    anything any;
    menu_item *selected = 0;

    picksomething = (ROLE == ROLE_NONE || RACE == ROLE_NONE
                     || GEND == ROLE_NONE || ALGN == ROLE_NONE);

    if (flags.randomall && picksomething) {
        if (ROLE == ROLE_NONE) ROLE = ROLE_RANDOM;
        if (RACE == ROLE_NONE) RACE = ROLE_RANDOM;
        if (GEND == ROLE_NONE) GEND = ROLE_RANDOM;
        if (ALGN == ROLE_NONE) ALGN = ROLE_RANDOM;
    }

    rigid_role_checks();

    if (!picksomething || flags.randomall) {
        pick4u = 'y';
    } else {
        pick4u = hack2b_yn_function(
            "Shall I pick a character for you?", "ynq", 'y');
        if (pick4u == 'q' || pick4u == '\033')
            goto give_up;
    }

    /* ── Role ──────────────────────────────────────────────────────────── */
    if (ROLE < 0) {
        if (pick4u == 'y' || ROLE == ROLE_RANDOM) {
            k = pick_role(RACE, GEND, ALGN, PICK_RANDOM);
            if (k < 0)
                k = randrole(FALSE);
        } else {
            win = create_nhwindow(NHW_MENU);
            start_menu(win, MENU_BEHAVE_STANDARD);
            hack2b_setup_rolemenu(win, RACE, GEND, ALGN);
            any = cg.zeroany;
            any.a_int = ROLE_RANDOM;
            add_menu(win, &nul_glyphinfo, &any, '*', 0, ATR_NONE, NO_COLOR, "Random",
                     MENU_ITEMFLAGS_NONE);
            any.a_int = 0;
            add_menu(win, &nul_glyphinfo, &any, 0, 0, ATR_NONE, NO_COLOR, "",
                     MENU_ITEMFLAGS_NONE);
            any.a_int = ROLE_NONE;
            add_menu(win, &nul_glyphinfo, &any, 'q', 0, ATR_NONE, NO_COLOR, "Quit",
                     MENU_ITEMFLAGS_NONE);
            end_menu(win, "Pick a role");
            n = select_menu(win, PICK_ONE, &selected);
            choice = (n > 0) ? selected[0].item.a_int : ROLE_NONE;
            if (selected) free((genericptr_t) selected), selected = 0;
            destroy_nhwindow(win);
            if (choice == ROLE_NONE)
                goto give_up;
            if (choice == ROLE_RANDOM) {
                k = pick_role(RACE, GEND, ALGN, PICK_RANDOM);
                if (k < 0) k = randrole(FALSE);
            } else {
                k = choice - 1;
            }
        }
        ROLE = k;
    }

    /* ── Race ──────────────────────────────────────────────────────────── */
    if (RACE < 0 || !validrace(ROLE, RACE)) {
        if (pick4u == 'y' || RACE == ROLE_RANDOM) {
            k = pick_race(ROLE, GEND, ALGN, PICK_RANDOM);
            if (k < 0) k = randrace(ROLE);
        } else {
            win = create_nhwindow(NHW_MENU);
            start_menu(win, MENU_BEHAVE_STANDARD);
            hack2b_setup_racemenu(win, ROLE, GEND, ALGN);
            any = cg.zeroany;
            any.a_int = ROLE_RANDOM;
            add_menu(win, &nul_glyphinfo, &any, '*', 0, ATR_NONE, NO_COLOR, "Random",
                     MENU_ITEMFLAGS_NONE);
            any.a_int = 0;
            add_menu(win, &nul_glyphinfo, &any, 0, 0, ATR_NONE, NO_COLOR, "",
                     MENU_ITEMFLAGS_NONE);
            any.a_int = ROLE_NONE;
            add_menu(win, &nul_glyphinfo, &any, 'q', 0, ATR_NONE, NO_COLOR, "Quit",
                     MENU_ITEMFLAGS_NONE);
            end_menu(win, "Pick a race");
            n = select_menu(win, PICK_ONE, &selected);
            choice = (n > 0) ? selected[0].item.a_int : ROLE_NONE;
            if (selected) free((genericptr_t) selected), selected = 0;
            destroy_nhwindow(win);
            if (choice == ROLE_NONE)
                goto give_up;
            if (choice == ROLE_RANDOM) {
                k = pick_race(ROLE, GEND, ALGN, PICK_RANDOM);
                if (k < 0) k = randrace(ROLE);
            } else {
                k = choice - 1;
            }
        }
        RACE = k;
    }

    /* ── Gender ────────────────────────────────────────────────────────── */
    if (GEND < 0 || !validgend(ROLE, RACE, GEND)) {
        if (pick4u == 'y' || GEND == ROLE_RANDOM) {
            k = pick_gend(ROLE, RACE, ALGN, PICK_RANDOM);
            if (k < 0) k = randgend(ROLE, RACE);
        } else {
            win = create_nhwindow(NHW_MENU);
            start_menu(win, MENU_BEHAVE_STANDARD);
            hack2b_setup_gendmenu(win, ROLE, RACE, ALGN);
            any = cg.zeroany;
            any.a_int = ROLE_RANDOM;
            add_menu(win, &nul_glyphinfo, &any, '*', 0, ATR_NONE, NO_COLOR, "Random",
                     MENU_ITEMFLAGS_NONE);
            any.a_int = 0;
            add_menu(win, &nul_glyphinfo, &any, 0, 0, ATR_NONE, NO_COLOR, "",
                     MENU_ITEMFLAGS_NONE);
            any.a_int = ROLE_NONE;
            add_menu(win, &nul_glyphinfo, &any, 'q', 0, ATR_NONE, NO_COLOR, "Quit",
                     MENU_ITEMFLAGS_NONE);
            end_menu(win, "Pick a gender");
            n = select_menu(win, PICK_ONE, &selected);
            choice = (n > 0) ? selected[0].item.a_int : ROLE_NONE;
            if (selected) free((genericptr_t) selected), selected = 0;
            destroy_nhwindow(win);
            if (choice == ROLE_NONE)
                goto give_up;
            if (choice == ROLE_RANDOM) {
                k = pick_gend(ROLE, RACE, ALGN, PICK_RANDOM);
                if (k < 0) k = randgend(ROLE, RACE);
            } else {
                k = choice - 1;
            }
        }
        GEND = k;
    }

    /* ── Alignment ─────────────────────────────────────────────────────── */
    if (ALGN < 0 || !validalign(ROLE, RACE, ALGN)) {
        if (pick4u == 'y' || ALGN == ROLE_RANDOM) {
            k = pick_align(ROLE, RACE, GEND, PICK_RANDOM);
            if (k < 0) k = randalign(ROLE, RACE);
        } else {
            win = create_nhwindow(NHW_MENU);
            start_menu(win, MENU_BEHAVE_STANDARD);
            hack2b_setup_algnmenu(win, ROLE, RACE, GEND);
            any = cg.zeroany;
            any.a_int = ROLE_RANDOM;
            add_menu(win, &nul_glyphinfo, &any, '*', 0, ATR_NONE, NO_COLOR, "Random",
                     MENU_ITEMFLAGS_NONE);
            any.a_int = 0;
            add_menu(win, &nul_glyphinfo, &any, 0, 0, ATR_NONE, NO_COLOR, "",
                     MENU_ITEMFLAGS_NONE);
            any.a_int = ROLE_NONE;
            add_menu(win, &nul_glyphinfo, &any, 'q', 0, ATR_NONE, NO_COLOR, "Quit",
                     MENU_ITEMFLAGS_NONE);
            end_menu(win, "Pick an alignment");
            n = select_menu(win, PICK_ONE, &selected);
            choice = (n > 0) ? selected[0].item.a_int : ROLE_NONE;
            if (selected) free((genericptr_t) selected), selected = 0;
            destroy_nhwindow(win);
            if (choice == ROLE_NONE)
                goto give_up;
            if (choice == ROLE_RANDOM) {
                k = pick_align(ROLE, RACE, GEND, PICK_RANDOM);
                if (k < 0) k = randalign(ROLE, RACE);
            } else {
                k = choice - 1;
            }
        }
        ALGN = k;
    }

    if (ROLE < 0) { k = pick_role(RACE, GEND, ALGN, PICK_RANDOM); ROLE = (k < 0) ? randrole(FALSE) : k; }
    if (RACE < 0) { k = pick_race(ROLE, GEND, ALGN, PICK_RANDOM); RACE = (k < 0) ? randrace(ROLE)  : k; }
    if (GEND < 0) { k = pick_gend(ROLE, RACE, ALGN, PICK_RANDOM); GEND = (k < 0) ? randgend(ROLE, RACE) : k; }
    if (ALGN < 0) { k = pick_align(ROLE, RACE, GEND, PICK_RANDOM); ALGN = (k < 0) ? randalign(ROLE, RACE) : k; }
    return;

give_up:
    if (selected)
        free((genericptr_t) selected), selected = 0;
    clearlocks();
    hack2b_exit_nhwindows((char *) 0);
    nh_terminate(EXIT_SUCCESS);
    /*NOTREACHED*/
}

#undef ROLE
#undef RACE
#undef GEND
#undef ALGN

void
hack2b_askname(void)
{
    hack2b_getlin("Who are you?", svp.plname);

    if (*svp.plname) {
        const char *fq_save;
        int save_exists;
#ifdef COMPRESS_EXTENSION
        char gz_path[BUFSZ];
#endif

        set_savefile_name(TRUE);
        fq_save = fqname(gs.SAVEF, SAVEPREFIX, 0);

        save_exists = (access(fq_save, F_OK) == 0);
#ifdef COMPRESS_EXTENSION
        if (!save_exists) {
            Sprintf(gz_path, "%s%s", fq_save, COMPRESS_EXTENSION);
            save_exists = (access(gz_path, F_OK) == 0);
        }
#endif

        if (save_exists) {
            if (hack2b_yn_function("Continue your saved game?", "yn", 'y') == 'n') {
                (void) delete_savefile();
#ifdef COMPRESS_EXTENSION
                Sprintf(gz_path, "%s%s", fq_save, COMPRESS_EXTENSION);
                (void) unlink(gz_path);
#endif
            }
        }
    }
}

void
hack2b_get_nh_event(void)
{
}

void
hack2b_exit_nhwindows(const char *str UNUSED)
{
    if (program_state.gameover) {
        const char *role = (gu.urole.name.m && gu.urole.name.m[0]) ? gu.urole.name.m : "?";
        const char *race = (gu.urace.noun && gu.urace.noun[0]) ? gu.urace.noun : "?";
        const char *kname = svk.killer.name[0] ? svk.killer.name : "(unknown)";
        fprintf(stdout,
                "< game.end score=%ld turn=%ld role=", u.urexp, svm.moves);
        hack2b_emit_quoted(stdout, role);
        fprintf(stdout, " race=");
        hack2b_emit_quoted(stdout, race);
        fprintf(stdout, " name=");
        hack2b_emit_quoted(stdout, svp.plname[0] ? svp.plname : "?");
        fprintf(stdout, " reason=");
        hack2b_emit_quoted(stdout, kname);
        fprintf(stdout, "\n");
        fflush(stdout);
    }
    iflags.window_inited = FALSE;
}

void
hack2b_suspend_nhwindows(const char *str UNUSED)
{
}

void
hack2b_resume_nhwindows(void)
{
}

winid
hack2b_create_nhwindow(int type)
{
    winid w = hack2b_next_winid++;
    if (w < HACK2B_MAX_WINS)
        hack2b_win_type[w] = type;
    if (type == NHW_TEXT)
        fprintf(stdout, "< text.start id=%d\n", (int) w);
    return w;
}

void
hack2b_clear_nhwindow(winid window)
{
    if (window == WIN_MAP)
        fprintf(stdout, "< map.clear\n");
    else if (window == WIN_STATUS)
        hack2b_status_line_idx = 0;
}

void
hack2b_display_nhwindow(winid window, boolean blocking)
{
    if (window == WIN_MAP) {
        hack2b_in_animation = 0;
        fprintf(stdout, "< map.end\n");
        fflush(stdout);
    } else if (window < HACK2B_MAX_WINS &&
               (hack2b_win_type[window] == NHW_TEXT ||
                (hack2b_win_type[window] == NHW_MENU && hack2b_win_as_text[window]))) {
        Hack2bCmd cmd;
        fprintf(stdout, "< text.show id=%d\n", (int) window);
        fflush(stdout);
        (void) blocking;
        hack2b_read_command(&cmd);
    }
}

void
hack2b_destroy_nhwindow(winid window)
{
    if (window < HACK2B_MAX_WINS &&
        (hack2b_win_type[window] == NHW_TEXT ||
         (hack2b_win_type[window] == NHW_MENU && hack2b_win_as_text[window]))) {
        fprintf(stdout, "< text.end id=%d\n", (int) window);
        fflush(stdout);
        hack2b_win_as_text[window] = 0;
        hack2b_win_type[window] = 0;
    }
}

void
hack2b_curs(winid window UNUSED, int x UNUSED, int y UNUSED)
{
}

void
hack2b_putstr(winid window, int attr UNUSED, const char *str)
{
    if (window == WIN_MESSAGE) {
#ifdef USER_SOUNDS
        play_sound_for_message(str);
#endif
        fprintf(stdout, "< msg.line text=");
        hack2b_emit_quoted(stdout, str);
        fprintf(stdout, "\n");
    } else if (window == WIN_STATUS) {
        if (hack2b_status_line_idx >= 2)
            hack2b_status_line_idx = 0;
        fprintf(stdout, "< status.line n=%d text=", hack2b_status_line_idx);
        hack2b_emit_quoted(stdout, str);
        fprintf(stdout, "\n");
        hack2b_status_line_idx++;
        if (hack2b_status_line_idx == 2)
            hack2b_emit_status_update();
    } else if (window < HACK2B_MAX_WINS && hack2b_win_type[window] == NHW_TEXT) {
        fprintf(stdout, "< text.line id=%d text=", (int) window);
        hack2b_emit_quoted(stdout, str);
        fprintf(stdout, "\n");
    } else if (window < HACK2B_MAX_WINS && hack2b_win_type[window] == NHW_MENU) {
        if (!hack2b_win_as_text[window]) {
            hack2b_win_as_text[window] = 1;
            fprintf(stdout, "< text.start id=%d\n", (int) window);
        }
        fprintf(stdout, "< text.line id=%d text=", (int) window);
        hack2b_emit_quoted(stdout, str);
        fprintf(stdout, "\n");
    }
}

void
hack2b_putmixed(winid window, int attr, const char *str)
{
    hack2b_putstr(window, attr, str);
}

void
hack2b_display_file(const char *fname UNUSED, boolean complain UNUSED)
{
}

void
hack2b_start_menu(winid window, unsigned long mbehavior UNUSED)
{
    hack2b_menu_count    = 0;
    hack2b_menu_next_acc = 'a';
    fprintf(stdout, "< menu.start id=%d\n", (int) window);
}

void
hack2b_add_menu(winid window, const glyph_info *glyphinfo,
                const ANY_P *identifier, char ch, char gch UNUSED,
                int attr UNUSED, int color UNUSED,
                const char *str, unsigned int itemflags)
{
    boolean selectable;
    char acc;
    int tile;
    hack2b_menu_entry *e;

    if (hack2b_menu_count >= HACK2B_MAX_MENU_ITEMS)
        return;

    selectable = (identifier && identifier->a_void != 0);
    acc = 0;
    if (selectable) {
        if (ch != 0) {
            acc = ch;
        } else if (hack2b_menu_next_acc != 0) {
            acc = hack2b_menu_next_acc;
            hack2b_menu_next_acc = hack2b_acc_next(hack2b_menu_next_acc);
        }
    }

    e = &hack2b_menu_buf[hack2b_menu_count++];
    e->acc = acc;
    if (selectable)
        e->identifier = *identifier;
    else
        e->identifier.a_void = 0;

    tile = (glyphinfo && glyphinfo->glyph >= 0) ? (int) glyphinfo->gm.tileidx : -1;
    fprintf(stdout,
            "< menu.item id=%d acc=%c tile=%d sel=%d text=",
            (int) window,
            acc ? acc : '-',
            tile,
            (itemflags & MENU_ITEMFLAGS_SELECTED) ? 1 : 0);
    hack2b_emit_quoted(stdout, str);
    fprintf(stdout, "\n");
}

void
hack2b_end_menu(winid window, const char *prompt)
{
    fprintf(stdout, "< menu.end id=%d prompt=", (int) window);
    hack2b_emit_quoted(stdout, prompt);
    fprintf(stdout, "\n");
    fflush(stdout);
}

int
hack2b_select_menu(winid window, int how, MENU_ITEM_P **menu_list)
{
    Hack2bCmd cmd;
    int  picks_len = 0;
    int  i, j, out_count;
    char want;
    MENU_ITEM_P *out;

    *menu_list = (MENU_ITEM_P *) 0;

    fprintf(stdout, "< menu.select id=%d how=%d\n", (int) window, how);
    fflush(stdout);

    hack2b_read_command(&cmd);
    if (cmd.type == HACK2B_CMD_MENU_CANCEL)
        return -1;
    if (cmd.type == HACK2B_CMD_MENU_DONE) {
        picks_len = (int)strlen(cmd.items);
        if (picks_len > HACK2B_MAX_MENU_ITEMS)
            picks_len = HACK2B_MAX_MENU_ITEMS;
    }

    if (how == PICK_NONE || picks_len == 0)
        return 0;

    out = (MENU_ITEM_P *) alloc((unsigned)(sizeof(MENU_ITEM_P) * picks_len));
    out_count = 0;
    for (i = 0; i < picks_len; ++i) {
        want = cmd.items[i];
        for (j = 0; j < hack2b_menu_count; ++j) {
            if (hack2b_menu_buf[j].acc && hack2b_menu_buf[j].acc == want) {
                out[out_count].item  = hack2b_menu_buf[j].identifier;
                out[out_count].count = -1L;
                out_count++;
                if (how == PICK_ONE)
                    goto done;
                break;
            }
        }
    }
done:
    if (out_count == 0) {
        free(out);
        return 0;
    }
    *menu_list = out;
    return out_count;
}

char
hack2b_message_menu(char let UNUSED, int how UNUSED, const char *mesg UNUSED)
{
    return ' ';
}

void
hack2b_update_inventory(int arg UNUSED)
{
    struct obj *obj;
    int glyph, tile;
    glyph_info ogi;
    const char *buc_str;

    fprintf(stdout, "< inv.begin\n");
    for (obj = gi.invent; obj; obj = obj->nobj) {
        glyph = obj_to_glyph(obj, rn2_on_display_rng);
        map_glyphinfo(0, 0, glyph, 0, &ogi);
        tile  = (int) ogi.gm.tileidx;

        if (!obj->bknown)
            buc_str = "unknown";
        else if (obj->blessed)
            buc_str = "blessed";
        else if (obj->cursed)
            buc_str = "cursed";
        else
            buc_str = "uncursed";

        fprintf(stdout,
                "< inv.item slot=%c tile=%d cls=%d worn=%ld quan=%ld buc=%s clean_name=",
                obj->invlet ? obj->invlet : '?',
                tile,
                (int) obj->oclass,
                obj->owornmask,
                obj->quan,
                buc_str);
        hack2b_emit_quoted(stdout, xname(obj));
        fprintf(stdout, " name=");
        hack2b_emit_quoted(stdout, doname(obj));
        fprintf(stdout, "\n");
    }
    fprintf(stdout, "< inv.end\n");
    fflush(stdout);
}

void
hack2b_mark_synch(void)
{
    fflush(stdout);
}

void
hack2b_wait_synch(void)
{
    static long turn_count = 0;
    turn_count++;
    fprintf(stdout, "< map.end\n");
    fprintf(stdout, "< game.turn n=%ld\n", turn_count);
    fflush(stdout);
}

#ifdef CLIPPING
void
hack2b_cliparound(int x UNUSED, int y UNUSED)
{
}
#endif

#ifdef POSITIONBAR
void
hack2b_update_positionbar(char *posbar UNUSED)
{
}
#endif

void
hack2b_print_glyph(winid window UNUSED, coordxy x, coordxy y,
                   const glyph_info *glyphinfo,
                   const glyph_info *bkglyphinfo UNUSED)
{
    int glyph, ch, tile;

    if (!glyphinfo)
        return;

    glyph = glyphinfo->glyph;
    ch    = glyphinfo->ttychar;
    tile  = (int) glyphinfo->gm.tileidx;

    /* Suppress explosion/zap glyphs at non-passable positions during
     * beam animations so missiles don't visually overlap walls. */
    if (hack2b_in_animation && !ZAP_POS(levl[x][y].typ)
            && (glyph_is_explosion(glyph) || glyph_is_swallow(glyph)))
        return;

    fprintf(stdout, "< map.cell x=%d y=%d glyph=%c tile=%d\n",
            (int) x, (int) y, (char) ch, tile);
}

void
hack2b_outrip(winid tmpwin UNUSED, int how, time_t when UNUSED)
{
    char cause[BUFSZ];
    char esc[BUFSZ * 2];
    const char *rolename;
    const char *racename;
    char *src, *dst;

    formatkiller(cause, sizeof cause, how, FALSE);

    src = cause;
    dst = esc;
    while (*src && (dst - esc) < (int)(sizeof esc) - 2) {
        if (*src == '"' || *src == '\\')
            *dst++ = '\\';
        *dst++ = *src++;
    }
    *dst = '\0';

    rolename = (flags.female && gu.urole.name.f) ? gu.urole.name.f : gu.urole.name.m;
    racename = gu.urace.noun;

    printf("< game.end name=\"%s\" reason=\"%s\" role=\"%s\" race=\"%s\" turn=%ld score=0\n",
           svp.plname, esc,
           rolename ? rolename : "?",
           racename ? racename : "?",
           svm.moves);
    fflush(stdout);
}

void
hack2b_raw_print(const char *str UNUSED)
{
}

void
hack2b_raw_print_bold(const char *str UNUSED)
{
}

int
hack2b_nhgetch(void)
{
    Hack2bCmd cmd;
    fflush(stdout);
    hack2b_read_command(&cmd);
    if (cmd.type == HACK2B_CMD_PLAYER_KEY)
        return (int)(unsigned char) cmd.key;
    return '\033';
}

int
hack2b_nh_poskey(coordxy *x, coordxy *y, int *mod)
{
    *x = *y = *mod = 0;
    return hack2b_nhgetch();
}

void
hack2b_nhbell(void)
{
}

int
hack2b_doprev_message(void)
{
    fprintf(stdout, "< msg.prev\n");
    fflush(stdout);
    return 0;
}

static void strip_yn_suffix(const char *src, char *dst, int dstsz)
{
    int len = src ? (int)strlen(src) : 0;
    if (len <= 0 || len >= dstsz) {
        if (dstsz > 0) dst[0] = '\0';
        return;
    }
    memcpy(dst, src, (size_t)(len + 1));
    if (dst[len - 1] == ']') {
        int i = len - 2;
        while (i >= 0 && dst[i] != '[') i--;
        if (i > 0 && dst[i - 1] == ' ') i--;
        if (i >= 0) dst[i] = '\0';
    }
}

char
hack2b_yn_function(const char *ques, const char *choices, char def)
{
    char qbuf[512];
    strip_yn_suffix(ques, qbuf, (int)sizeof qbuf);
    fprintf(stdout, "< msg.yn text=");
    hack2b_emit_quoted(stdout, qbuf);
    if (choices && choices[0]) {
        const char *p;
        fprintf(stdout, " keys=\"");
        for (p = choices; *p; p++) {
            if ((unsigned char)*p >= 32 && (unsigned char)*p < 127)
                fputc(*p, stdout);
        }
        fputc('"', stdout);
    }
    if (def && (unsigned char)def >= 32 && (unsigned char)def < 127)
        fprintf(stdout, " def=%c", def);
    fprintf(stdout, "\n");
    fflush(stdout);
    return (char) hack2b_nhgetch();
}

void
hack2b_getlin(const char *ques, char *input)
{
    Hack2bCmd cmd;

    fprintf(stdout, "< msg.getlin text=");
    hack2b_emit_quoted(stdout, ques);
    fprintf(stdout, "\n");
    fflush(stdout);
    hack2b_read_command(&cmd);
    if (cmd.type == HACK2B_CMD_PLAYER_LINE) {
        strncpy(input, cmd.text, BUFSZ - 1);
        input[BUFSZ - 1] = '\0';
    } else {
        input[0] = '\033';
        input[1] = '\0';
    }
}

int
hack2b_get_ext_cmd(void)
{
    Hack2bCmd cmd;
    int i;

    fprintf(stdout, "< msg.extcmd\n");
    fflush(stdout);

    hack2b_read_command(&cmd);
    if (cmd.type == HACK2B_CMD_PLAYER_LINE) {
        if (cmd.text[0] == '\0' || cmd.text[0] == '\033')
            return -1;
        for (i = 0; extcmdlist[i].ef_txt != (char *) 0; i++)
            if (!strcmpi(cmd.text, extcmdlist[i].ef_txt))
                return i;
        pline("%s: unknown extended command.", cmd.text);
    }
    return -1;
}

void
hack2b_number_pad(int state UNUSED)
{
}

void
hack2b_delay_output(void)
{
    Hack2bCmd cmd;
    fprintf(stdout, "< anim.frame delay_ms=50\n");
    fflush(stdout);
    do {
        hack2b_read_command(&cmd);
    } while (cmd.type != HACK2B_CMD_ANIM_ACK && cmd.type != HACK2B_CMD_UNKNOWN);
    hack2b_in_animation = 1;
}

#ifdef CHANGE_COLOR
void
hack2b_change_color(int color UNUSED, long rgb UNUSED, int reverse UNUSED)
{
}

#ifdef MAC
void
hack2b_change_background(int bkgnd UNUSED)
{
}

short
hack2b_set_font_name(winid window UNUSED, char *fontname UNUSED)
{
    return 0;
}
#endif /* MAC */

char *
hack2b_get_color_string(void)
{
    return (char *) 0;
}
#endif /* CHANGE_COLOR */

void
hack2b_status_enablefield(int fieldidx, const char *nm, const char *fmt,
                          boolean enable)
{
    genl_status_enablefield(fieldidx, nm, fmt, enable);
}

win_request_info *
hack2b_ctrl_nhwindow(winid window UNUSED, int request UNUSED,
                     win_request_info *wri UNUSED)
{
    return (win_request_info *) 0;
}

#ifdef USER_SOUNDS
void
play_usersound(const char *filename, int volume)
{
    fprintf(stdout, "< sound.play file=");
    hack2b_emit_quoted(stdout, filename);
    fprintf(stdout, " vol=%d\n", volume);
    fflush(stdout);
}
#endif /* USER_SOUNDS */

#endif /* HACK2BRIDGE_GRAPHICS */
