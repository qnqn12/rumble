/*$6
 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 */

#include "rumble.h"
#include "private.h"
#include "database.h"
#include <stdarg.h>
masterHandle    *rumble_database_master_handle = 0;
int             rumble_database_engine = RUMBLE_SQLITE3;

/*
 =======================================================================================================================
    Database constructors and wrappers
 =======================================================================================================================
 */
void rumble_database_load_sqlite(masterHandle *master, FILE *runlog) {

    /*~~~~~~~~~~~~~~~~~~~*/
    char    dbpath[1024];
    char    mailpath[1024];
    int     rc;
    FILE    *ftmp;
    /*~~~~~~~~~~~~~~~~~~~*/

    printf("Checking for thread-safe environment: %s\n", sqlite3_threadsafe() == 0 ? "No" : "Yes");
    sprintf(dbpath, "%s/rumble.sqlite", rumble_config_str(master, "datafolder"));
    sprintf(mailpath, "%s/mail.sqlite", rumble_config_str(master, "datafolder"));
    ftmp = fopen(dbpath, "a+");
    if (!ftmp) {
        sprintf(dbpath, "%s/%s/rumble.sqlite", rrdict(master->_core.conf, "execpath"), rumble_config_str(master, "datafolder"));
        sprintf(mailpath, "%s/%s/mail.sqlite", rrdict(master->_core.conf, "execpath"), rumble_config_str(master, "datafolder"));
    } else fclose(ftmp);
    printf("%-48s", "Loading database...");
    statusLog("Loading database %s", dbpath);

    /* Domains and accounts */
    master->_core.db = radb_init_sqlite(dbpath);
    if (!master->_core.db) {
        fprintf(stderr, "ERROR: Can't open database <%s>\r\n", dbpath);
        statusLog("ERROR: Can't open database <%s>\r\n", dbpath);
        exit(EXIT_FAILURE);
    }

    /* Letters */
    master->_core.mail = radb_init_sqlite(mailpath);
    if (!master->_core.mail) {
        fprintf(stderr, "ERROR: Can't open database <%s>\r\n", mailpath);
        statusLog("ERROR: Can't open database <%s>\r\n", mailpath);
        exit(EXIT_FAILURE);
    }

    radb_run(master->_core.db, "PRAGMA synchronous = 2");
    radb_run(master->_core.mail, "PRAGMA synchronous = 1");
    radb_run(master->_core.mail, "PRAGMA cache_size = 5000");

    /*$2
     -------------------------------------------------------------------------------------------------------------------
        Check if the tables exists or not
     -------------------------------------------------------------------------------------------------------------------
     */

    rc = radb_do(master->_core.db, "PRAGMA table_info (accounts)");
    if (!rc) {
        printf("[OK]\r\n");
        printf("%-48s", "Setting up tables...");
        statusLog("New installation, creating DB");
        rc = radb_do(master->_core.db,
                     "CREATE TABLE \"domains\" (\"id\" INTEGER PRIMARY KEY  AUTOINCREMENT  NOT NULL  UNIQUE , \"domain\" VARCHAR NOT NULL , \"storagepath\" VARCHAR);");
        rc = radb_do(master->_core.db,
                     "CREATE TABLE \"accounts\" (\"id\" INTEGER PRIMARY KEY  AUTOINCREMENT  NOT NULL  UNIQUE , \"domain\" VARCHAR NOT NULL , \"user\" VARCHAR, \"password\" CHAR(64), \"type\" CHAR(5) NOT NULL  DEFAULT mbox, \"arg\" VARCHAR);");
        rc = radb_do(master->_core.db,
                     "CREATE TABLE \"folders\" (\"uid\" INTEGER NOT NULL  DEFAULT 0, \"id\" INTEGER PRIMARY KEY  AUTOINCREMENT  NOT NULL  UNIQUE , \"name\" VARCHAR NOT NULL , \"subscribed\" BOOL NOT NULL  DEFAULT false);");
        if (!rc) printf("[OK]\r\n");
        else {
            statusLog("Couldn't create database tables!");
            printf("[BAD]\r\n");
        }
    } else printf("[OK]\r\n");
    statusLog("Database successfully initialized");
    printf("%-48s", "Loading mail db...");
    rc = radb_do(master->_core.mail, "PRAGMA table_info (queue)");
    if (!rc) {
        printf("[OK]\r\n");
        printf("%-48s", "Setting up message tables...");
        statusLog("New installation, creating mail DB");
        rc = radb_do(master->_core.mail,
                     "CREATE TABLE \"mbox\" (\"id\" INTEGER PRIMARY KEY  AUTOINCREMENT  NOT NULL  UNIQUE , \"uid\" INTEGER NOT NULL , \"fid\" VARCHAR NOT NULL , \"size\" INTEGER NOT NULL , \"delivered\" INTEGER DEFAULT (strftime('%s', 'now')), \"folder\" INTEGER NOT NULL DEFAULT 0, \"flags\" INTEGER NOT NULL DEFAULT 1 );");
        rc = radb_do(master->_core.mail,
                     "CREATE TABLE \"queue\" (\"id\" INTEGER PRIMARY KEY  NOT NULL ,\"time\" INTEGER NOT NULL  DEFAULT (STRFTIME('%s','now')) ,\"loops\" INTEGER NOT NULL  DEFAULT (0) ,\"fid\" VARCHAR NOT NULL ,\"sender\" VARCHAR NOT NULL ,\"recipient\" VARCHAR NOT NULL ,\"flags\" INTEGER NOT NULL  DEFAULT (0) );");
        rc = radb_do(master->_core.mail, "CREATE TABLE \"trash\" (\"id\" INTEGER PRIMARY KEY  NOT NULL ,\"fid\" VARCHAR NOT NULL);");
        if (!rc) printf("[OK]\r\n");
        else {
            statusLog("Couldn't create database tables!");
            printf("[BAD]\r\n");
        }
    } else printf("[OK]\r\n");
    statusLog("Database successfully initialized");
}

