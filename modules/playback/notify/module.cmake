lnd_add_module(notify
    SOURCES notify.c dispatch.c
    HEADER lindar_notify.h
    UPDATE lnd_notify_update STOP lnd_notify_stop FREE lnd_notify_free PRIORITY 90)
