/* 
 * $Id: cnid.c,v 1.1.4.1 2003-09-09 16:42:21 didg Exp $
 *
 * Copyright (c) 2003 the Netatalk Team
 * Copyright (c) 2003 Rafal Lewczuk <rlewczuk@pronet.pl>
 * 
 * This program is free software; you can redistribute and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation version 2 of the License or later
 * version if explicitly stated by any of above copyright holders.
 *
 */
#define USE_LIST

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <atalk/cnid.h>
#include <atalk/list.h>
#include <atalk/logger.h>
#include <stdlib.h>
#include <string.h>

/* List of all registered modules. */
static struct list_head modules = LIST_HEAD_INIT(modules);

static sigset_t sigblockset;
static const struct itimerval none = {{0, 0}, {0, 0}};
static struct itimerval savetimer;

/* Registers new CNID backend module. */

/* Once module has been registered, it cannot be unregistered. */
void cnid_register(struct _cnid_module *module)
{
    struct list_head *ptr;

    /* Check if our module is already registered. */
    list_for_each(ptr, &modules)
        if (0 == strcmp(list_entry(ptr, cnid_module, db_list)->name, module->name)) {
        LOG(log_error, logtype_afpd, "Module with name [%s] is already registered !", module->name);
        return;
    }

    LOG(log_info, logtype_afpd, "Registering CNID module [%s]", module->name);
    ptr = &(module->db_list);
    list_add_tail(ptr, &modules);
}

/* --------------- */
static int cnid_dir(const char *dir, mode_t mask)
{
   struct stat st; 

   if (stat(dir, &st) < 0) {
       if (errno != ENOENT) 
           return -1;
       if (ad_stat( dir, &st) < 0)
          return -1;

       LOG(log_info, logtype_cnid, "Setting uid/gid to %d/%d", st.st_uid, st.st_gid);
       if (setegid(st.st_gid) < 0 || seteuid(st.st_uid) < 0) {
           LOG(log_error, logtype_cnid, "uid/gid: %s", strerror(errno));
           return -1;
       }

       if (mkdir(dir, 0777 & ~mask) < 0)
           return -1;
   } else {
       LOG(log_info, logtype_cnid, "Setting uid/gid to %d/%d", st.st_uid, st.st_gid);
       if (setegid(st.st_gid) < 0 || seteuid(st.st_uid) < 0) {
           LOG(log_error, logtype_cnid, "uid/gid: %s", strerror(errno));
           return -1;
       }
   }
   return 0;
}

/* Opens CNID database using particular back-end */
struct _cnid_db *cnid_open(const char *volpath, mode_t mask, char *type)
{
    struct _cnid_db *db;
    cnid_module *mod = NULL;
    struct list_head *ptr;
    uid_t uid;
    gid_t gid;
    
    list_for_each(ptr, &modules) {
        if (0 == strcmp(list_entry(ptr, cnid_module, db_list)->name, type)) {
	    mod = list_entry(ptr, cnid_module, db_list);
        break;
        }
    }

    if (NULL == mod) {
        LOG(log_error, logtype_afpd, "Cannot find module named [%s] in registered module list!", type);
        return NULL;
    }
    if ((mod->flags & CNID_FLAG_SETUID)) {
        uid = geteuid();
        gid = getegid();
        if (seteuid(0)) {
            LOG(log_error, logtype_afpd, "seteuid failed %s", strerror(errno));
            return NULL;
        }
        if (cnid_dir(volpath, mask) < 0) {
            if ( setegid(gid) < 0 || seteuid(uid) < 0) {
                LOG(log_error, logtype_afpd, "can't seteuid back %s", strerror(errno));
                exit(1);
            }
            return NULL;
        }
    }

    db = mod->cnid_open(volpath, mask);

    if ((mod->flags & CNID_FLAG_SETUID)) {
        seteuid(0);
        if ( setegid(gid) < 0 || seteuid(uid) < 0) {
            LOG(log_error, logtype_afpd, "can't seteuid back %s", strerror(errno));
            exit(1);
        }
    }