#ifdef MYSQL_CLIENT

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void rumble_database_load_mysql(masterHandle *master, FILE *runlog) {

    /*~~~~~~~~~~~~~~~~*/
    const char  *host,
                *user,
                *pass,
                *db;
    int         port,
                threads,
                rc;
    /*~~~~~~~~~~~~~~~~*/

    host = rrdict(master->_core.conf, "mysqlhost");
    user = rrdict(master->_core.conf, "mysqluser");
    pass = rrdict(master->_core.conf, "mysqlpass");
    db = rrdict(master->_core.conf, "mysqldb");
    port = atoi(rrdict(master->_core.conf, "mysqlport"));
    threads = atoi(rrdict(master->_core.conf, "mysqlconnections"));
    printf("%-38s", "MySQL: Connecting....");
    master->_core.db = radb_init_mysql(threads, host, user, pass, db, port);
    if (!master->_core.db) {
        printf("[BAD]\r\n");
        statusLog("Failed to connect to mysql database!");
        exit(0);
    }

    rc = radb_do(master->_core.db, "DESCRIBE `queue`;");
    if (!rc) {
        printf("[OK]\r\n");
        printf("%-48s", "Setting up tables...");
        statusLog("New installation, creating DB");
        rc = radb_do(master->_core.db,
                     "CREATE TABLE `domains` ( `id` MEDIUMINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY , `domain` VARCHAR( 128 ) NOT NULL , `storagepath` VARCHAR( 128 ) NOT NULL );");
        rc = radb_do(master->_core.db,
                     "CREATE TABLE `accounts` ( `id` INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY , `domain` VARCHAR( 128 ) NOT NULL , `user` VARCHAR( 128 ) NOT NULL , `password` CHAR( 64 ) NOT NULL , `type` ENUM( 'mbox', 'alias', 'mod', 'feed', 'relay', 'void' ) NOT NULL DEFAULT 'mbox', `arg` VARCHAR( 255 ) NOT NULL );");
        rc = radb_do(master->_core.db,
                     "CREATE TABLE `folders` ( `uid` INT UNSIGNED NOT NULL DEFAULT '0', `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY , `name` VARCHAR( 128 ) NOT NULL , `subscribed` ENUM( 'true', 'false' ) NOT NULL DEFAULT 'false' );");
        rc = radb_do(master->_core.db,
                     "CREATE TABLE `mbox` ( `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY , `uid` INT NOT NULL , `fid` VARCHAR( 64 ) NOT NULL , `size` BIGINT NOT NULL , `delivered` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP , `folder` BIGINT UNSIGNED NOT NULL , `flags` INT NOT NULL , INDEX ( `uid` ) );");
        rc = radb_do(master->_core.db,
                     "CREATE TABLE `queue` ( `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY , `time` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP , `loops` TINYINT UNSIGNED NOT NULL DEFAULT '0', `fid` VARCHAR( 64 ) NOT NULL , `sender` VARCHAR( 255 ) NOT NULL , `recipient` VARCHAR( 255 ) NOT NULL , `flags` INT NOT NULL ); ");
        if (!rc) printf("[OK]\r\n");
        else {
            statusLog("Couldn't create database tables!");
            printf("[BAD]");
        }
    } else {
        printf("[OK]\r\n");
    }

    printf("Cleaning up\r\n");
    fflush(stdout);
    statusLog("Database successfully initialized");
}
#endif

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void rumble_database_load(masterHandle *master, FILE *runlog) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    const char  *dbe = rrdict(master->_core.conf, "databaseengine");
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    if (!strcmp(dbe, "mysql")) rumble_database_engine = RUMBLE_MYSQL;
    if (!strcmp(dbe, "postgresql")) rumble_database_engine = RUMBLE_POSTGRESQL;
    if (rumble_database_engine == RUMBLE_SQLITE3) rumble_database_load_sqlite(master, runlog);
