/* NetHack 3.7  hack2bridge.h */
/* Hack2 bridge window port header.                                */
/* NetHack may be freely redistributed.  See license for details. */

#ifndef HACK2BRIDGE_H
#define HACK2BRIDGE_H

#ifdef HACK2BRIDGE_GRAPHICS

/* Protocol version emitted in < engine.hello on startup. */
#define HACK2B_PROTOCOL_VERSION 1

/* ── Inbound command types ──────────────────────────────────────────────── */

typedef enum {
    HACK2B_CMD_UNKNOWN     = 0,
    HACK2B_CMD_PLAYER_KEY,      /* > player.key key=N          */
    HACK2B_CMD_PLAYER_LINE,     /* > player.line text="..."    */
    HACK2B_CMD_MENU_DONE,       /* > menu.done id=N items="..."*/
    HACK2B_CMD_MENU_CANCEL,     /* > menu.cancel id=N          */
    HACK2B_CMD_TEXT_DISMISS,    /* > text.dismiss id=N         */
    HACK2B_CMD_ANIM_ACK         /* > anim.ack                  */
} Hack2bCmdType;

typedef struct {
    Hack2bCmdType type;
    long          key;          /* HACK2B_CMD_PLAYER_KEY  */
    char          text[256];    /* HACK2B_CMD_PLAYER_LINE */
    long          id;           /* HACK2B_CMD_MENU_DONE/CANCEL/TEXT_DISMISS */
    char          items[129];   /* HACK2B_CMD_MENU_DONE: accelerator chars */
} Hack2bCmd;

extern int           hack2b_parse_command(const char *line, Hack2bCmd *cmd);
extern Hack2bCmdType hack2b_read_command(Hack2bCmd *cmd);

extern struct window_procs hack2bridge_procs;

/* Prototypes for all hack2b_* functions */
extern void hack2b_init_nhwindows(int *, char **);
extern void hack2b_player_selection(void);
extern void hack2b_askname(void);
extern void hack2b_get_nh_event(void);
extern void hack2b_exit_nhwindows(const char *);
extern void hack2b_suspend_nhwindows(const char *);
extern void hack2b_resume_nhwindows(void);
extern winid hack2b_create_nhwindow(int);
extern void hack2b_clear_nhwindow(winid);
extern void hack2b_display_nhwindow(winid, boolean);
extern void hack2b_destroy_nhwindow(winid);
extern void hack2b_curs(winid, int, int);
extern void hack2b_putstr(winid, int, const char *);
extern void hack2b_putmixed(winid, int, const char *);
extern void hack2b_display_file(const char *, boolean);
extern void hack2b_start_menu(winid, unsigned long);
extern void hack2b_add_menu(winid, const glyph_info *, const ANY_P *,
                             char, char, int, int,
                             const char *, unsigned int);
extern void hack2b_end_menu(winid, const char *);
extern int hack2b_select_menu(winid, int, MENU_ITEM_P **);
extern char hack2b_message_menu(char, int, const char *);
extern void hack2b_mark_synch(void);
extern void hack2b_wait_synch(void);
#ifdef CLIPPING
extern void hack2b_cliparound(int, int);
#endif
#ifdef POSITIONBAR
extern void hack2b_update_positionbar(char *);
#endif
extern void hack2b_print_glyph(winid, coordxy, coordxy,
                                const glyph_info *, const glyph_info *);
extern void hack2b_outrip(winid, int, time_t);
extern void hack2b_raw_print(const char *);
extern void hack2b_raw_print_bold(const char *);
extern int hack2b_nhgetch(void);
extern int hack2b_nh_poskey(coordxy *, coordxy *, int *);
extern void hack2b_nhbell(void);
extern int hack2b_doprev_message(void);
extern char hack2b_yn_function(const char *, const char *, char);
extern void hack2b_getlin(const char *, char *);
extern int hack2b_get_ext_cmd(void);
extern void hack2b_number_pad(int);
extern void hack2b_delay_output(void);
#ifdef CHANGE_COLOR
extern void hack2b_change_color(int, long, int);
#ifdef MAC
extern void hack2b_change_background(int);
extern short hack2b_set_font_name(winid, char *);
#endif
extern char *hack2b_get_color_string(void);
#endif
extern void hack2b_status_enablefield(int, const char *, const char *,
                                      boolean);
extern void hack2b_update_inventory(int);
extern win_request_info *hack2b_ctrl_nhwindow(winid, int, win_request_info *);

#endif /* HACK2BRIDGE_GRAPHICS */
#endif /* HACK2BRIDGE_H */
