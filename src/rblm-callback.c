/*
 * Loudmouth to ruby callback
 * Used by ruby code for dispatching to proper handler
 */

#include "rblm-callback.h"
#include "rblm-private.h"
#include "rblm-synchronizer.h"
#include <ruby.h>

/* Ruby callback class */
VALUE lm_cCallback;

/* Create messages to be posted on async queue */
LmAsyncCallback*
create_async_message (LmAsyncNotification notification, VALUE block, gpointer data)
{
    LmAsyncCallback* cb = (LmAsyncCallback*)malloc (sizeof(LmAsyncCallback));
    cb->notification = notification;
    cb->block = block;
    cb->data = data;
    return cb;
}

/* Free underlying async message */
static void
callback_free (LmAsyncCallback* cb)
{
    switch (cb->notification)
    {
        case LM_CB_MSG:
        case LM_CB_REPLY:
        {
            lm_message_unref ((LmMessage*)cb->data);
            break;
        }
        default:
            break;
    }
    free (cb);
}

/* Convert raw async message to ruby object */
VALUE
lm_callback_to_ruby_object (LmAsyncCallback* cb)
{
    if (cb)
        return Data_Wrap_Struct (lm_cCallback, NULL, callback_free, cb);
    else
        return Qnil;
}

static LmAsyncCallback *
rb_lm_callback_from_ruby_object (VALUE obj)
{
    LmAsyncCallback* cb = NULL;

    if (!rb_lm__is_kind_of (obj, lm_cCallback))
        rb_raise (rb_eTypeError, "not a LM::Callback");

    Data_Get_Struct (obj, LmAsyncCallback, cb);

    return cb;
}

static VALUE
callback_get_kind (VALUE self)
{
    LmAsyncCallback* cb = rb_lm_callback_from_ruby_object (self);

    return INT2FIX (cb->notification);
}

static VALUE
callback_get_target (VALUE self)
{
    LmAsyncCallback* cb = rb_lm_callback_from_ruby_object (self);

    return cb->block;
}

static VALUE
callback_get_data (VALUE self)
{
    LmAsyncCallback* cb = rb_lm_callback_from_ruby_object (self);
    VALUE res;
    switch (cb->notification)
    {
        case LM_CB_MSG:
        case LM_CB_REPLY:
        {
            res = LMMESSAGE2RVAL (GPOINTER2MSG (cb->data));
            break;
        }
        case LM_CB_CONN_OPEN:
        case LM_CB_AUTH:
        {
            res = GBOOL2RVAL (GPOINTER2GBOOL (cb->data));
            break;
        }
        case LM_CB_DISCONNECT:
        {
            res = INT2FIX (GPOINTER2DISCONNECT (cb->data));
            break;
        }
        case LM_CB_SSL:
        {
            res = INT2FIX (GPOINTER2SSLSTATUS (cb->data));
            break;
        }
        default:
            g_warning ("Unknown callback type '%d'\n", cb->notification);
    }

    return res;
}


void
Init_lm_callback(VALUE lm_mLM)
{
    lm_cCallback = rb_define_class_under (lm_mLM, "Callback", rb_cObject);

    rb_define_const (lm_mLM, "CB_MSG", INT2FIX (LM_CB_MSG));
    rb_define_const (lm_mLM, "CB_REPLY", INT2FIX (LM_CB_REPLY));
    rb_define_const (lm_mLM, "CB_CONN_OPEN", INT2FIX (LM_CB_CONN_OPEN));
    rb_define_const (lm_mLM, "CB_AUTH", INT2FIX (LM_CB_AUTH));
    rb_define_const (lm_mLM, "CB_DISCONNECT", INT2FIX (LM_CB_DISCONNECT));
    rb_define_const (lm_mLM, "CB_SSL", INT2FIX (LM_CB_SSL));

    rb_define_method (lm_cCallback, "target", callback_get_target, 0);
    rb_define_method (lm_cCallback, "kind", callback_get_kind, 0);
    rb_define_method (lm_cCallback, "data", callback_get_data, 0);
}

