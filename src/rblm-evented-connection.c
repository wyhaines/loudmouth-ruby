#include "rblm.h"
#include "rblm-private.h"
#include "rblm-synchronizer.h"
#include "rblm-callback.h"
#include <string.h>
#include <errno.h>

VALUE lm_cEventedConnection;

static VALUE Copen_block;
static VALUE Cauth_block;
static VALUE Cdisconnect_block;
static VALUE Chandler_blocks;
static VALUE Csend_blocks;

static VALUE Cempty_block;

static VALUE ev_conn_set_server (VALUE self, VALUE server);
static VALUE _do_send_with_reply (VALUE self, LmConnection *conn, LmMessage *msg, VALUE block);

LmConnection *
rb_lm_ev_connection_from_ruby_object (VALUE obj)
{
	LmConnection *conn;

	if (!rb_lm__is_kind_of (obj, lm_cEventedConnection)) {
		rb_raise (rb_eTypeError, "not a LmConnection");
	}

	Data_Get_Struct (obj, LmConnection, conn);

	return conn;
}

static void
ev_conn_free (LmConnection *self)
{
    LM_CALL (lm_connection_unref (self));
}

static VALUE
ev_conn_allocate (VALUE klass)
{

    return Data_Wrap_Struct (klass, NULL, ev_conn_free, NULL);
}

static VALUE
ev_conn_initialize (int argc, VALUE *argv, VALUE self)
{
    LmConnection *conn;
    VALUE         server;

    /* Initialize some static VALUE's which will point at stuff that will be
       accessed repeatedly. */

    Copen_block       = rb_intern ("@open_block");
    Cauth_block       = rb_intern ("@auth_block");
    Cdisconnect_block = rb_intern ("@disconnect_block");
    Chandler_blocks   = rb_intern ("@handler_blocks");
    Csend_blocks      = rb_intern ("@send_blocks");

    /* These data structures will track the blocks that are in use so that they
       don't get prematurey garbage collected. */

    rb_ivar_set (self, Copen_block,       Qnil);
    rb_ivar_set (self, Cauth_block,       Qnil);
    rb_ivar_set (self, Cdisconnect_block, Qnil);
    rb_ivar_set (self, Chandler_blocks,   rb_hash_new());
    rb_ivar_set (self, Csend_blocks,      rb_hash_new());

    rb_scan_args (argc, argv, "01", &server);

    LM_CALL2 (lm_connection_new_with_context (NULL, g_main_context_default()), conn);

    DATA_PTR (self) = conn;

    if (!NIL_P (server)) {
        ev_conn_set_server (self, server);
    }

    return self;
}

static VALUE
ev_conn_open (int argc, VALUE *argv, VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);
    VALUE         func;

    rb_scan_args (argc, argv, "0&", &func);
    if (NIL_P (func)) func = Cempty_block; /* Replace current handler with an empty one */

    rb_ivar_set (self, Copen_block, func);
    gboolean res;
    GError* error =  NULL;
    LM_CALL2 (lm_connection_open (conn,
                                  open_handler,
                                  (gpointer) func, /* user_data */
                                  NULL,            /* notify    */
                                  &error),         /* error     */
              res);
    if (error)
    {
        g_warning ("Could not open connection: %d, %d, %s", error->domain, error->code, error->message);
        g_error_free (error);
    }
    return GBOOL2RVAL (res);
}

static VALUE
ev_conn_close (VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    gboolean res;
    LM_CALL2 (lm_connection_close (conn, NULL), res);
    return GBOOL2RVAL (res);
}

