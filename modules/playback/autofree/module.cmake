lnd_add_module(autofree SOURCES autofree.c REQUIRES graph HEADER lindar_autofree.h
    PRIORITY 40 FREE lnd_autofree_free UPDATE lnd_autofree_update MAINTAIN lnd_autofree_update)
