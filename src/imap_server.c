#include "rumble.h"
#include "servers.h"
#include "comm.h"

// Main loop
void* rumble_imap_init(void* m) {
    masterHandle* master = (masterHandle*) m;
	

    // Initialize a session handle and wait for incoming connections.
    sessionHandle session;
    sessionHandle* sessptr = &session;
    ssize_t rc;
    char *tag, *cmd, *arg, *line, *tmp;
    const char* myName;
    int x = 0;
    time_t now;
    sessionHandle* s;
    pop3Session* pops;
    void* pp,*tp;
    pthread_t p = pthread_self();
    session.dict = cvector_init();
    session.recipients = cvector_init();
    session._svcHandle = (pop3Session*) malloc(sizeof(pop3Session));
    session.client = (clientHandle*) malloc(sizeof(clientHandle));
    session._master = m;
    pops = (pop3Session*) session._svcHandle;
    pops->account = 0;
    pops->bag = 0;
    session._tflags = RUMBLE_THREAD_POP3; // Identify the thread/session as POP3
    myName = rrdict(master->_core.conf, "servername");
    myName = myName ? myName : "??";
    tmp = (char*) malloc(100);
    #if RUMBLE_DEBUG & RUMBLE_DEBUG_THREADS
        #ifdef PTW32_CDECL
                        pp = (void*) p.p;
        #else
                        pp = p;
        #endif
        printf("<pop3::threads> Initialized thread %#x\n", pp);
    #endif
	
    while (1) {
        comm_accept(master->pop3.socket, session.client);
        pthread_mutex_lock(&master->pop3.mutex);
        cvector_add(master->pop3.handles, (void*) sessptr);
        pthread_mutex_unlock(&master->pop3.mutex);
        session.flags = 0;
        session._tflags += 0x00100000; // job count ( 0 through 4095)
        session.sender = 0;
            now = time(0);
        
        #if (RUMBLE_DEBUG & RUMBLE_DEBUG_COMM)
        strftime(tmp, 100, "%X", localtime(&now));
        printf("<debug::comm> [%s] Accepted connection from %s on IMAP4\n", tmp, session.client->addr);
        #endif
        
        // Check for hooks on accept()
        rc = RUMBLE_RETURN_OKAY;
        rc = rumble_server_schedule_hooks(master, sessptr, RUMBLE_HOOK_ACCEPT + RUMBLE_HOOK_POP3 );
		if ( rc == RUMBLE_RETURN_OKAY) rcprintf(sessptr, rumble_pop3_reply_code(101), myName); // Hello!
        
        // Parse incoming commands
        tag = (char*) malloc(32);
        cmd = (char*) malloc(32);
        arg = (char*) malloc(1024);
        if (!cmd || !arg || !tag) merror();
        while ( rc != RUMBLE_RETURN_FAILURE ) {
            memset(tag, 0, 32);
            memset(cmd, 0, 32);
            memset(arg, 0, 1024);
            line = rumble_comm_read(sessptr);
            rc = 421;
            if ( !line ) break;
            rc = 105; // default return code is "500 unknown command thing"
            if (sscanf(line, "%32c %32c %1000[^\r\n]", tag, cmd, arg)) {
                rumble_string_upper(cmd);
                if (!strcmp(cmd, "QUIT")) break; // bye!
                else if (!strcmp(cmd, "LOGIN"))         rc = rumble_server_imap_login(master, &session, tag, arg);
				else if (!strcmp(cmd, "LOGOUT"))        break;
				else if (!strcmp(cmd, "NOOP"))          rc = rumble_server_imap_noop(master, &session, tag, arg);
				else if (!strcmp(cmd, "CAPABILITY"))    rc = rumble_server_imap_capability(master, &session, tag, arg);
				else if (!strcmp(cmd, "AUTHENTICATE"))  rc = rumble_server_imap_authenticate(master, &session, tag, arg);
				else if (!strcmp(cmd, "STARTTLS"))      rc = rumble_server_imap_starttls(master, &session, tag, arg);

				else if (!strcmp(cmd, "SELECT"))        rc = rumble_server_imap_select(master, &session, tag, arg);
				else if (!strcmp(cmd, "EXAMINE"))       rc = rumble_server_imap_examine(master, &session, tag, arg);
				else if (!strcmp(cmd, "CREATE"))        rc = rumble_server_imap_create(master, &session, tag, arg);
				else if (!strcmp(cmd, "DELETE"))        rc = rumble_server_imap_delete(master, &session, tag, arg);
				else if (!strcmp(cmd, "RENAME"))        rc = rumble_server_imap_rename(master, &session, tag, arg);
				else if (!strcmp(cmd, "SUBSCRIBE"))     rc = rumble_server_imap_subscribe(master, &session, tag, arg);
				else if (!strcmp(cmd, "UNSUBSCRIBE"))   rc = rumble_server_imap_unsubscribe(master, &session, tag, arg);
				else if (!strcmp(cmd, "LIST"))          rc = rumble_server_imap_list(master, &session, tag, arg);
				else if (!strcmp(cmd, "LSUB"))          rc = rumble_server_imap_lsub(master, &session, tag, arg);
				else if (!strcmp(cmd, "STATUS"))        rc = rumble_server_imap_status(master, &session, tag, arg);
				else if (!strcmp(cmd, "APPEND"))        rc = rumble_server_imap_append(master, &session, tag, arg);

				else if (!strcmp(cmd, "CHECK"))         rc = rumble_server_imap_check(master, &session, tag, arg);
				else if (!strcmp(cmd, "CLOSE"))         rc = rumble_server_imap_close(master, &session, tag, arg);
				else if (!strcmp(cmd, "EXPUNGE"))       rc = rumble_server_imap_expunge(master, &session, tag, arg);
				else if (!strcmp(cmd, "SEARCH"))        rc = rumble_server_imap_search(master, &session, tag, arg);
				else if (!strcmp(cmd, "FETCH"))         rc = rumble_server_imap_fetch(master, &session, tag, arg);
				else if (!strcmp(cmd, "STORE"))         rc = rumble_server_imap_store(master, &session, tag, arg);
				else if (!strcmp(cmd, "COPY"))			rc = rumble_server_imap_copy(master, &session, tag, arg);
				else if (!strcmp(cmd, "UID"))			rc = rumble_server_imap_uid(master, &session, tag, arg);
            }
            free(line);
            if ( rc == RUMBLE_RETURN_IGNORE ) continue; // Skip to next line.
            else if ( rc == RUMBLE_RETURN_FAILURE ) break; // Abort!
            else rcprintf(&session, "%s BAD Invalid command!\r\n", tag); // Bad command thing.
        }
        // Cleanup
        #if (RUMBLE_DEBUG & RUMBLE_DEBUG_COMM)
        now = time(0);
        strftime(tmp, 100, "%X", localtime(&now));
        printf("<debug::comm> [%s] Closing connection from %s on IMAP4\n", tmp, session.client->addr);
        #endif
        if ( rc == 421 ) rcprintf(&session, "%s BAD Session timed out!\r\n", tag);// timeout!
        else rcsend(&session, "IMAP BYE bye!\r\n"); // bye!
		
        close(session.client->socket);

        /* Start cleanup */
        free(arg);
        free(cmd);
        rumble_clean_session(sessptr);
        rumble_letters_purge(pops->bag); /* delete any letters marked for deletion */
        rumble_letters_flush(pops->bag); /* flush the mail bag from memory */
        if ( pops->account ) rumble_free_account(pops->account);
        /* End cleanup */

        pthread_mutex_lock(&(master->imap.mutex));
        
        for (s = (sessionHandle*) cvector_first(master->imap.handles); s != NULL; s = (sessionHandle*) cvector_next(master->imap.handles)) {
            if (s == sessptr) { cvector_delete(master->imap.handles); x = 1; break; }
        }
        // Check if we were told to go kill ourself :(
        if ( session._tflags & RUMBLE_THREAD_DIE ) {
            #if RUMBLE_DEBUG & RUMBLE_DEBUG_THREADS
            printf("<imap4::threads>I (%#x) was told to die :(\n", (uintptr_t) pthread_self());
            #endif
            cvector_element* el = master->imap.threads->first;
            while ( el != NULL ) {
                pthread_t* t = (pthread_t*) el->object;
                #ifdef PTW32_CDECL
                        pp = (void*) p.p;
                        tp = t->p;
                #else
                        tp = t;
                        pp = p;
                #endif
				if (tp == pp) { cvector_delete_at(master->imap.threads, el); break; }
                el = el->next;
            }
            pthread_mutex_unlock(&master->imap.mutex);
            pthread_exit(0);
        }
        pthread_mutex_unlock(&master->imap.mutex);
    }
    pthread_exit(0);
    return 0;
}


ssize_t rumble_server_imap_login(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 
	char user[512], pass[512];
	address* addr;
	imap4Session* imap = (imap4Session*) session->_svcHandle;
	if ( sscanf(arg, "%s %s", user, pass) == 2 ) {
		addr = rumble_parse_mail_address(user);
		if ( addr ) {
			imap->account = rumble_account_data(session, addr->user, addr->domain);
			if ( imap->account ) {
				rcprintf(session, "%s OK Welcome!\r\n", tag);
			}
			else {
				rcprintf(session, "%s NO Incorrect username or password!\r\n", tag);
			}
		}
		else {
			rcprintf(session, "%s NO Incorrect username or password!\r\n", tag);
		}
	}
	else {
		rcprintf(session, "%s NO Incorrect username or password!\r\n", tag);
	}
	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_noop(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_capability(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_authenticate(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_starttls(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_select(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_examine(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_create(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_delete(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_rename(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_subscribe(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_unsubscribe(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_list(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_lsub(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_status(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_append(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_check(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_close(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_expunge(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_search(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_fetch(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_store(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_copy(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_uid(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