    if (NULL == db) {
        LOG(log_error, logtype_afpd, "Cannot open CNID db at [%s].", volpath);
        return NULL;
    }
    db->flags |= mod->flags;

    if (db->flags |= CNID_FLAG_BLOCK) {
        sigemptyset(&sigblockset);
        sigaddset(&sigblockset, SIGTERM);
        sigaddset(&sigblockset, SIGHUP);
        sigaddset(&sigblockset, SIGUSR1);
        sigaddset(&sigblockset, SIGALRM);
    }

    return db;
}

/* ------------------- */
static void block_signal(struct _cnid_db *db)
{
    if ((db->flags & CNID_FLAG_BLOCK)) {
        sigprocmask(SIG_BLOCK, &sigblockset, NULL);
        setitimer(ITIMER_REAL, &none, &savetimer);
    }
}

/* ------------------- */
static void unblock_signal(struct _cnid_db *db)
{
    if ((db->flags & CNID_FLAG_BLOCK)) {
        setitimer(ITIMER_REAL, &savetimer, NULL);
        sigprocmask(SIG_UNBLOCK, &sigblockset, NULL);
    }
}

/* Closes CNID database. Currently it's just a wrapper around db->cnid_close(). */
void cnid_close(struct _cnid_db *db)
{
    if (NULL == db) {
        LOG(log_error, logtype_afpd, "Error: cnid_close called with NULL argument !");
        return;
    }
    block_signal(db);
    db->cnid_close(db);
    unblock_signal(db);
}

/* --------------- */
cnid_t cnid_add(struct _cnid_db *cdb, const struct stat *st, const cnid_t did, 
			const char *name, const int len, cnid_t hint)
{
cnid_t ret;

    block_signal(cdb);
    ret = cdb->cnid_add(cdb, st, did, name, len, hint);
    unblock_signal(cdb);
    return ret;
}

/* --------------- */
int cnid_delete(struct _cnid_db *cdb, cnid_t id)
{
int ret;

    block_signal(cdb);
    ret = cdb->cnid_delete(cdb, id);
    unblock_signal(cdb);
    return ret;
}


/* --------------- */
cnid_t cnid_get(struct _cnid_db *cdb, const cnid_t did, const char *name,const int len)
{
cnid_t ret;

    block_signal(cdb);
    ret = cdb->cnid_get(cdb, did, name, len);
    unblock_signal(cdb);
    return ret;
}

/* --------------- */
cnid_t cnid_getstamp(struct _cnid_db *cdb,  void *buffer, const int len)
{
cnid_t ret;

    if (!cdb->cnid_getstamp) {
        return -1;
    }
    block_signal(cdb);
    ret = cdb->cnid_getstamp(cdb, buffer, len);
    unblock_signal(cdb);
    return ret;
}

/* --------------- */
cnid_t cnid_lookup(struct _cnid_db *cdb, const struct stat *st, const cnid_t did,
			const char *name, const int len)
{
cnid_t ret;

    block_signal(cdb);
    ret = cdb->cnid_lookup(cdb, st, did, name, len);
    unblock_signal(cdb);
    return ret;
}

/* --------------- */
char *cnid_resolve(struct _cnid_db *cdb, cnid_t *id, void *buffer, u_int32_t len)
{
char *ret;

    block_signal(cdb);
    ret = cdb->cnid_resolve(cdb, id, buffer, len);
    unblock_signal(cdb);
    return ret;
}

/* --------------- */
int cnid_update   (struct _cnid_db *cdb, const cnid_t id, const struct stat *st, 
			const cnid_t did, const char *name, const int len)
{
int ret;

    block_signal(cdb);
    ret = cdb->cnid_update(cdb, id, st, did, name, len);
    unblock_signal(cdb);
    return ret;
}
			