static VALUE
ev_conn_auth (int argc, VALUE *argv, VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);
    VALUE         name, password, resource, func;

    rb_scan_args (argc, argv, "21&", &name, &password, &resource, &func);
    if (NIL_P (func)) func = Cempty_block; /* Replace current handler with an empty one */

    rb_ivar_set(self,Cauth_block,func);
    gboolean res;
    GError* error = NULL;
    LM_CALL2 (lm_connection_authenticate (conn,
                                          StringValuePtr (name),
                                          StringValuePtr (password),
                                          StringValuePtr (resource),
                                          auth_handler,
                                          (gpointer) func, /* user_data */
                                          NULL,            /* notify    */
                                          &error),         /* error     */
               res);
    if (error)
    {
        g_warning ("Authentication failed: %s", strerror (errno));
        g_error_free (error);
    }
    return GBOOL2RVAL (res);
}

static VALUE
ev_conn_auth_and_block (int argc, VALUE *argv, VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);
    VALUE         name, password, resource;

    rb_scan_args (argc, argv, "21", &name, &password, &resource);

    gboolean res;
    GError* error = NULL;
    LM_CALL2 (lm_connection_authenticate_and_block (conn,
                                                    StringValuePtr (name),
                                                    StringValuePtr (password),
                                                    StringValuePtr (resource),
                                                    &error),
              res);
    if (error)
    {
        g_warning ("Could not authenticate: %s\n", strerror (errno));
        g_error_free (error);
    }
    return GBOOL2RVAL (res);
}

static VALUE
ev_conn_set_keep_alive_rate (VALUE self, VALUE rate)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    LM_CALL (lm_connection_set_keep_alive_rate (conn, NUM2UINT (rate)));

    return Qnil;
}

/*static VALUE
ev_conn_get_keep_alive_rate (VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    guint rate = 0;
    LM_CALL2 (lm_connection_get_keep_alive_rate (conn), rate);

    return UINT2NUM (rate);
}*/

static VALUE
ev_conn_is_open (VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    gboolean res;
    LM_CALL2 (lm_connection_is_open (conn), res);
    return GBOOL2RVAL (res);
}

static VALUE
ev_conn_is_authenticated (VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    gboolean res;
    LM_CALL2 (lm_connection_is_authenticated (conn), res);
    return GBOOL2RVAL (res);
}

static VALUE
ev_conn_get_server (VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    const gchar* server = NULL;
    LM_CALL2 (lm_connection_get_server (conn), server)
    return rb_str_new2 (server);
}

static VALUE
ev_conn_set_server (VALUE self, VALUE server)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    if (!rb_respond_to (server, rb_intern ("to_s"))) {
        rb_raise (rb_eArgError, "server should respond to to_s");
    } else {
        VALUE str_val = rb_funcall (server, rb_intern ("to_s"), 0);
        LM_CALL (lm_connection_set_server (conn, StringValuePtr (str_val)));
    }

    return Qnil;
}

static VALUE
ev_conn_get_jid (VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    const gchar* jid = NULL;
    LM_CALL2 (lm_connection_get_jid (conn), jid)
    return rb_str_new2 (jid);
}

static VALUE
ev_conn_set_jid (VALUE self, VALUE jid)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    if (!rb_respond_to (jid, rb_intern ("to_s"))) {
        rb_raise (rb_eArgError, "jid should respond to to_s");
    } else {
        VALUE str_val = rb_funcall (jid, rb_intern ("to_s"), 0);
        LM_CALL (lm_connection_set_jid (conn, StringValuePtr (str_val)));
    }

    return Qnil;
}

static VALUE
ev_conn_get_port (VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    guint port = 0;
    LM_CALL2 (lm_connection_get_port (conn), port)
    return UINT2NUM (port);
}

static VALUE
ev_conn_set_port (VALUE self, VALUE port)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    LM_CALL (lm_connection_set_port (conn, NUM2UINT (port)));

    return Qnil;
}

static VALUE
ev_conn_get_ssl (VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    LmSSL * ssl = NULL;
    LM_CALL2 (lm_connection_get_ssl (conn), ssl);
    return LMEVENTEDSSL2RVAL (ssl);
}

