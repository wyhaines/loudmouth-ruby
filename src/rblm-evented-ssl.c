#include "rblm.h"
#include "rblm-private.h"
#include "rblm-synchronizer.h"

VALUE lm_cEventedSSL;

LmSSL *
rb_lm_ev_ssl_from_ruby_object (VALUE obj)
{
	LmSSL *ssl;

	if (!rb_lm__is_kind_of (obj, lm_cEventedSSL)) {
		rb_raise (rb_eTypeError, "not a EventedSSL");
	}

	Data_Get_Struct (obj, LmSSL, ssl);

	return ssl;
}

void
ev_ssl_free (LmSSL *ssl)
{
	LM_CALL (lm_ssl_unref (ssl));
}

VALUE
rb_lm_ev_ssl_to_ruby_object (LmSSL *ssl)
{
	if (ssl) {
		lm_ssl_ref (ssl);
		return Data_Wrap_Struct (lm_cEventedSSL, NULL,
					 ev_ssl_free, ssl);
	} else {
		return Qnil;
	}
}

VALUE
ev_ssl_allocate (VALUE klass)
{
	return Data_Wrap_Struct (klass, NULL, ev_ssl_free, NULL);
}

static VALUE
ev_ssl_is_supported (VALUE self)
{
        gboolean res;
        LM_CALL2 (lm_ssl_is_supported (), res);
        return GBOOL2RVAL (res);
}

static VALUE
ev_ssl_initialize (int argc, VALUE *argv, VALUE self)
{
	LmSSL    *ssl;
	VALUE     fingerprint;
	VALUE     func;
	char     *fingerprint_str = NULL;
	gpointer  func_ptr = NULL;

	rb_scan_args (argc, argv, "01&", &fingerprint, &func);

	if (!NIL_P (func)) {
		func_ptr = (gpointer) func;
	}

	if (!NIL_P (fingerprint)) {
		VALUE str_val;

		if (!rb_respond_to (fingerprint, rb_intern ("to_s"))) {
			rb_raise (rb_eArgError, 
				  "fingerprint should respond to to_s");
		}

		str_val = rb_funcall (fingerprint, rb_intern ("to_s"), 0);
		fingerprint_str = StringValuePtr (str_val);
	}

	LM_CALL2 (lm_ssl_new (fingerprint_str, /* expected_fingerprint */
                          ssl_handler,         /* ssl_function         */
			  func_ptr,            /* user_data            */
                          NULL),               /* notify               */
                  ssl);

	DATA_PTR (self) = ssl;

	return self;
}

static VALUE
ev_ssl_get_fingerprint (VALUE self)
{
	LmSSL *ssl = rb_lm_ev_ssl_from_ruby_object (self);

        const gchar* res = NULL;
        LM_CALL2 (lm_ssl_get_fingerprint (ssl), res);
	if (res) {
		return rb_str_new2 (res);
	}

	return Qnil;
}

static VALUE
ev_ssl_get_use_starttls (VALUE self)
{
	LmSSL *ssl = rb_lm_ev_ssl_from_ruby_object (self);
        gboolean res;
        LM_CALL2 (lm_ssl_get_use_starttls (ssl), res);

	return GBOOL2RVAL (res);
}

static VALUE
ev_ssl_set_use_starttls (VALUE self, VALUE use)
{
	LmSSL *ssl = rb_lm_ev_ssl_from_ruby_object (self);

        gboolean require;
        LM_CALL2 (lm_ssl_get_require_starttls (ssl), require);
	LM_CALL (lm_ssl_use_starttls (ssl,
			     RVAL2GBOOL (use),
			     require));

	return Qnil;
}

static VALUE
ev_ssl_get_require_starttls (VALUE self)
{
	LmSSL *ssl = rb_lm_ev_ssl_from_ruby_object (self);
        gboolean res;
        LM_CALL2 (lm_ssl_get_require_starttls (ssl), res);

        return GBOOL2RVAL (res);
}

static VALUE
ev_ssl_set_require_starttls (VALUE self, VALUE require)
{
	LmSSL *ssl = rb_lm_ev_ssl_from_ruby_object (self);

	LM_CALL (lm_ssl_use_starttls (ssl,
			     lm_ssl_get_use_starttls (ssl),
			     RVAL2GBOOL (require)));

	return Qnil;
}

extern void
Init_lm_evented_ssl (VALUE lm_mLM)
{
	lm_cEventedSSL = rb_define_class_under (lm_mLM, "EventedSSL", rb_cObject);

	rb_define_alloc_func (lm_cEventedSSL, ev_ssl_allocate);

	rb_define_singleton_method (lm_cEventedSSL, "supported?",
				    ev_ssl_is_supported, 0);

	rb_define_method (lm_cEventedSSL, "initialize", ev_ssl_initialize, -1);
	rb_define_method (lm_cEventedSSL, "fingerprint", ev_ssl_get_fingerprint, 0);
	rb_define_method (lm_cEventedSSL, "use_starttls", ev_ssl_get_use_starttls, 0);
	rb_define_method (lm_cEventedSSL, "use_starttls=", ev_ssl_set_use_starttls, 1);
	rb_define_method (lm_cEventedSSL, "require_starttls", ev_ssl_get_require_starttls, 0);
	rb_define_method (lm_cEventedSSL, "require_starttls=", ev_ssl_set_require_starttls, 1);
}

