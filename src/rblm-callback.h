/*
 * Loudmouth to ruby callback
 * Used by ruby code for dispatching to proper handler
 */

#ifndef _RBLM_CALLBACK_H
#define	_RBLM_CALLBACK_H

#include "rblm.h"

/* Types of callbacks that Loudmouth may trigger */
typedef enum {
    LM_CB_NONE,
    LM_CB_MSG,
    LM_CB_REPLY,
    LM_CB_CONN_OPEN,
    LM_CB_AUTH,
    LM_CB_DISCONNECT,
    LM_CB_SSL
} LmAsyncNotification;

/* Data posted to the async queue from Loudmouth */
typedef struct {
    LmAsyncNotification notification; /* Type of callback   */
    VALUE block;                      /* Target of callback */
    gpointer data;                    /* Associated data: LmMessage*, gboolean,
                                       * LmDisconnectReason or LmSSLStatus */
} LmAsyncCallback;

/* Data structure used for Loudmouth callback user data */
typedef struct {
    VALUE block;
    GIOChannel* write_channel;
} LmUserData;

/* Create async messages */
LmAsyncCallback* create_async_message (LmAsyncNotification notification,
                                       VALUE block,
                                       gpointer data);

/* Create ruby object from raw async message */
VALUE lm_callback_to_ruby_object (LmAsyncCallback* callback);

#endif	/* _RBLM_CALLBACK_H */

