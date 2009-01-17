#include "rblm.h"
#include "rblm-private.h"

VALUE lm_cConnection;

static VALUE Copen_block;
static VALUE Cauth_block;
static VALUE Cdisconnect_block;
static VALUE Chandler_blocks;
static VALUE Csend_blocks;

VALUE conn_set_server (VALUE self, VALUE server);
VALUE _do_send_with_reply (VALUE self, LmConnection *conn, LmMessage *msg, VALUE block);
static LmHandlerResult
msg_handler_cb (LmMessageHandler *handler,
		LmConnection     *connection,
		LmMessage        *message,
		gpointer          user_data);

static LmHandlerResult
msg_handler_for_send_cb (LmMessageHandler *handler,
		LmConnection     *connection,
		LmMessage        *message,
		gpointer         *user_data);

/* -- START of GMainContext hack -- 
 * This is a hack to get the GMainContext from a ruby VALUE, this will break if
 * internals change in Ruby/GLib.
 */
typedef struct {
    gpointer boxed;
    gboolean own;
    gsize    type;
} boxed_holder;

static GMainContext *
rb_lm_hack_get_main_context_from_rval (VALUE ctx_rval)
{
	boxed_holder *holder;

	Data_Get_Struct (ctx_rval, boxed_holder, holder);

	return holder->boxed;
}
/* -- END of GMainContext hack -- */

LmConnection *
rb_lm_connection_from_ruby_object (VALUE obj)
{
	LmConnection *conn;

	if (!rb_lm__is_kind_of (obj, lm_cConnection)) {
		rb_raise (rb_eTypeError, "not a LmConnection");
	}

	Data_Get_Struct (obj, LmConnection, conn);

	return conn;
}

void
conn_free (LmConnection *self)
{
	lm_connection_unref (self);
}

static int
conn_mark_each_handler(VALUE key, VALUE value)
{
printf("conn_mark_each_handler\n");
	if (value != Qnil) {
		rb_gc_mark(value);
	}

	return 0;
}

void
conn_mark (VALUE *conn)
{
	VALUE block, handler_blocks, send_blocks;

	block = rb_ivar_get(*conn, Copen_block);
	if (block != Qnil) {
		rb_gc_mark(block);
	}

	block = rb_ivar_get(*conn, Cauth_block);
	if (block != Qnil) {
		rb_gc_mark(block);
	}

	handler_blocks = rb_ivar_get(*conn, Chandler_blocks);
	send_blocks = rb_ivar_get(*conn, Csend_blocks);
	if (handler_blocks != Qnil)
		rb_hash_foreach(handler_blocks, conn_mark_each_handler, 0);

	if (send_blocks != Qnil)
		rb_hash_foreach(send_blocks, conn_mark_each_handler, 0);
}

VALUE
conn_allocate (VALUE klass)
{
	return Data_Wrap_Struct (klass, conn_mark, conn_free, NULL);
}

VALUE
conn_initialize (int argc, VALUE *argv, VALUE self)
{
	LmConnection *conn;
	char         *srv_str = NULL;
	VALUE         server, context;
	VALUE open_block, auth_block, disconnect_block, handler_blocks, send_blocks;

	Copen_block = rb_intern("@open_block");
	Cauth_block = rb_intern("@auth_block");
	Cdisconnect_block = rb_intern("@disconnect_block");
	Chandler_blocks = rb_intern("@handler_blocks");
	Csend_blocks = rb_intern("@send_blocks");

	rb_ivar_set(self,Copen_block,Qnil);
	rb_ivar_set(self,Cauth_block,Qnil);
	rb_ivar_set(self,Cdisconnect_block,Qnil);
	rb_ivar_set(self,Chandler_blocks,rb_hash_new());
	rb_ivar_set(self,Csend_blocks,rb_hash_new());

	rb_scan_args (argc, argv, "02", &server, &context);

	if (!NIL_P (context)) {
		GMainContext *ctx;

		ctx = rb_lm_hack_get_main_context_from_rval (context);

		conn = lm_connection_new_with_context (NULL, ctx);
	} else {
		conn = lm_connection_new (NULL);
	}

	DATA_PTR (self) = conn;

	if (!NIL_P (server)) {
		conn_set_server (self, server);
	}

	return self;
}

static void
open_callback (LmConnection *conn, gboolean success, gpointer user_data)
{
	rb_funcall((VALUE)user_data, rb_intern ("call"), 1,
		   GBOOL2RVAL (success));
}

VALUE
conn_open (int argc, VALUE *argv, VALUE self)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);
	VALUE         func;

	rb_scan_args (argc, argv, "0&", &func);
	if (NIL_P (func)) {
		func = rb_block_proc ();
	}

	rb_ivar_set(self,Copen_block,func);
	return GBOOL2RVAL (lm_connection_open (conn, open_callback, 
					       (gpointer) func, NULL, NULL));
}

VALUE
conn_close (VALUE self)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);

	return GBOOL2RVAL (lm_connection_close (conn, NULL));
}

static void
auth_callback (LmConnection *conn, gboolean success, gpointer user_data)
{
	rb_funcall((VALUE)user_data, rb_intern ("call"), 1,
		   GBOOL2RVAL (success));
}

