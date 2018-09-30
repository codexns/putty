/*
 * Abstraction of the various ways to handle the local end of an SSH
 * connection-layer channel.
 */

#ifndef PUTTY_SSHCHAN_H
#define PUTTY_SSHCHAN_H

struct ChannelVtable {
    void (*free)(Channel *);

    /* Called for channel types that were created at the same time as
     * we sent an outgoing CHANNEL_OPEN, when the confirmation comes
     * back from the server indicating that the channel has been
     * opened, or the failure message indicating that it hasn't,
     * respectively. In the latter case, this must _not_ free the
     * Channel structure - the client will call the free method
     * separately. But it might do logging or other local cleanup. */
    void (*open_confirmation)(Channel *);
    void (*open_failed)(Channel *, const char *error_text);

    int (*send)(Channel *, int is_stderr, const void *buf, int len);
    void (*send_eof)(Channel *);
    void (*set_input_wanted)(Channel *, int wanted);

    char *(*log_close_msg)(Channel *);

    int (*want_close)(Channel *, int sent_local_eof, int rcvd_remote_eof);

    /* A method for every channel request we know of. All of these
     * return TRUE for success or FALSE for failure. */
    int (*rcvd_exit_status)(Channel *, int status);
    int (*rcvd_exit_signal)(
        Channel *chan, ptrlen signame, int core_dumped, ptrlen msg);
    int (*rcvd_exit_signal_numeric)(
        Channel *chan, int signum, int core_dumped, ptrlen msg);

    /* A method for signalling success/failure responses to channel
     * requests initiated from the SshChannel vtable with want_reply
     * true. */
    void (*request_response)(Channel *, int success);
};

struct Channel {
    const struct ChannelVtable *vt;
    unsigned initial_fixed_window_size;
};

#define chan_free(ch) ((ch)->vt->free(ch))
#define chan_open_confirmation(ch) ((ch)->vt->open_confirmation(ch))
#define chan_open_failed(ch, err) ((ch)->vt->open_failed(ch, err))
#define chan_send(ch, err, buf, len) ((ch)->vt->send(ch, err, buf, len))
#define chan_send_eof(ch) ((ch)->vt->send_eof(ch))
#define chan_set_input_wanted(ch, wanted) \
    ((ch)->vt->set_input_wanted(ch, wanted))
#define chan_log_close_msg(ch) ((ch)->vt->log_close_msg(ch))
#define chan_want_close(ch, leof, reof) ((ch)->vt->want_close(ch, leof, reof))
#define chan_rcvd_exit_status(ch, status) \
    ((ch)->vt->rcvd_exit_status(ch, status))
#define chan_rcvd_exit_signal(ch, sig, core, msg)   \
    ((ch)->vt->rcvd_exit_signal(ch, sig, core, msg))
#define chan_rcvd_exit_signal_numeric(ch, sig, core, msg)   \
    ((ch)->vt->rcvd_exit_signal_numeric(ch, sig, core, msg))
#define chan_request_response(ch, success)   \
    ((ch)->vt->request_response(ch, success))

/*
 * Reusable methods you can put in vtables to give default handling of
 * some of those functions.
 */

/* open_confirmation / open_failed for any channel it doesn't apply to */
void chan_remotely_opened_confirmation(Channel *chan);
void chan_remotely_opened_failure(Channel *chan, const char *errtext);

/* want_close for any channel that wants the default behaviour of not
 * closing until both directions have had an EOF */
int chan_no_eager_close(Channel *, int, int);

/* default implementations that refuse all the channel requests */
int chan_no_exit_status(Channel *, int);
int chan_no_exit_signal(Channel *, ptrlen, int, ptrlen);
int chan_no_exit_signal_numeric(Channel *, int, int, ptrlen);

/* default implementation that never expects to receive a response */
void chan_no_request_response(Channel *, int);

/*
 * Constructor for a trivial do-nothing implementation of
 * ChannelVtable. Used for 'zombie' channels, i.e. channels whose
 * proper local source of data has been shut down or otherwise stopped
 * existing, but the SSH side is still there and needs some kind of a
 * Channel implementation to talk to. In particular, the want_close
 * method for this channel always returns 'yes, please close this
 * channel asap', regardless of whether local and/or remote EOF have
 * been sent - indeed, even if _neither_ has.
 */
Channel *zombiechan_new(void);

/* ----------------------------------------------------------------------
 * This structure is owned by an SSH connection layer, and identifies
 * the connection layer's end of the channel, for the Channel
 * implementation to talk back to.
 */

struct SshChannelVtable {
    int (*write)(SshChannel *c, const void *, int);
    void (*write_eof)(SshChannel *c);
    void (*unclean_close)(SshChannel *c, const char *err);
    void (*unthrottle)(SshChannel *c, int bufsize);
    Conf *(*get_conf)(SshChannel *c);
    void (*window_override_removed)(SshChannel *c);
    void (*x11_sharing_handover)(SshChannel *c,
                                 ssh_sharing_connstate *share_cs,
                                 share_channel *share_chan,
                                 const char *peer_addr, int peer_port,
                                 int endian, int protomajor, int protominor,
                                 const void *initial_data, int initial_len);