static VALUE
ev_conn_set_ssl (VALUE self, VALUE ssl_rval)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);
    LmSSL        *ssl  = rb_lm_ev_ssl_from_ruby_object (ssl_rval);

    LM_CALL (lm_connection_set_ssl (conn, ssl));

    return Qnil;
}

static VALUE
ev_conn_get_proxy (VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    LmProxy * proxy = NULL;
    LM_CALL2 (lm_connection_get_proxy (conn), proxy);
    return LMPROXY2RVAL (proxy);
}

static VALUE
ev_conn_set_proxy (VALUE self, VALUE proxy_rval)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);
    LmProxy      *proxy = rb_lm_proxy_from_ruby_object (proxy_rval);

    LM_CALL (lm_connection_set_proxy (conn, proxy));

    return Qnil;
}

static VALUE
ev_conn_set_disconnect_handler (int argc, VALUE *argv, VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);
    VALUE         func;

    rb_scan_args (argc, argv, "0&", &func);
    if (NIL_P (func)) func = Cempty_block; /* Replace current handler with an empty one */

    rb_ivar_set (self, Cdisconnect_block, func);
    LM_CALL (lm_connection_set_disconnect_function (
                conn,               /* connection */
                disconnect_handler, /* function   */
                (gpointer) func,    /* user_data  */
                NULL));             /* notify     */
}

/* TODO: Make this function check if an LmMessage or text is passed and use the proper lm_connection_send/lm_connection_send_raw function. */
static VALUE
ev_conn_send (int argc, VALUE *argv, VALUE self)
{
    VALUE msg, block;

    rb_scan_args(argc, argv, "1&", &msg, &block);

    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);
    LmMessage    *m = rb_lm_message_from_ruby_object (msg);

    if (!NIL_P(block)) {
        return _do_send_with_reply(self,conn,m,block);
    } else {
        GError* error = NULL;
        gboolean res;
        LM_CALL2 (lm_connection_send (conn, m, &error), res);
        if (error)
        {
            g_warning ("Could not send message: %s\n", strerror (errno));
            g_error_free (error);
        }
        return GBOOL2RVAL (res);
    }
}

static VALUE
ev_conn_send_with_reply (int argc, VALUE *argv, VALUE self)
{
    VALUE msg, block;

    rb_scan_args(argc, argv, "1&", &msg, &block);

    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);
    LmMessage    *m = rb_lm_message_from_ruby_object (msg);

    if (NIL_P (block)) block = Cempty_block; /* Replace current handler with an empty one */

    return _do_send_with_reply(self,conn,m,block);
}

static VALUE
_do_send_with_reply (VALUE self, LmConnection *conn, LmMessage *msg, VALUE block)
{
    LmMessageHandler *handler;
    VALUE *data;

    data = malloc(2*sizeof(VALUE));
    data[0] = block;
    data[1] = self;

    rb_hash_aset (rb_ivar_get (self, Csend_blocks), self,block);

    LM_CALL2 (lm_message_handler_new (
                   (LmHandleMessageFunction)reply_handler,   /* function  */
                                            (gpointer) data, /* user_data */
                                            NULL),           /* notify    */
              handler);

    GError* error = NULL;
    LM_CALL (lm_connection_send_with_reply (conn,     /* connection */
                                            msg,      /* message    */
                                            handler,  /* handler    */
                                            &error)); /* error      */
    if (error)
    {
        g_warning ("Could not send message: %s\n", strerror (errno));
        g_error_free (error);
    }

    return Qtrue;
}

static VALUE
ev_conn_get_state (VALUE self)
{
    LmConnection *conn = rb_lm_ev_connection_from_ruby_object (self);

    LmConnectionState state;
    LM_CALL2 (lm_connection_get_state (conn), state);
    return INT2FIX (state);
}

