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
    imap4Session* pops;
    void* pp,*tp;
    pthread_t p = pthread_self();
    session.dict = cvector_init();
    session.recipients = cvector_init();
    session._svcHandle = (imap4Session*) malloc(sizeof(imap4Session));
    session.client = (clientHandle*) malloc(sizeof(clientHandle));
    session._master = m;
    pops = (imap4Session*) session._svcHandle;
    pops->account = 0;
    pops->bag = 0;
	pops->folder = 0;
    session._tflags = RUMBLE_THREAD_IMAP; // Identify the thread/session as IMAP4
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
        comm_accept(master->imap.socket, session.client);
        pthread_mutex_lock(&master->imap.mutex);
        cvector_add(master->imap.handles, (void*) sessptr);
        pthread_mutex_unlock(&master->imap.mutex);
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
        rc = rumble_server_schedule_hooks(master, sessptr, RUMBLE_HOOK_ACCEPT + RUMBLE_HOOK_IMAP );
		if ( rc == RUMBLE_RETURN_OKAY) rcprintf(sessptr, "* OK <%s> IMAP4rev1 Service Ready\r\n", myName); // Hello!
        
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
            if (sscanf(line, "%32s %32s %1000[^\r\n]", tag, cmd, arg)) {
                rumble_string_upper(cmd);
				if (!strcmp(cmd, "UID")) { // Set UID flag if requested
					session.flags |= RUMBLE_IMAP4_HAS_UID;
					sscanf(arg, "%32s %1000[^\r\n]", cmd, arg);
					rumble_string_upper(cmd);
				}
				else session.flags -= (session.flags & RUMBLE_IMAP4_HAS_UID); // clear UID demand if not there.
				printf("Client said: <%s> %s %s\r\n", tag, cmd, arg);
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
        else {
			rcsend(&session, "* BYE bye!\r\n");
			rcprintf(&session, "%s OK <%s> signing off for now.\r\n", tag, myName);
		}
		
        close(session.client->socket);

        /* Start cleanup */
        free(arg);
        free(cmd);
        rumble_clean_session(sessptr);
        //rumble_letters_expunge(pops->bag); /* delete any letters marked for deletion */
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
			imap->account = rumble_account_data_auth(session, addr->user, addr->domain, pass);
			if ( imap->account ) {
				rcprintf(session, "%s OK Welcome!\r\n", tag);
				imap->bag = rumble_letters_retreive(imap->account);
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
	rcprintf(session, "%s OK Doin' nothin'...\r\n", tag);
	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_capability(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 
	imap4Session* imap = (imap4Session*) session->_svcHandle;
	if ( !(session->flags & RUMBLE_IMAP4_HAS_TLS) ) rcsend(session, "* CAPABILITY IMAP4rev1 STARTTLS AUTH=PLAIN LOGINDISABLED\r\n");
	else rcsend(session, "* CAPABILITY IMAP4rev1 AUTH=PLAIN\r\n");
	rcprintf(session, "%s OK CAPABILITY completed.\r\n", tag);
	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_authenticate(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 
	imap4Session* imap = (imap4Session*) session->_svcHandle;
	char method[32], *user, *pass, *line, *buffer;
	address* addr = 0;
	if ( sscanf(strchr(arg, '"') ? strchr(arg, '"')+1 : arg, "%32[a-zA-Z]", method)  ) {
		rumble_string_upper(method);
		if ( !strcmp(method, "PLAIN") ) {
			rcprintf(session, "+ OK Method <%s> accepted, input stuffs!\r\n", method);
			line = rcread(session);
			if (line) {
				buffer = rumble_decode_base64(line);
				user = buffer+1;
				pass = buffer+2 + strlen(user);
				addr = rumble_parse_mail_address(user);
				if ( addr ) {
					imap->account = rumble_account_data_auth(session, addr->user, addr->domain, pass);
					if ( imap->account ) {
						rcprintf(session, "%s OK Welcome!\r\n", tag);
						imap->bag = rumble_letters_retreive(imap->account);
					}
					else {
						rcprintf(session, "%s NO Incorrect username or password!\r\n", tag);
					}
				}
				else {
					rcprintf(session, "%s NO Incorrect username or password!\r\n", tag);
				}
				free(buffer);
				free(line);
			}
		}
	}
	rumble_free_address(addr);
	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_starttls(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_select(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 
	uint32_t exists, recent, first,i,found;
	rumble_args* folder;
	rumbleIntValuePair* pair;
	char* selector;
	imap4Session* imap = (imap4Session*) session->_svcHandle;
	rumble_letter* letter;
	
	/* Are we authed? */
	if ( imap->account && imap->bag ) {
		folder = rumble_read_words(arg);
		found = 0;
		selector = folder->argc ? folder->argv[0] : "";
		for ( pair = (rumbleIntValuePair*) cvector_first(imap->bag->folder.names); pair != NULL; pair = (rumbleIntValuePair*) cvector_next(imap->bag->folder.names) ) {
			if ( !strcmp(selector, pair->value) ) { imap->bag->folder.id = pair->key; found++; break; }
		}
		if (!found && !strcmp(selector, "INBOX") ) { imap->bag->folder.id = 0; found++; }
		if ( found ) {
			session->flags |= RUMBLE_IMAP4_HAS_SELECT;
			session->flags |= RUMBLE_IMAP4_HAS_READWRITE;
			exists = 0;
			recent = 0;
			first = 0;
			for ( i = 0; i < imap->bag->size; i++ ) {
				letter = imap->bag->letters[i];
				if ( letter->folder == imap->bag->folder.id ) {
					exists++;
					if ( !first && ( (letter->flags & RUMBLE_LETTER_UNREAD) || (letter->flags == RUMBLE_LETTER_RECENT) ) ) first = exists;
					if (letter->flags == RUMBLE_LETTER_RECENT) {
						letter->flags |= RUMBLE_LETTER_UNREAD;
						recent++;
					}
				}
			}
			rcprintf(session, "* %u EXISTS\r\n", exists);
			rcsend(session, "* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)\r\n");
			if (recent) rcprintf(session, "* %u RECENT.\r\n", recent);
			if (first) rcprintf(session, "* OK [UNSEEN %u] Message %u is the first unseen message.\r\n", first, first);
			rcprintf(session, "* OK [UIDVALIDITY %08u] UIDs valid\r\n", imap->account->uid);
			rcprintf(session, "%s OK [READ-WRITE] SELECT completed.\r\n", tag);
			rumble_letters_update(imap->bag); // Update letter flags if needed.
		}
		else {
			rcprintf(session, "%s NO No such mailbox <%s>!\r\n", tag, selector);
		}
	}
	else {
		rcprintf(session, "%s NO Not logged in yet!\r\n", tag);
	}
	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_examine(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 
	uint32_t exists, recent, first,i,found;
	rumble_args* folder;
	rumbleIntValuePair* pair;
	char* selector;
	imap4Session* imap = (imap4Session*) session->_svcHandle;
	rumble_letter* letter;
	
	/* Are we authed? */
	if ( imap->account && imap->bag ) {
		folder = rumble_read_words(arg);
		found = 0;
		selector = folder->argc ? folder->argv[0] : "";
		for ( pair = (rumbleIntValuePair*) cvector_first(imap->bag->folder.names); pair != NULL; pair = (rumbleIntValuePair*) cvector_next(imap->bag->folder.names) ) {
			if ( !strcmp(selector, pair->value) ) { imap->bag->folder.id = pair->key; found++; break; }
		}
		if (!found && !strcmp(selector, "INBOX") ) { imap->bag->folder.id = 0; found++; }
		if ( found ) {
			session->flags |= RUMBLE_IMAP4_HAS_SELECT;
			session->flags |= RUMBLE_IMAP4_HAS_READONLY;
			exists = 0;
			recent = 0;
			first = 0;
			for ( i = 0; i < imap->bag->size; i++ ) {
				letter = imap->bag->letters[i];
				if ( letter->folder == imap->bag->folder.id ) {
					exists++;
					if ( !first && ( (letter->flags & RUMBLE_LETTER_UNREAD) || (letter->flags == RUMBLE_LETTER_RECENT) ) ) first = exists;
					if (letter->flags == RUMBLE_LETTER_RECENT) {
						recent++;
					}
				}
			}
			rcprintf(session, "* %u EXISTS\r\n", exists);
			rcsend(session, "* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)\r\n");
			if (recent) rcprintf(session, "* %u RECENT.\r\n", recent);
			if (first) rcprintf(session, "* OK [UNSEEN %u] Message %u is the first unseen message.\r\n", first, first);
			rcprintf(session, "* OK [UIDVALIDITY %08u] UIDs valid\r\n", imap->account->uid);
			rcprintf(session, "%s OK [READ-ONLY] EXAMINE completed.\r\n", tag);
		}
		else {
			rcprintf(session, "%s NO No such mailbox <%s>!\r\n", tag, selector);
		}
	}
	else {
		rcprintf(session, "%s NO Not logged in yet!\r\n", tag);
	}
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
	rumble_args* args;
	char* mbox, *pattern, *pfolder;
	rumbleIntValuePair* pair;
	imap4Session* imap = (imap4Session*) session->_svcHandle;
	args = rumble_read_words(arg);
	if (args && args->argc == 2) {
		mbox = args->argv[0];
		pattern = args->argv[1];
		if ( imap->bag->folder.id == -1 ) { rcsend(session, "* LIST (\\Noselect) \"/\" \"\"\r\n"); rcsend(session, "* LIST () \"/\" \"INBOX\"\r\n"); }
		if ( imap->bag->folder.id != -1) {
			for ( pair = (rumbleIntValuePair*) cvector_first(imap->bag->folder.names); pair != NULL; pair = (rumbleIntValuePair*) cvector_next(imap->bag->folder.names) ) {
				if ( pair->key == imap->bag->folder.id ) pfolder = pair->value; break;
			}
		}
		for ( pair = (rumbleIntValuePair*) cvector_first(imap->bag->folder.names); pair != NULL; pair = (rumbleIntValuePair*) cvector_next(imap->bag->folder.names) ) {
			rcprintf(session, "* LIST () \"\" \"%s\"\r\n", pair->value);
		}
		
		rcprintf(session, "%s OK LIST completed\r\n", tag);
	}
	else rcprintf(session, "%s BAD Invalid LIST syntax!\r\n", tag);
	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_lsub(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 
	rcprintf(session, "%s OK I r not here!!\r\n", tag);
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
	imap4Session* imap = (imap4Session*) session->_svcHandle;
	if ( imap->account && (session->flags & RUMBLE_IMAP4_HAS_SELECT) ) {
		rumble_letters_expunge(imap->bag);
		rumble_letters_update(imap->bag);
		session->flags -= RUMBLE_IMAP4_HAS_SELECT; // clear select flag.
		imap->folder = 0;
		rcprintf(session, "%s OK Expunged and closed the mailbox.\r\n", tag);
	}
	else rcprintf(session, "%s NO No mailbox selected yet!\r\n", tag);
	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_expunge(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 
	imap4Session* imap = (imap4Session*) session->_svcHandle;
	if ( imap->account && (session->flags & RUMBLE_IMAP4_HAS_SELECT) ) {
		rumble_letters_expunge(imap->bag);
		rcprintf(session, "%s OK Expunged them letters.\r\n", tag);
	}
	else rcprintf(session, "%s NO No mailbox selected yet!\r\n", tag);
	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_search(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 

	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_fetch(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 
	rumble_letter* letter;
	rumble_args* parts;
	int a,b,c,w_uid,first,last;
	char line[1024];
	const char *body, *body_peek, *rfc822;
	int flags, uid, internaldate, envelope;
	int size, text, header; // rfc822.size/text/header
	uint32_t octets;
	imap4Session* imap = (imap4Session*) session->_svcHandle;
	uid = strstr(arg, "UID") ? 1 : 0;
	internaldate = strstr(arg, "INTERNALDATE") ? 1 : 0;
	envelope = strstr(arg, "ENVELOPE") ? 1 : 0;
	size = strstr(arg, "RFC822.SIZE") ? 1 : 0;
	text = strstr(arg, "RFC822.TEXT") ? 1 : 0;
	header = strstr(arg, "RFC822.HEADER") ? 1 : 0;
	flags = strstr(arg, "FLAGS") ? 1 : 0;
	octets = 0;
	memset(line, 0, 1024);
	body_peek = strstr(arg, "BODY.PEEK[");
	body = strstr(arg, "BODY[");
	parts = 0;
	first=0;last=0;
	if ( sscanf(arg, "%u:%c[*]", &first, &last) == 2 ) { last = -1; }
	else sscanf(arg, "%u:%u", &first, &last);
	if ( last == 0 ) last = first;
	if ( body ) sscanf(body, "BODY[%1000[^]]<%u>", line, &octets);
	else if ( body_peek ) sscanf(body_peek, "BODY.PEEK[%1000[^]]<%u>", line, &octets);

	w_uid = session->flags & RUMBLE_IMAP4_HAS_UID;

	if ( body || body_peek) { 
		if ( strlen(line) ) {
			char region[32], buffer[1024];
			memset(region, 0, 32);
			memset(buffer, 0, 1024);printf("%u %s\r\n", octets, line);
			if ( sscanf(line, "%32s (%1000c)", region, buffer) == 2 ) {
				parts = rumble_read_words(buffer);
				for (b = 0; b < parts->argc; b++) rumble_string_lower(parts->argv[b]);
			}
		}
	}

	for (a = 0; a < imap->bag->size; a++) {
		letter = imap->bag->letters[a];
		if ( letter->folder == imap->bag->folder.id ) {
			if ( w_uid && (letter->id < first || (last > 0 && letter->id > last) )) continue;
			if ( !w_uid && ((a+1) < first || (last > 0 &&  (a+1) > last) )) continue;
			rcprintf(session, "* %u FETCH (", a+1);
			if (flags) rcprintf(session, "FLAGS (%s%s%s%s) ", (letter->flags & RUMBLE_LETTER_RECENT) ? "\\Recent " : "", (letter->flags & RUMBLE_LETTER_READ) ? "\\Seen " : "", (letter->flags & RUMBLE_LETTER_DELETED) ? "\\Deleted " : "", (letter->flags & RUMBLE_LETTER_FLAGGED) ? "\\Flagged " : "");
			if (uid || w_uid) rcprintf(session, "UID %u ", letter->id);
			if (size) rcprintf(session, "RFC822.SIZE %u ", letter->size);
			if (internaldate) rcprintf(session, "INTERNALDATE %u ", letter->delivered);
			if ( body ) letter->flags -= (letter->flags & RUMBLE_LETTER_RECENT); // Remove \Recent flag since we're not peeking.
			if ( body || body_peek ) {
				char header[10240], key[64];
				FILE* fp = rumble_letters_open(imap->account, letter);
				if (fp) {
					if (parts) {
						memset(header, 0, 10240);
						while (fgets(line, 1024, fp)) {
							c = strlen(line);
							if (line[0] == '\r' || line[0] == '\n') break;
							memset(key, 0, 64);
							if (sscanf(line, "%63[^:]", key)) {
								rumble_string_lower(key);
								if ( parts ) {
									for (b = 0; b < parts->argc; b++) {
										if ( !strcmp(key, parts->argv[b]) ) {
											if ( line[c-2] != '\r' ) {line[c-1] = '\r'; line[c] = '\n'; line[c+1] = 0;}
											strncpy(header+strlen(header), line, strlen(line));
										}
									}
								}
								else {
									//if ( line[c-2] != '\r' ) {line[c-1] = '\r'; line[c] = '\n'; line[c+1] = 0;}
									strncpy(header+strlen(header), line, strlen(line));
								}
							}
						}
						sprintf(header+strlen(header), "\r\n \r\n");
						rcprintf(session, "BODY[HEADER.FIELDS (line)] {%u}\r\n", strlen(header));
						rcsend(session, header);
					}
					else {
						rcprintf(session, "BODY[] {%u}\r\n", letter->size);
						while (fgets(line, 1024, fp)) {
							rcsend(session, line);
							printf("%s", line);
						}
					}
					fclose(fp);
				}
				rcsend(session, " ");
			}
			rcprintf(session, ")\r\n");
		}
	}
	rcprintf(session, "%s OK FETCH completed\r\n", tag);
	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_store(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 
	int first, last,silent,control,a,useUID,flag;
	rumble_letter* letter;
	rumble_args* parts;
	char args[100];
	imap4Session* imap = (imap4Session*) session->_svcHandle;
	
	/* Get the message range */
	first=0;last=0;
	if ( sscanf(arg, "%u:%1[*]", &first, &last) == 2 ) { last = -1; }
	else sscanf(arg, "%u:%u", &first, &last);
	if ( last == 0 ) last = first;
	useUID = session->flags & RUMBLE_IMAP4_HAS_UID;

	/* Get the store type */
	silent = strstr(arg, ".SILENT") ? 1 : 0;
	control = strchr(arg, '+') ? 1 : (strchr(arg, '-') ? -1 : 0);
	memset(args, 0, 100);
	sscanf(arg, "%*100[^(](%99[^)])", args);

	/* Set the master flag */
	flag = 0;
	flag |= (strstr(arg, "\\Deleted") ?  RUMBLE_LETTER_DELETED : 0);
	flag |= (strstr(arg, "\\Seen") ?	    RUMBLE_LETTER_READ : 0);
	flag |= (strstr(arg, "\\Flagged") ?  RUMBLE_LETTER_FLAGGED : 0);
	flag |= (strstr(arg, "\\Draft") ?    RUMBLE_LETTER_DRAFT : 0);
	flag |= (strstr(arg, "\\Answered") ? RUMBLE_LETTER_ANSWERED : 0);
	
	/* Process the letters */
	for (a = 0; a < imap->bag->size; a++) {
		letter = imap->bag->letters[a];
		if ( letter->folder == imap->bag->folder.id ) {
			if ( useUID && (letter->id < first || (last > 0 && letter->id > last) )) continue;
			if ( !useUID && ((a+1) < first || (last > 0 &&  (a+1) > last) )) continue;

			/* +FLAGS ? */
			if (control == 1) letter->flags |= flag;

			/* -FLAGS ? */
			if (control == -1) letter->flags -= (flag & letter->flags);

			/* FLAGS ? */
			if (control == 0) letter->flags = flag | RUMBLE_LETTER_UNREAD;

			if ( !silent ) rcprintf(session, "* %u FETCH (FLAGS (%s%s%s%s))\r\n", (a+1), (letter->flags & RUMBLE_LETTER_RECENT) ? "\\Recent " : "", (letter->flags & RUMBLE_LETTER_READ) ? "\\Seen " : "", (letter->flags & RUMBLE_LETTER_DELETED) ? "\\Deleted " : "", (letter->flags & RUMBLE_LETTER_FLAGGED) ? "\\Flagged " : "");
		}
	}

	rcprintf(session, "%s OK STORE completed\r\n", tag);
	return RUMBLE_RETURN_IGNORE;
}

ssize_t rumble_server_imap_copy(masterHandle* master, sessionHandle* session, const char* tag, const char* arg) { 
	rcprintf(session, "%s OK COPY completed\r\n", tag);
	return RUMBLE_RETURN_IGNORE;
}