    /*
     * All the outgoing channel requests we support. Each one has a
     * want_reply flag, which will cause a callback to
     * chan_request_response when the result is available.
     *
     * The ones that return 'int' use it to indicate that the SSH
     * protocol in use doesn't support this request at all.
     *
     * (It's also intentional that not all of them have a want_reply
     * flag: the ones that don't are because SSH-1 has no method for
     * signalling success or failure of that request, or because we
     * wouldn't do anything usefully different with the reply in any
     * case.)
     */
    void (*request_x11_forwarding)(
        SshChannel *c, int want_reply, const char *authproto,
        const char *authdata, int screen_number, int oneshot);
    void (*request_agent_forwarding)(
        SshChannel *c, int want_reply);
    void (*request_pty)(
        SshChannel *c, int want_reply, Conf *conf, int w, int h);
    int (*send_env_var)(
        SshChannel *c, int want_reply, const char *var, const char *value);
    void (*start_shell)(
        SshChannel *c, int want_reply);
    void (*start_command)(
        SshChannel *c, int want_reply, const char *command);
    int (*start_subsystem)(
        SshChannel *c, int want_reply, const char *subsystem);
    int (*send_serial_break)(
        SshChannel *c, int want_reply, int length); /* length=0 for default */
    int (*send_signal)(
        SshChannel *c, int want_reply, const char *signame);
    void (*send_terminal_size_change)(
        SshChannel *c, int w, int h);
    void (*hint_channel_is_simple)(SshChannel *c);
};

struct SshChannel {
    const struct SshChannelVtable *vt;
    ConnectionLayer *cl;
};

#define sshfwd_write(c, buf, len) ((c)->vt->write(c, buf, len))
#define sshfwd_write_eof(c) ((c)->vt->write_eof(c))
#define sshfwd_unclean_close(c, err) ((c)->vt->unclean_close(c, err))
#define sshfwd_unthrottle(c, bufsize) ((c)->vt->unthrottle(c, bufsize))
#define sshfwd_get_conf(c) ((c)->vt->get_conf(c))
#define sshfwd_window_override_removed(c) ((c)->vt->window_override_removed(c))
#define sshfwd_x11_sharing_handover(c, cs, ch, pa, pp, e, pmaj, pmin, d, l) \
    ((c)->vt->x11_sharing_handover(c, cs, ch, pa, pp, e, pmaj, pmin, d, l))
#define sshfwd_request_x11_forwarding(c, wr, ap, ad, scr, oneshot) \
    ((c)->vt->request_x11_forwarding(c, wr, ap, ad, scr, oneshot))
#define sshfwd_request_agent_forwarding(c, wr) \
    ((c)->vt->request_agent_forwarding(c, wr))
#define sshfwd_request_pty(c, wr, conf, w, h) \
    ((c)->vt->request_pty(c, wr, conf, w, h))
#define sshfwd_send_env_var(c, wr, var, value) \
    ((c)->vt->send_env_var(c, wr, var, value))
#define sshfwd_start_shell(c, wr) \
    ((c)->vt->start_shell(c, wr))
#define sshfwd_start_command(c, wr, cmd) \
    ((c)->vt->start_command(c, wr, cmd))
#define sshfwd_start_subsystem(c, wr, subsys) \
    ((c)->vt->start_subsystem(c, wr, subsys))
#define sshfwd_send_serial_break(c, wr, length) \
    ((c)->vt->send_serial_break(c, wr, length))
#define sshfwd_send_signal(c, wr, sig) \
    ((c)->vt->send_signal(c, wr, sig))
#define sshfwd_send_terminal_size_change(c, w, h) \
    ((c)->vt->send_terminal_size_change(c, w, h))
#define sshfwd_hint_channel_is_simple(c) \
    ((c)->vt->hint_channel_is_simple(c))

/* ----------------------------------------------------------------------
 * The 'main' or primary channel of the SSH connection is special,
 * because it's the one that's connected directly to parts of the
 * frontend such as the terminal and the specials menu. So it exposes
 * a richer API.
 */

mainchan *mainchan_new(
    PacketProtocolLayer *ppl, ConnectionLayer *cl, Conf *conf,
    int term_width, int term_height, int is_simple, SshChannel **sc_out);
void mainchan_get_specials(
    mainchan *mc, add_special_fn_t add_special, void *ctx);
void mainchan_special_cmd(mainchan *mc, SessionSpecialCode code, int arg);
void mainchan_terminal_size(mainchan *mc, int width, int height);

#endif /* PUTTY_SSHCHAN_H */