static VALUE
ev_conn_add_msg_handler (int argc, VALUE *argv, VALUE self)
{
    LmConnection     *conn = rb_lm_ev_connection_from_ruby_object (self);
    VALUE             type, func;
    LmMessageHandler *handler;

    rb_scan_args (argc, argv, "1&", &type, &func);
    if (NIL_P (func)) func = Cempty_block; /* Replace current handler with an empty one */

    LM_CALL2 (lm_message_handler_new (msg_handler,    /* function  */
                                      (gpointer)func, /* user_data */
                                      NULL),          /* notify    */
              handler);

    rb_hash_aset (rb_ivar_get (self, Chandler_blocks), type, func);

    LM_CALL (lm_connection_register_message_handler (
                conn,                                       /* connection */
                handler,                                    /* handler    */
                rb_lm_message_type_from_ruby_object (type), /* type       */
                LM_HANDLER_PRIORITY_NORMAL));               /* priority   */

    LM_CALL (lm_message_handler_unref (handler));

    return Qnil;
}

void
Init_lm_evented_connection (VALUE lm_mLM)
{
    lm_cEventedConnection = rb_define_class_under (lm_mLM, "EventedConnection",
                        rb_cObject);

    rb_define_alloc_func (lm_cEventedConnection, ev_conn_allocate);

    rb_define_method (lm_cEventedConnection, "initialize", ev_conn_initialize, -1);
    rb_define_method (lm_cEventedConnection, "open", ev_conn_open, -1);
    rb_define_method (lm_cEventedConnection, "close", ev_conn_close, 0);
    rb_define_method (lm_cEventedConnection, "authenticate", ev_conn_auth, -1);
    rb_define_method (lm_cEventedConnection, "authenticate_and_block", ev_conn_auth_and_block, -1);
    rb_define_method (lm_cEventedConnection, "keep_alive_rate=", ev_conn_set_keep_alive_rate, 1);
    /*rb_define_method (lm_cEventedConnection, "keep_alive_rate", ev_conn_get_keep_alive_rate, 0);*/
    rb_define_method (lm_cEventedConnection, "open?", ev_conn_is_open, 0);
    rb_define_method (lm_cEventedConnection, "authenticated?", ev_conn_is_authenticated, 0);
    rb_define_method (lm_cEventedConnection, "server", ev_conn_get_server, 0);
    rb_define_method (lm_cEventedConnection, "server=", ev_conn_set_server, 1);
    rb_define_method (lm_cEventedConnection, "jid", ev_conn_get_jid, 0);
    rb_define_method (lm_cEventedConnection, "jid=", ev_conn_set_jid, 1);
    rb_define_method (lm_cEventedConnection, "port", ev_conn_get_port, 0);
    rb_define_method (lm_cEventedConnection, "port=", ev_conn_set_port, 1);

    rb_define_method (lm_cEventedConnection, "ssl", ev_conn_get_ssl, 0);
    rb_define_method (lm_cEventedConnection, "ssl=", ev_conn_set_ssl, 1);
    rb_define_method (lm_cEventedConnection, "proxy", ev_conn_get_proxy, 0);
    rb_define_method (lm_cEventedConnection, "proxy=", ev_conn_set_proxy, 1);

    rb_define_method (lm_cEventedConnection, "set_disconnect_handler", ev_conn_set_disconnect_handler, -1);

    /* Use one send message and check if there is a block passed? */
    rb_define_method (lm_cEventedConnection, "send", ev_conn_send, -1);

    rb_define_method (lm_cEventedConnection, "send_with_reply", ev_conn_send_with_reply, -1);
/*
    rb_define_method (lm_cEventedConnection, "send_raw", ev_conn_send_raw, 1);
    */

    rb_define_method (lm_cEventedConnection, "state", ev_conn_get_state, 0);
    rb_define_method (lm_cEventedConnection, "add_message_handler", ev_conn_add_msg_handler, -1);

    //Cempty_block = rb_class_new_instance (0, NULL, rb_cProc);
}
