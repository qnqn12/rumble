#include "rumble.h"
#include "servers.h"
#include "comm.h"


// Main loop
void* rumble_smtp_init(void* m) {
    masterHandle* master = (masterHandle*) m;
    #ifdef RUMBLE_DEBUG
    printf("<smtp::threads> Initialized thread %#x\n", (uintptr_t) pthread_self());
    #endif
    
    // Initialize a session handle and wait for incoming connections.
    sessionHandle session;
    sessionHandle* sessptr = &session;
    session.recipients = cvector_init();
    session.client = (clientHandle*) malloc(sizeof(clientHandle));
    while (1) {
        comm_accept(master->smtp.socket, session.client);
        pthread_mutex_lock(&master->smtp.mutex);
        cvector_add(master->smtp.handles, (void*) sessptr);
        pthread_mutex_unlock(&master->smtp.mutex);
        session.flags = 0;
        session._tflags += 0x00100000; // job count ( 0 through 4095)
        printf("Added session %#x to cvector\n", (uintptr_t) sessptr);
        // Check for hooks on accept()
        ssize_t rc = EXIT_SUCCESS;
        rc = rumble_server_schedule_hooks(master, sessptr, RUMBLE_HOOK_ACCEPT + RUMBLE_HOOK_SMTP );
        rumble_comm_send(sessptr, rumble_smtp_reply_code(220)); // Hello!
        
        // Parse incoming commands
        char* cmd = malloc(5);
        char* arg = malloc(1024);
        while ( rc != -1 ) {
            memset(cmd, 0, 5);
            memset(arg, 0, 1024);
            char* line = rumble_comm_read(sessptr);
            if ( !line ) break;
            sscanf(line, "%4[^\t ]%*[ \t]%1000c", cmd, arg);
            free(line);
            rumble_string_upper(cmd);
            rc = 500; // default return code is "500 lolwut?"
            if (!strcmp(cmd, "QUIT")) break; // bye!
            else if (!strcmp(cmd, "MAIL")) rc = rumble_server_smtp_mail(master, &session, arg);
            else if (!strcmp(cmd, "RCPT")) rc = rumble_server_smtp_rcpt(master, &session, arg);
            else if (!strcmp(cmd, "HELO")) rc = rumble_server_smtp_helo(master, &session, arg);
            else if (!strcmp(cmd, "NOOP")) rc = rumble_server_smtp_noop(master, &session, arg);
            else if (!strcmp(cmd, "DATA")) rc = rumble_server_smtp_data(master, &session, arg);
            else if (!strcmp(cmd, "VRFY")) rc = rumble_server_smtp_vrfy(master, &session, arg);
            else if (!strcmp(cmd, "RSET")) rc = rumble_server_smtp_rset(master, &session, arg);
            if ( rc == RUMBLE_RETURN_IGNORE ) continue; // Skip to next line.
            else if ( rc == RUMBLE_RETURN_FAILURE ) break; // Abort!
            else rumble_comm_send(sessptr, rumble_smtp_reply_code(rc)); // Bad command thing.
        }
        // Cleanup
        rumble_comm_send(sessptr, rumble_smtp_reply_code(221)); // bye!
        shutdown(session.client->socket,0);
        close(session.client->socket);
        free(arg);
        free(cmd);
        rumble_clean_session(sessptr);
        pthread_mutex_lock(&(master->smtp.mutex));
        cvector_element* el = master->smtp.handles->first;
        int x = 0;
        printf("Removing session %#x from cvector\n", (uintptr_t) sessptr);
        printf("cvector has %u elements\n", (uint32_t) cvector_size(master->smtp.handles));
        while ( el != NULL ) {
            sessionHandle* s = (sessionHandle*) el->object;
            printf("Found %#x in cvector\n", (uintptr_t) s);
            if (s == sessptr) { cvector_delete_at(master->smtp.handles, el); x = 1; break; }
            el = el->next;
        }
        if ( !x ) printf("couldn't find session %#x in cvector :O\n", (uintptr_t) sessptr);
        // Check if we were told to go kill ourself :(
        if ( session._tflags & RUMBLE_THREAD_DIE ) {
            printf("<smtp::threads>I was told to die :(\n");
            cvector_element* el = master->smtp.threads->first;
            while ( el != NULL ) {
                pthread_t* t = (pthread_t*) el->object;
                if (*t == pthread_self()) { cvector_delete_at(master->smtp.threads, el); break; }
                el = el->next;
            }
            pthread_mutex_unlock(&master->smtp.mutex);
            pthread_exit(0);
        }
        pthread_mutex_unlock(&master->smtp.mutex);
    }
    pthread_exit(0);
    return 0;
}


