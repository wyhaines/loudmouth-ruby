/* Synchronization between Glib and Ruby threads */

#include "rblm-synchronizer.h"
#include "rblm-callback.h"
#include <errno.h>
#include <string.h>

/* GLib event loop thread */
static GThread* glib_thread = NULL;

/* GLib main loop */
static GMainLoop* main_loop = NULL;

/* Was GLib thread started? */
static gboolean glib_started = FALSE;

/* Startup synchronization condition variable */
GCond* event_loop_started = NULL;
GMutex* event_loop_started_mx = NULL;

/* Condition variables used to pause/resume GLib event loop */
GCond* loop_paused = NULL;
GCond* loop_resumed = NULL;
GMutex* cond_mx = NULL;

/* Pipes channels used for synchronization */
GIOChannel* rb2lm_read = NULL;
GIOChannel* rb2lm_write = NULL;
GIOChannel* lm2rb_read = NULL;
GIOChannel* lm2rb_write = NULL;

/* Async message queue used to forward LM events to ruby */
GAsyncQueue* lm2rb_queue = NULL;

/* Pipe notification token */
static gchar g_token = '1';

/* Was GLib event loop thread started? */
gboolean
rblm_sync_started() {
    return glib_started;
}

/* Helper method to create pipe IO channels */
void
rblm_create_pipe(GIOChannel** ch_read, GIOChannel** ch_write) {
    int fd[2], ret;
    ret = pipe(fd);
    if (ret == -1) {
        g_warning("Creating pipe failed: error %s\n", strerror(errno));
        return;
    }
    if (ch_read) {
        *ch_read = g_io_channel_unix_new(fd[0]);
        g_io_channel_set_encoding(*ch_read, NULL, NULL);
        g_io_channel_set_close_on_unref(*ch_read, TRUE);
        g_io_channel_set_buffered(*ch_read, FALSE);
    }
    if (ch_write) {
        *ch_write = g_io_channel_unix_new(fd[1]);
        g_io_channel_set_encoding(*ch_write, NULL, NULL);
        g_io_channel_set_close_on_unref(*ch_write, TRUE);
        g_io_channel_set_buffered(*ch_write, FALSE);
    }
}

/* Handle notification coming from ruby thread */
static gboolean
rb2lm_notify (GIOChannel* source, GIOCondition cond, gpointer _)
{
    g_mutex_lock (cond_mx);

    static gchar buf[1];
    GError* error = NULL;
    gboolean res = TRUE;

    g_io_channel_read_chars (source, buf, 1, NULL, &error);
    if (cond & G_IO_HUP)
    {
        g_warning ("Read end of ruby to GLib pipe broken");
        res = FALSE;
    }
    else
    {
        g_cond_signal (loop_paused);
        g_cond_wait (loop_resumed, cond_mx);
    }

    g_mutex_unlock (cond_mx);
    return res;
}

/* Notify ruby that main loop is started */
static gboolean
main_loop_started (gpointer _)
{
    g_cond_signal (event_loop_started);
    return FALSE; /* only run once */
}

/* GLib event loop thread function */
static gpointer
loop_thread(gpointer _) {
    loop_paused = g_cond_new();
    loop_resumed = g_cond_new();
    cond_mx = g_mutex_new();

    (void)g_async_queue_ref (lm2rb_queue);

    if (!g_io_add_watch (rb2lm_read, G_IO_IN | G_IO_HUP, rb2lm_notify, NULL))
        g_error ("Failed to add watch on IO Channel");
    if (!g_timeout_add (10, main_loop_started, NULL))
        g_error ("Failed to add event loop start notification");

    main_loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run(main_loop);

    g_async_queue_unref (lm2rb_queue);

    return NULL;
}

/* Initialize queues, pipes and the GLib event loop, call from ruby thread */
void
rblm_init_sync()
{
    if (!glib_started)
    {
        g_thread_init(NULL);
        event_loop_started = g_cond_new();
        event_loop_started_mx = g_mutex_new();

        rblm_create_pipe(&rb2lm_read, &rb2lm_write);
        rblm_create_pipe(&lm2rb_read, &lm2rb_write);

        lm2rb_queue = g_async_queue_new();

        GError* error = NULL;
        glib_thread = g_thread_create((GThreadFunc) &loop_thread, /* func     */
                                      NULL,                       /* data     */
                                      TRUE,                       /* joinable */
                                      &error);                    /* error    */
        if (error) {
            g_error("Could not start GLib thread: %s\n", error->message);
            g_error_free(error);
        }

        /* Wait until main loop is running */
        g_cond_wait (event_loop_started, event_loop_started_mx);
        glib_started = TRUE;
    }
}