#ifdef MYSQL_CLIENT
    if (rumble_database_engine == RUMBLE_MYSQL) rumble_database_load_mysql(master, runlog);
#endif
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void rumble_free_account(rumble_mailbox *user) {
    if (!user) return;
    if (user->arg) free(user->arg);
    if (user->user) free(user->user);
    if (user->hash) free(user->hash);
    if (user->domain) {
        if (user->domain->name) free(user->domain->name);
        if (user->domain->path) free(user->domain->path);
        free(user->domain);
    }

    user->arg = 0;
    user->domain = 0;
    user->user = 0;
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
uint32_t rumble_account_exists(sessionHandle *session, const char *user, const char *domain) {

    /*~~~~~~~*/
    int rc = 0;
    /*~~~~~~~*/

    if (rumble_database_master_handle->_core.db->dbType == RADB_SQLITE3) {
        rc = radb_run_inject(rumble_database_master_handle->_core.db,
                             "SELECT 1 FROM accounts WHERE domain = %s AND %s GLOB user ORDER BY LENGTH(user) DESC LIMIT 1", domain, user);
    } else if (rumble_database_master_handle->_core.db->dbType == RADB_MYSQL) {
        rc = radb_run_inject(rumble_database_master_handle->_core.db,
                             "SELECT 1 FROM accounts WHERE domain = %s AND %s LIKE user ORDER BY LENGTH(user) DESC LIMIT 1", domain, user);
    }

    return (rc);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
uint32_t rumble_account_exists_raw(const char *user, const char *domain) {

    /*~~~*/
    int rc;
    /*~~~*/

    rc = radb_run_inject(rumble_database_master_handle->_core.db,
                         "SELECT 1 FROM accounts WHERE domain = %s AND user = %s ORDER BY LENGTH(user) DESC LIMIT 1", domain, user);
    return (rc);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
rumble_mailbox *rumble_account_data(uint32_t uid, const char *user, const char *domain) {

    /*~~~~~~~~~~~~~~~~~~~~~*/
    radbObject      *dbo = 0;
    radbResult      *dbr;
    char            *tmp,
                    *xusr,
                    *xdmn;
    rumble_mailbox  *acc;
    /*~~~~~~~~~~~~~~~~~~~~~*/

    if (uid) {
        dbo = radb_prepare(rumble_database_master_handle->_core.db,
                           "SELECT id, domain, user, password, type, arg FROM accounts WHERE id = %u LIMIT 1", uid);
    } else {
        if (!domain or!user) return (0);
        xusr = strclone(user);
        xdmn = strclone(domain);
        rumble_string_lower(xusr);
        rumble_string_lower(xdmn);
        if (rumble_database_master_handle->_core.db->dbType == RADB_SQLITE3) {
            dbo = radb_prepare(rumble_database_master_handle->_core.db,
                               "SELECT id, domain, user, password, type, arg FROM accounts WHERE domain = %s AND %s GLOB user ORDER BY LENGTH(user) DESC LIMIT 1",
                           xdmn, xusr);
        } else if (rumble_database_master_handle->_core.db->dbType == RADB_MYSQL) {
            dbo = radb_prepare(rumble_database_master_handle->_core.db,
                               "SELECT id, domain, user, password, type, arg FROM accounts WHERE domain = %s AND %s LIKE user ORDER BY LENGTH(user) DESC LIMIT 1",
                           xdmn, xusr);
        }

        free(xusr);
        free(xdmn);
    }

    dbr = radb_step(dbo);
    acc = NULL;
    if (dbr) {
        acc = (rumble_mailbox *) malloc(sizeof(rumble_mailbox));
        if (!acc) merror();

        /* Account UID */
        acc->uid = dbr->column[0].data.int32;

        /* Account Domain struct */
        acc->domain = rumble_domain_copy(dbr->column[1].data.string);

        /* Account Username */
        acc->user = strclone(dbr->column[2].data.string);

        /* Password (hashed) */
        acc->hash = strclone(dbr->column[3].data.string);

        /* Account type */
        tmp = strclone(dbr->column[4].data.string);
        rumble_string_lower(tmp);
        acc->type = RUMBLE_MTYPE_MBOX;
        if (!strcmp(tmp, "alias")) acc->type = RUMBLE_MTYPE_ALIAS;
        else if (!strcmp(tmp, "mod"))
            acc->type = RUMBLE_MTYPE_MOD;
        else if (!strcmp(tmp, "feed"))
            acc->type = RUMBLE_MTYPE_FEED;
        else if (!strcmp(tmp, "relay"))
            acc->type = RUMBLE_MTYPE_FEED;
        free(tmp);

        /* Account args */
        acc->arg = strclone(dbr->column[5].data.string);
    }

    radb_cleanup(dbo);
    return (acc);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
rumble_mailbox *rumble_account_data_auth(uint32_t uid, const char *user, const char *domain, const char *pass) {

    /*~~~~~~~~~~~~~~~~~~*/
    rumble_mailbox  *acc;
    char            *hash;
    /*~~~~~~~~~~~~~~~~~~*/
    acc = rumble_account_data(0, user, domain);
    if (acc) {
        hash = rumble_sha256((const unsigned char *) pass);
        if (!strcmp(hash, acc->hash)) {
			rumble_debug("core", "Account %s successfully logged in.", user);
			return (acc);
		}
        rumble_free_account(acc);
        acc = 0;
    }
	rumble_debug("core", "Account %s failed to log in (wrong pass?).", user);
    return (acc);
}

/*
 =======================================================================================================================
    rumble_domain_exists: Checks if a domain exists in the database. Returns 1 if true, 0 if false.
 =======================================================================================================================
 */
uint32_t rumble_domain_exists(const char *domain) {

    /*~~~~~~~~~~~~~~~~~~~~*/
    uint32_t        rc;
    rumble_domain   *dmn;
    d_iterator      iter;
    char            *dmncpy;
    /*~~~~~~~~~~~~~~~~~~~~*/

    dmncpy = strclone(domain);
    rumble_string_lower(dmncpy);
    rc = 0;
    rumble_rw_start_read(rumble_database_master_handle->domains.rrw);
    foreach((rumble_domain *), dmn, rumble_database_master_handle->domains.list, iter) {
        if (!strcmp(dmn->name, dmncpy)) {
            rc = 1;
            break;
        }
    }

    free(dmncpy);
    rumble_rw_stop_read(rumble_database_master_handle->domains.rrw);
    return (rc);
}

/*
 =======================================================================================================================
    rumble_domain_copy: Returns a copy of the domain info
 =======================================================================================================================
 */
rumble_domain *rumble_domain_copy(const char *domain) {

    /*~~~~~~~~~~~~~~~~~~~~*/
    rumble_domain   *dmn,
                    *rc;
    d_iterator      iter;
    char            *dmncpy;
    /*~~~~~~~~~~~~~~~~~~~~*/

    dmncpy = strclone(domain);
    rumble_string_lower(dmncpy);
    rc = 0;
    rumble_rw_start_read(rumble_database_master_handle->domains.rrw);
    foreach((rumble_domain *), dmn, rumble_database_master_handle->domains.list, iter) {
        if (!strcmp(dmn->name, dmncpy)) {
            rc = (rumble_domain *) malloc(sizeof(rumble_domain));
            rc->name = (char *) calloc(1, strlen(dmn->name) + 1);
            rc->path = (char *) calloc(1, strlen(dmn->path) + 1);
            strcpy(rc->name, dmn->name);
            strcpy(rc->path, dmn->path);
            rc->id = dmn->id;
            break;
        }
    }

    rumble_rw_stop_read(rumble_database_master_handle->domains.rrw);
    free(dmncpy);
    return (rc);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
cvector *rumble_domains_list(void) {

    /*~~~~~~~~~~~~~~~~~~*/
    rumble_domain   *dmn,
                    *rc;
    d_iterator      iter;
    cvector         *cvec;
    /*~~~~~~~~~~~~~~~~~~*/

    cvec = cvector_init();
    rumble_rw_start_read(rumble_database_master_handle->domains.rrw);
    foreach((rumble_domain *), dmn, rumble_database_master_handle->domains.list, iter) {
        rc = (rumble_domain *) malloc(sizeof(rumble_domain));
        rc->id = 0;
        rc->name = (char *) calloc(1, strlen(dmn->name) + 1);
        rc->path = (char *) calloc(1, strlen(dmn->path) + 1);
        strcpy(rc->name, dmn->name);
        strcpy(rc->path, dmn->path);
        rc->id = dmn->id;
        cvector_add(cvec, rc);
    }

    rumble_rw_stop_read(rumble_database_master_handle->domains.rrw);
    return (cvec);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
cvector *rumble_database_accounts_list(const char *domain) {

    /*~~~~~~~~~~~~~~~~~~*/
    radbObject      *dbo;
    radbResult      *dbr;
    cvector         *cvec;
    rumble_mailbox  *acc;
    char            *tmp;
    /*~~~~~~~~~~~~~~~~~~*/

    cvec = cvector_init();
    if (rumble_domain_exists(domain)) {
        dbo = radb_prepare(rumble_database_master_handle->_core.db, "SELECT id, user, password, type, arg FROM accounts WHERE domain = %s",
                           domain);
        acc = NULL;
        while ((dbr = radb_step(dbo))) {
            acc = (rumble_mailbox *) malloc(sizeof(rumble_mailbox));
            if (!acc) merror();

            /* Account UID */
            acc->uid = dbr->column[0].data.int32;

            /* Account Username */
            acc->user = strclone(dbr->column[1].data.string);

            /* Password (hashed) */
            acc->hash = strclone(dbr->column[2].data.string);

            /* Account type */
            tmp = strclone(dbr->column[3].data.string);
            rumble_string_lower(tmp);
            acc->type = RUMBLE_MTYPE_MBOX;
            if (!strcmp(tmp, "alias")) acc->type = RUMBLE_MTYPE_ALIAS;
            else if (!strcmp(tmp, "mod"))
                acc->type = RUMBLE_MTYPE_MOD;
            else if (!strcmp(tmp, "feed"))
                acc->type = RUMBLE_MTYPE_FEED;
            else if (!strcmp(tmp, "relay"))
                acc->type = RUMBLE_MTYPE_FEED;
            free(tmp);

            /* Account args */
            acc->arg = strclone(dbr->column[4].data.string);
            acc->domain = 0;
            cvector_add(cvec, acc);
        }

        radb_cleanup(dbo);
    }

    return (cvec);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void rumble_database_accounts_free(cvector *accounts) {

    /*~~~~~~~~~~~~~~~~~~*/
    c_iterator      citer;
    rumble_mailbox  *acc;
    /*~~~~~~~~~~~~~~~~~~*/

    cforeach((rumble_mailbox *), acc, accounts, citer) {
        if (acc->hash) free(acc->hash);
        if (acc->user) free(acc->user);
        if (acc->arg) free(acc->arg);
        free(acc);
    }

    cvector_destroy(accounts);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void rumble_domain_free(rumble_domain *domain) {
    if (!domain) return;
    if (domain->name) free(domain->name);
    if (domain->path) free(domain->path);
    free(domain);
}

/*
 =======================================================================================================================
    Internal database functions (not for use by modules) ;
    rumble_database_update_domains: Updates the list of domains from the db
 =======================================================================================================================
 */
void rumble_database_update_domains(void) {

    /*~~~~~~~~~~~~~~~~~~~~*/
    radbObject      *dbo;
    radbResult      *dbr;
    rumble_domain   *domain;
    d_iterator      iter;
    /*~~~~~~~~~~~~~~~~~~~~*/

    /* Clean up the old list */
    rumble_rw_start_write(rumble_database_master_handle->domains.rrw);
    foreach((rumble_domain *), domain, rumble_database_master_handle->domains.list, iter) {
        free(domain->name);
        free(domain->path);
        free(domain);
    }

    dvector_flush(rumble_database_master_handle->domains.list);
    dbo = radb_prepare(rumble_database_master_handle->_core.db, "SELECT id, domain, storagepath FROM domains WHERE 1");
    while ((dbr = radb_step(dbo))) {
        domain = (rumble_domain *) malloc(sizeof(rumble_domain));

        /* Domain ID */
        domain->id = dbr->column[0].data.int32;

        /* Domain name */
        domain->name = strclone(dbr->column[1].data.string);

        /* Optional domain specific storage path */
        domain->path = strclone(dbr->column[2].data.string);
        dvector_add(rumble_database_master_handle->domains.list, domain);

        /*
         * printf("Found domain: %s (%d)\r\n", domain->name, domain->id);
         */
    }

    rumble_rw_stop_write(rumble_database_master_handle->domains.rrw);
    radb_cleanup(dbo);
}