VALUE
conn_auth (int argc, VALUE *argv, VALUE self)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);
	VALUE         name, password, resource, func; 

	rb_scan_args (argc, argv, "21&", &name, &password, &resource, &func);
	if (NIL_P (func)) {
		func = rb_block_proc ();
	}

	rb_ivar_set(self,Cauth_block,func);
	return GBOOL2RVAL (lm_connection_authenticate (conn, 
						       StringValuePtr (name),
						       StringValuePtr (password), 
						       StringValuePtr (resource),
						       auth_callback,
						       (gpointer) func, NULL,
						       NULL));
}

VALUE
conn_auth_and_block (int argc, VALUE *argv, VALUE self)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);
	VALUE         name, password, resource; 

	rb_scan_args (argc, argv, "21", &name, &password, &resource);

	return GBOOL2RVAL (lm_connection_authenticate_and_block (conn, 
				StringValuePtr (name),
				StringValuePtr (password), 
				StringValuePtr (resource),
				NULL));
}

VALUE
conn_set_keep_alive_rate (VALUE self, VALUE rate)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);

	lm_connection_set_keep_alive_rate (conn, NUM2UINT (rate));

	return Qnil;
}

/*
 * VALUE
conn_get_keep_alive_rate (VALUE self)
{
	LmConnection *connection;
} */

VALUE
conn_is_open (VALUE self)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);

	return GBOOL2RVAL (lm_connection_is_open (conn));
}

VALUE
conn_is_authenticated (VALUE self)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);

	return GBOOL2RVAL (lm_connection_is_authenticated (conn));
}

VALUE
conn_get_server (VALUE self)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);

	return rb_str_new2 (lm_connection_get_server (conn));
}

VALUE
conn_set_server (VALUE self, VALUE server)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);

	if (!rb_respond_to (server, rb_intern ("to_s"))) {
		rb_raise (rb_eArgError, "server should respond to to_s");
	} else {
		VALUE str_val = rb_funcall (server, rb_intern ("to_s"), 0);
		lm_connection_set_server (conn, StringValuePtr (str_val));
	}

	return Qnil;
}

VALUE
conn_get_jid (VALUE self)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);

	return rb_str_new2 (lm_connection_get_jid (conn));
}

VALUE
conn_set_jid (VALUE self, VALUE jid)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);

	if (!rb_respond_to (jid, rb_intern ("to_s"))) {
		rb_raise (rb_eArgError, "jid should respond to to_s");
	} else {
		VALUE str_val = rb_funcall (jid, rb_intern ("to_s"), 0);
		lm_connection_set_jid (conn, StringValuePtr (str_val));
	}

	return Qnil;
}

VALUE
conn_get_port (VALUE self)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);

	return UINT2NUM (lm_connection_get_port (conn));
}

VALUE
conn_set_port (VALUE self, VALUE port)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);

	lm_connection_set_port (conn, NUM2UINT (port));

	return Qnil;
}

VALUE
conn_get_ssl (VALUE self)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);

	return LMSSL2RVAL (lm_connection_get_ssl (conn));
}

VALUE
conn_set_ssl (VALUE self, VALUE ssl_rval)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);
	LmSSL        *ssl  = rb_lm_ssl_from_ruby_object (ssl_rval);

	lm_connection_set_ssl (conn, ssl);

	return Qnil;
}

VALUE
conn_get_proxy (VALUE self)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);

	return LMPROXY2RVAL (lm_connection_get_proxy (conn));
}

VALUE
conn_set_proxy (VALUE self, VALUE proxy_rval)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);
	LmProxy      *proxy = rb_lm_proxy_from_ruby_object (proxy_rval);

	lm_connection_set_proxy (conn, proxy);

	return Qnil;
}

static void
disconnect_cb (LmConnection       *conn, 
	       LmDisconnectReason  reason, 
	       gpointer            user_data)
{
	rb_funcall((VALUE)user_data, rb_intern ("call"), 1, INT2FIX (reason));
}

VALUE
conn_set_disconnect_handler (int argc, VALUE *argv, VALUE self)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);
	VALUE         func;

	rb_scan_args (argc, argv, "0&", &func);
	if (NIL_P (func)) {
		func = rb_block_proc ();
	}

	rb_ivar_set(self,Cdisconnect_block,func);
	lm_connection_set_disconnect_function (conn, 
					       disconnect_cb,
					       (gpointer) func, NULL);
}

/* TODO: Make this function check if an LmMessage or text is passed and use the proper lm_connection_send/lm_connection_send_raw function. */
VALUE
conn_send (int argc, VALUE *argv, VALUE self)
{
	VALUE msg, block;

	rb_scan_args(argc, argv, "1&", &msg, &block);

	LmConnection *conn = rb_lm_connection_from_ruby_object (self);
	LmMessage    *m = rb_lm_message_from_ruby_object (msg);

	if (!NIL_P(block)) {
		return _do_send_with_reply(self,conn,m,block);
	} else {
		return GBOOL2RVAL (lm_connection_send (conn, m, NULL));
	}
}