// Command specific routines
ssize_t rumble_server_smtp_mail(masterHandle* master, sessionHandle* session, const char* argument) {
    char* raw = calloc(1,1000);
    char* user = calloc(1,64);
    char* domain = calloc(1,128); // RFC says 64, but that was before i18n
    sscanf(argument, "%*4c:%1000c", raw);
    // Try to fetch standard syntax: MAIL FROM: [whatever] <user@domain.tld>
    sscanf(raw, "%*256[^<]<%64[^>@]@%128[^@>]", user, domain);
    // Set the current values
    session->sender.raw = raw;
    session->sender.user = user;
    session->sender.domain = domain;
    // Fire events scheduled for pre-processing run
    ssize_t rc = rumble_server_schedule_hooks(master,session,RUMBLE_HOOK_SMTP + RUMBLE_HOOK_COMMAND + RUMBLE_HOOK_BEFORE + RUMBLE_CUE_SMTP_MAIL);
    if ( rc != RUMBLE_RETURN_OKAY ) { // Something went wrong, let's clean up and return.
        free(session->sender.domain);
        free(session->sender.user);
        free(session->sender.raw);
        session->flags = session->flags ^ RUMBLE_SMTP_HAS_MAIL;
        return rc;
    }
    // Validate address.
    
    // Fire post-processing hooks.
    rc = rumble_server_schedule_hooks(master,session,RUMBLE_HOOK_SMTP + RUMBLE_HOOK_COMMAND + RUMBLE_HOOK_BEFORE + RUMBLE_CUE_SMTP_MAIL);
    if ( rc != RUMBLE_RETURN_OKAY ) return rc;
    
    session->flags = session->flags | RUMBLE_SMTP_HAS_MAIL;
    return 250;
}

ssize_t rumble_server_smtp_rcpt(masterHandle* master, sessionHandle* session, const char* argument) {
    // First, check for the right sequence of commands.
    if ( !(session->flags & RUMBLE_SMTP_HAS_MAIL) ) return 503; 
    
    // Allocate stuff and start parsing
    char* raw = calloc(1,1000);
    char* user = calloc(1,64);
    char* domain = calloc(1,128); // RFC says 64, but that was before i18n
    sscanf(argument, "%*2c:%1000c", raw);
    // Try to fetch standard syntax: MAIL FROM: [whatever] <user@domain.tld>
    sscanf(raw, "%*256[^<]<%64[^>@]@%128[^@>]", user, domain);
    // Set the current values
    address* recipient = malloc(sizeof(address));
    recipient->domain = domain;
    recipient->user = user;
    recipient->raw = raw;
    cvector_add(session->recipients, recipient);
    
    ssize_t rc;
    // Fire events scheduled for pre-processing run
    rc = rumble_server_schedule_hooks(master,session,RUMBLE_HOOK_SMTP + RUMBLE_HOOK_COMMAND + RUMBLE_HOOK_BEFORE + RUMBLE_CUE_SMTP_RCPT);
    if ( rc != RUMBLE_RETURN_OKAY ) return rc;
    
    // Validate address...
    
    // Fire post-processing hooks.
    rc = rumble_server_schedule_hooks(master,session,RUMBLE_HOOK_SMTP + RUMBLE_HOOK_COMMAND + RUMBLE_HOOK_BEFORE + RUMBLE_CUE_SMTP_RCPT);
    if ( rc != RUMBLE_RETURN_OKAY ) return rc;
    
    // If everything went fine, set the RCPT flag and return with code 200.
    session->flags = session->flags | RUMBLE_SMTP_HAS_RCPT;
    return 250;
}

ssize_t rumble_server_smtp_helo(masterHandle* master, sessionHandle* session, const char* argument) {
    session->flags = session->flags | RUMBLE_SMTP_HAS_HELO;
    return 250;
}
ssize_t rumble_server_smtp_ehlo(masterHandle* master, sessionHandle* session, const char* argument) {
    session->flags = session->flags | RUMBLE_SMTP_HAS_EHLO;
    return 250;
}
ssize_t rumble_server_smtp_data(masterHandle* master, sessionHandle* session, const char* argument) {
    // First, check for the right sequence of commands.
    if ( !(session->flags & RUMBLE_SMTP_HAS_RCPT) ) return 503; 
    return RUMBLE_RETURN_IGNORE;
}
ssize_t rumble_server_smtp_rset(masterHandle* master, sessionHandle* session, const char* argument) {
    ssize_t rc;
    // Fire events scheduled for pre-processing run
    rc = rumble_server_schedule_hooks(master,session,RUMBLE_HOOK_SMTP + RUMBLE_HOOK_COMMAND + RUMBLE_HOOK_BEFORE + RUMBLE_CUE_SMTP_RSET);
    if ( rc != RUMBLE_RETURN_OKAY ) return rc;
    
    // Reset the session handle
    session->flags = 0;
    rumble_clean_session(session);
    
    // Fire post-processing hooks.
    rc = rumble_server_schedule_hooks(master,session,RUMBLE_HOOK_SMTP + RUMBLE_HOOK_COMMAND + RUMBLE_HOOK_BEFORE + RUMBLE_CUE_SMTP_RSET);
    if ( rc != RUMBLE_RETURN_OKAY ) return rc;
    return 250;
}
ssize_t rumble_server_smtp_vrfy(masterHandle* master, sessionHandle* session, const char* argument) {
    return 250;
}
ssize_t rumble_server_smtp_noop(masterHandle* master, sessionHandle* session, const char* argument) {
    ssize_t rc;
    // Fire events scheduled for pre-processing run
    rc = rumble_server_schedule_hooks(master,session,RUMBLE_HOOK_SMTP + RUMBLE_HOOK_COMMAND + RUMBLE_HOOK_BEFORE + RUMBLE_CUE_SMTP_RSET);
    if ( rc != RUMBLE_RETURN_OKAY ) return rc;
    // Do...nothing    
    // Fire post-processing hooks.
    rc = rumble_server_schedule_hooks(master,session,RUMBLE_HOOK_SMTP + RUMBLE_HOOK_COMMAND + RUMBLE_HOOK_BEFORE + RUMBLE_CUE_SMTP_RSET);
    if ( rc != RUMBLE_RETURN_OKAY ) return rc;
    return 250;
}