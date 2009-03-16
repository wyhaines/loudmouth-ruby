/* Synchronizes the GLib and Ruby threads using an async queue and pipes.
 * 
 * Ruby to GLib thread synchronization
 *
 * Calls from the ruby code into Loudmouth must run whille the GLib event loop 
 * is stopped to avoid concurrent calls accessing GLib data structures with no
 * synchronization. This can be done by triggering an event through a pipe. 
 * The event handler should then signal the ruby thread that the event loop is
 * paused. The ruby thread can then proceed with calling the Loudmouth API. 
 * Once the Loudmouth API call returns the ruby thread can signal back the 
 * GLib thread and tell it to resume the event loop.
 * Ruby methods that need synchronization include all calls to Loudmouth
 * functions that access data also accessed by GLib events, including:
 *   - all calls to LM::Connection
 *
 * GLib to Ruby Synchronization
 *
 * The other way around requires an actual context switch: the event triggered
 * by GLib must run in the ruby thread. To achieve this, the GLib event posts a
 * message to an asynchronous queue and writes to a pipe to let the ruby thread
 * know that there is a message for it. The ruby thread then picks up the
 * message and dispatches it to the right event handler.
 * Loudmouth/GLib events that need to be sent to the Ruby thread are:
 *   - new message notifications (msg_handler_cb)
 *   - reply notifications (msg_handler_for_send_cb)
 *   - connection open notifications (open_callback)
 *   - authentication notifications (auth_callback)
 *   - disconnection notifications (disconnect_cb)
 *   - SSL notifications (ssl_function_callback)
 *
 * Packaging
 *
 * The Loudmouth ruby binding supports both blocking and threaded behaviors.
 */

#ifndef _RBLM_SYNCHRONIZER_H
#define	_RBLM_SYNCHRONIZER_H

#include "rblm.h"
#include <loudmouth/loudmouth.h>

/* Call Loudmouth API with no return value from ruby thread */
#define LM_CALL(func) {                      \
                        rb2lm_pause_glib();  \
                        (func);              \
                        rb2lm_resume_glib(); \
                      }

/* Call Loudmouth API with return value from ruby thread */
#define LM_CALL2(func, res) {                      \
                              rb2lm_pause_glib();  \
                              res = (func);        \
                              rb2lm_resume_glib(); \
                            }

/* Pipes used to notify of pending queue items */
extern GIOChannel* rb2lm_read;
extern GIOChannel* rb2lm_write;
extern GIOChannel* lm2rb_read;
extern GIOChannel* lm2rb_write;

/* Asynchronous Message queue */
extern GAsyncQueue* lm2rb_queue;

/* Initialize queues, pipes and the GLib event loop, call from ruby thread */
void rblm_init_sync();

/* Shut down synchronization layer, call from ruby thread */
void rblm_shutdown_sync();

/* Was the GLib thread started? (i.e. is synchronization necessary? */
gboolean rblm_sync_started();

/* Helper to create pipe and its channels */
void rblm_create_pipe (GIOChannel** ch_read, GIOChannel** ch_write);

/* 'Pause' GLib */
void rb2lm_pause_glib();

/* 'Resume' GLib */
void rb2lm_resume_glib();

/* Loudmouth event handlers */
LmHandlerResult msg_handler (LmMessageHandler *handler, LmConnection *connection, LmMessage *message, gpointer user_data);
LmHandlerResult reply_handler (LmMessageHandler *handler, LmConnection *connection, LmMessage *message, gpointer *user_data);
void open_handler (LmConnection *conn, gboolean success, gpointer user_data);
void auth_handler (LmConnection *conn, gboolean success, gpointer user_data);
void disconnect_handler (LmConnection *conn, LmDisconnectReason  reason, gpointer user_data);
LmSSLResponse ssl_handler (LmSSL *ssl, LmSSLStatus  status, gpointer user_data);

/* Event packaging */
#define MSG2GPOINTER(m)        ((gpointer)(m))
#define GPOINTER2MSG(p)        ((LmMessage*)(p))
#define GBOOL2GPOINTER(b)      ((b == TRUE) ? (void*)1 : NULL)
#define GPOINTER2GBOOL(p)      (p ? TRUE : FALSE)
#define DISCONNECT2GPOINTER(d) ((gpointer)(d))
#define GPOINTER2DISCONNECT(p) ((LmDisconnectReason)(p))
#define SSLSTATUS2GPOINTER(s)  ((gpointer)(s))
#define GPOINTER2SSLSTATUS(p)  ((LmSSLStatus)(p))

#endif	/* _RBLM_SYNCHRONIZER_H */