VALUE
conn_send_with_reply (int argc, VALUE *argv, VALUE self)
{
	VALUE msg, block;

	rb_scan_args(argc, argv, "1&", &msg, &block);

	LmConnection *conn = rb_lm_connection_from_ruby_object (self);
	LmMessage    *m = rb_lm_message_from_ruby_object (msg);

	if (NIL_P (block)) {
		block = rb_block_proc ();
	}

	return _do_send_with_reply(self,conn,m,block);
}

VALUE
_do_send_with_reply (VALUE self, LmConnection *conn, LmMessage *msg, VALUE block)
{
	LmMessageHandler *handler;
	VALUE *data;

	data = malloc(2*sizeof(VALUE));
	data[0] = block;
	data[1] = self;

	rb_hash_aset(rb_ivar_get(self,Csend_blocks),self,block);
	
	handler = lm_message_handler_new(msg_handler_for_send_cb, (gpointer) data, NULL);
	lm_connection_send_with_reply(conn,msg,handler,NULL);

	return Qtrue;
}

VALUE
conn_get_state (VALUE self)
{
	LmConnection *conn = rb_lm_connection_from_ruby_object (self);

	return INT2FIX (lm_connection_get_state (conn));
}

static LmHandlerResult
msg_handler_cb (LmMessageHandler *handler,
		LmConnection     *connection,
		LmMessage        *message,
		gpointer          user_data)
{
	rb_funcall ((VALUE)user_data, rb_intern ("call"), 1, 
		    LMMESSAGE2RVAL (message));

	return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static LmHandlerResult
msg_handler_for_send_cb (LmMessageHandler *handler,
		LmConnection     *connection,
		LmMessage        *message,
		gpointer         *user_data)
{
	rb_funcall ((VALUE)user_data[0], rb_intern ("call"), 1, 
		    LMMESSAGE2RVAL (message));

	return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

VALUE
conn_add_msg_handler (int argc, VALUE *argv, VALUE self)
{
	LmConnection     *conn = rb_lm_connection_from_ruby_object (self);
	VALUE             type, func;
	LmMessageHandler *handler;

	rb_scan_args (argc, argv, "1&", &type, &func);
	if (NIL_P (func)) {
		func = rb_block_proc ();
	}

	handler = lm_message_handler_new (msg_handler_cb, (gpointer) func, NULL);

	rb_hash_aset(rb_ivar_get(self,Chandler_blocks),type,func);
	lm_connection_register_message_handler (conn, handler,
						rb_lm_message_type_from_ruby_object (type),
						LM_HANDLER_PRIORITY_NORMAL);
	lm_message_handler_unref (handler);

	return Qnil;
}

void
Init_lm_connection (VALUE lm_mLM)
{
	lm_cConnection = rb_define_class_under (lm_mLM, "Connection", 
						rb_cObject);

	rb_define_alloc_func (lm_cConnection, conn_allocate);

	rb_define_method (lm_cConnection, "initialize", conn_initialize, -1);
	rb_define_method (lm_cConnection, "open", conn_open, -1);
	rb_define_method (lm_cConnection, "close", conn_close, 0);
	rb_define_method (lm_cConnection, "authenticate", conn_auth, -1);
	rb_define_method (lm_cConnection, "authenticate_and_block", conn_auth_and_block, -1);
	rb_define_method (lm_cConnection, "keep_alive_rate=", conn_set_keep_alive_rate, 1);
	/* rb_define_method (lm_cConnection, "keep_alive_rate", conn_get_keep_alive_rate, 0); */
	rb_define_method (lm_cConnection, "open?", conn_is_open, 0);
	rb_define_method (lm_cConnection, "authenticated?", conn_is_authenticated, 0);
	rb_define_method (lm_cConnection, "server", conn_get_server, 0);
	rb_define_method (lm_cConnection, "server=", conn_set_server, 1);
	rb_define_method (lm_cConnection, "jid", conn_get_jid, 0);
	rb_define_method (lm_cConnection, "jid=", conn_set_jid, 1);
	rb_define_method (lm_cConnection, "port", conn_get_port, 0);
	rb_define_method (lm_cConnection, "port=", conn_set_port, 1);

	rb_define_method (lm_cConnection, "ssl", conn_get_ssl, 0);
	rb_define_method (lm_cConnection, "ssl=", conn_set_ssl, 1);
	rb_define_method (lm_cConnection, "proxy", conn_get_proxy, 0);
	rb_define_method (lm_cConnection, "proxy=", conn_set_proxy, 1);

	rb_define_method (lm_cConnection, "set_disconnect_handler", conn_set_disconnect_handler, -1);

	/* Use one send message and check if there is a block passed? */
	rb_define_method (lm_cConnection, "send", conn_send, -1);
	
	rb_define_method (lm_cConnection, "send_with_reply", conn_send_with_reply, -1);
/*
	rb_define_method (lm_cConnection, "send_raw", conn_send_raw, 1);
	*/

	rb_define_method (lm_cConnection, "state", conn_get_state, 0);
	rb_define_method (lm_cConnection, "add_message_handler", conn_add_msg_handler, -1);
}
