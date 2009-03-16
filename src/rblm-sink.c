#include "rblm.h"
#include "rblm-callback.h"
#include "rblm-synchronizer.h"

static VALUE lm_cSink;

static void
sink_free (void* _)
{
    rblm_shutdown_sync();
}

static VALUE
sink_file_descriptor (VALUE self)
{
    rblm_init_sync();
    return INT2NUM (g_io_channel_unix_get_fd (lm2rb_read));
}

static VALUE
sink_notification (VALUE self)
{
    static gchar buf[1];
    g_io_channel_read_chars(lm2rb_read, buf, 1, NULL, NULL);
    LmAsyncCallback* cb = (LmAsyncCallback*)g_async_queue_pop(lm2rb_queue);
    if (cb)
        return lm_callback_to_ruby_object (cb);
    else
        return Qnil;
}

void
Init_lm_sink (VALUE lm_mLM)
{
    VALUE cleanup_callback = Data_Wrap_Struct(rb_cObject, 0, sink_free, NULL);
    rb_global_variable (&cleanup_callback);

    lm_cSink = rb_define_class_under (lm_mLM, "Sink", rb_cObject);

    rb_define_singleton_method (lm_cSink, "file_descriptor", sink_file_descriptor, 0);
    rb_define_singleton_method (lm_cSink, "notification", sink_notification, 0);
}