/* Shut down synchronization layer, call from ruby thread */
void
rblm_shutdown_sync() {
    g_main_loop_quit(main_loop);
    g_thread_join(glib_thread);
    g_io_channel_shutdown(lm2rb_read, FALSE, NULL);
    g_io_channel_shutdown(lm2rb_write, FALSE, NULL);
    g_io_channel_shutdown(rb2lm_read, FALSE, NULL);
    g_io_channel_shutdown(rb2lm_write, FALSE, NULL);
    g_io_channel_unref(lm2rb_read);
    g_io_channel_unref(lm2rb_write);
    g_io_channel_unref(rb2lm_read);
    g_io_channel_unref(rb2lm_write);
    g_async_queue_unref (lm2rb_queue);
}

/* Trigger event in GLib event loop that will wait for ruby thread */
void
rb2lm_pause_glib()
{
    if (rblm_sync_started())
    {
        g_mutex_lock (cond_mx);

        GError* error = NULL;
        g_io_channel_write_chars (rb2lm_write, &g_token, 1, NULL, &error);
        if (error)
        {
          g_warning ("Failed to write into Ruby to Loudmouth pipe: %s\n", error->message);
          g_error_free (error);
        }
        g_cond_wait (loop_paused, cond_mx);

        g_mutex_unlock (cond_mx);
    }
}

/* Tell Glib we're done */
void
rb2lm_resume_glib()
{
    if (rblm_sync_started())
        g_cond_signal (loop_resumed);
}

/* Notify ruby of message callback:  *
 *    1. Push message to async queue *
 *    2. Notify ruby through pipe    */
static void
notify_ruby (LmAsyncNotification notification,
             VALUE block,
             gpointer data)
{
    gsize written;
    GError* error = NULL;
    LmAsyncCallback* cb = create_async_message (notification, block, data);
    g_async_queue_push (lm2rb_queue, (gpointer)cb);
    if (g_io_channel_write_chars (lm2rb_write, &g_token, 1, &written, &error) == G_IO_STATUS_ERROR)
    {
        g_error ("Failed to write to Loudmouth to Ruby pipe: %s\n", error->message);
        g_error_free (error);
    }
}

/* Handlers that get called back by Loudmouth in GLib thread */
LmHandlerResult
msg_handler (LmMessageHandler *handler,
             LmConnection     *connection,
             LmMessage        *message,
             gpointer          user_data)
{
    notify_ruby (LM_CB_MSG, (VALUE)user_data, MSG2GPOINTER (lm_message_ref (message)));

    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

LmHandlerResult
reply_handler (LmMessageHandler *handler,
               LmConnection *connection,
               LmMessage *message,
               gpointer *user_data)
{
    notify_ruby (LM_CB_REPLY, (VALUE)user_data, MSG2GPOINTER (message));

    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

void open_handler (LmConnection *conn,
                   gboolean success,
                   gpointer user_data)
{
    notify_ruby (LM_CB_CONN_OPEN, (VALUE)user_data, GBOOL2GPOINTER (success));
}

void auth_handler (LmConnection *conn,
                   gboolean success,
                   gpointer user_data)
{
    notify_ruby (LM_CB_AUTH, (VALUE)user_data, GBOOL2GPOINTER (success));
}

void disconnect_handler (LmConnection *conn,
                         LmDisconnectReason  reason,
                         gpointer user_data)
{
    notify_ruby (LM_CB_CONN_OPEN, (VALUE)user_data, DISCONNECT2GPOINTER (reason));
}

LmSSLResponse ssl_handler (LmSSL *ssl, LmSSLStatus status, gpointer user_data)
{
    notify_ruby (LM_CB_SSL, (VALUE)user_data, SSLSTATUS2GPOINTER (status));

    /* TODO: We need to get this from ruby, use a blocking pipe read? */
    return LM_SSL_RESPONSE_CONTINUE;
}
