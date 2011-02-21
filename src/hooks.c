/*$6
 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 */

#include "rumble.h"
#include "servers.h"
#include <string.h>

/*
 =======================================================================================================================
 =======================================================================================================================
 */
rumblemodule rumble_module_check(void) {
    return (RUMBLE_VERSION);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void rumble_hook_function(void *handle, uint32_t flags, ssize_t (*func) (sessionHandle *)) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    hookHandle  *hook = (hookHandle *) malloc(sizeof(hookHandle));
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    if (!hook) merror();
    rumble_module_check();
    hook->func = func;
    hook->flags = flags;
    hook->module = ((masterHandle *) handle)->_core.currentSO;
    hook->modinfo = (rumble_module_info *) ((masterHandle *) handle)->_core.modules->last;
#if (RUMBLE_DEBUG & RUMBLE_DEBUG_HOOKS)
    printf("<debug :: hooks> Adding hook of type %#x from %s\n", hook->flags, hook->module);
#endif
    switch (flags & RUMBLE_HOOK_STATE_MASK)
    {
    case RUMBLE_HOOK_ACCEPT:
        switch (flags & RUMBLE_HOOK_SVC_MASK)
        {
        case RUMBLE_HOOK_SMTP:  cvector_add(((masterHandle *) handle)->smtp.init_hooks, hook); break;
        case RUMBLE_HOOK_POP3:  cvector_add(((masterHandle *) handle)->pop3.init_hooks, hook); break;
        case RUMBLE_HOOK_IMAP:  cvector_add(((masterHandle *) handle)->imap.init_hooks, hook); break;
        default:                break;
        }
        break;

    case RUMBLE_HOOK_COMMAND:
        switch (flags & RUMBLE_HOOK_SVC_MASK)
        {
        case RUMBLE_HOOK_SMTP:  cvector_add(((masterHandle *) handle)->smtp.cue_hooks, hook); break;
        case RUMBLE_HOOK_POP3:  cvector_add(((masterHandle *) handle)->pop3.cue_hooks, hook); break;
        case RUMBLE_HOOK_IMAP:  cvector_add(((masterHandle *) handle)->imap.cue_hooks, hook); break;
        default:                break;
        }
        break;

    case RUMBLE_HOOK_EXIT:
        switch (flags & RUMBLE_HOOK_SVC_MASK)
        {
        case RUMBLE_HOOK_SMTP:  cvector_add(((masterHandle *) handle)->smtp.exit_hooks, hook); break;
        case RUMBLE_HOOK_POP3:  cvector_add(((masterHandle *) handle)->pop3.exit_hooks, hook); break;
        case RUMBLE_HOOK_IMAP:  cvector_add(((masterHandle *) handle)->imap.exit_hooks, hook); break;
        default:                break;
        }
        break;

    case RUMBLE_HOOK_FEED:
        cvector_add(((masterHandle *) handle)->_core.feed_hooks, hook);
        break;

    case RUMBLE_HOOK_PARSER:
        cvector_add(((masterHandle *) handle)->_core.parser_hooks, hook);

    default:
        break;
    }
}

typedef ssize_t (*hookFunc) (sessionHandle *);

/*
 =======================================================================================================================
 =======================================================================================================================
 */
ssize_t rumble_server_execute_hooks(sessionHandle *session, cvector *hooks, uint32_t flags) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    int             g = 0;
    ssize_t         rc = RUMBLE_RETURN_OKAY;
    dvector_element *el;
    hookFunc        mFunc = NULL;
    hookHandle      *hook;
    c_iterator      iter;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

#if RUMBLE_DEBUG & RUMBLE_DEBUG_HOOKS
    if (dvector_size(hooks)) printf("<debug :: hooks> Running hooks of type %#x\n", flags);
#endif
    cforeach((hookHandle *), hook, hooks, iter) {
        if (!hook) continue;
        if (hook->flags == flags) {
            if (hook->flags & RUMBLE_HOOK_FEED) {

                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
                /* ignore wrong feeds */
                mqueue  *item = (mqueue *) session;
                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

                if (!item->account->arg || strcmp(hook->module, item->account->arg)) {
                    continue;
                }
            }

            mFunc = hook->func;
            g++;
#if RUMBLE_DEBUG & RUMBLE_DEBUG_HOOKS
            printf("<debug :: hooks> Executing hook %p from %s\n", (void *) hookFunc, hook->module);
#endif
            rc = (mFunc) (session);
            if (rc == RUMBLE_RETURN_FAILURE)
            {
#if RUMBLE_DEBUG & RUMBLE_DEBUG_HOOKS
                printf("<debug :: hooks> Hook %p claimed failure, aborting connection!\n", (void *) hookFunc);
#endif
                return (RUMBLE_RETURN_FAILURE);
            }

            if (rc == RUMBLE_RETURN_IGNORE)
            {
#if RUMBLE_DEBUG & RUMBLE_DEBUG_HOOKS
                printf("<debug :: hooks> Hook %p took over, skipping to next command.\n", (void *) hookFunc);
#endif
                return (RUMBLE_RETURN_IGNORE);
            }
        }
    }

    return (rc);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
ssize_t rumble_server_schedule_hooks(masterHandle *handle, sessionHandle *session, uint32_t flags) {
    switch (flags & RUMBLE_HOOK_STATE_MASK)
    {
    case RUMBLE_HOOK_ACCEPT:
        switch (flags & RUMBLE_HOOK_SVC_MASK)
        {
        case RUMBLE_HOOK_SMTP:  return (rumble_server_execute_hooks(session, handle->smtp.init_hooks, flags));
        case RUMBLE_HOOK_POP3:  return (rumble_server_execute_hooks(session, handle->pop3.init_hooks, flags));
        case RUMBLE_HOOK_IMAP:  return (rumble_server_execute_hooks(session, handle->imap.init_hooks, flags));
        default:                break;
        }
        break;

    case RUMBLE_HOOK_COMMAND:
        switch (flags & RUMBLE_HOOK_SVC_MASK)
        {
        case RUMBLE_HOOK_SMTP:  return (rumble_server_execute_hooks(session, handle->smtp.cue_hooks, flags));
        case RUMBLE_HOOK_POP3:  return (rumble_server_execute_hooks(session, handle->pop3.cue_hooks, flags));
        case RUMBLE_HOOK_IMAP:  return (rumble_server_execute_hooks(session, handle->imap.cue_hooks, flags));
        default:                break;
        }
        break;

    case RUMBLE_HOOK_EXIT:
        switch (flags & RUMBLE_HOOK_SVC_MASK)
        {
        case RUMBLE_HOOK_SMTP:  return (rumble_server_execute_hooks(session, handle->smtp.exit_hooks, flags));
        case RUMBLE_HOOK_POP3:  return (rumble_server_execute_hooks(session, handle->pop3.exit_hooks, flags));
        case RUMBLE_HOOK_IMAP:  return (rumble_server_execute_hooks(session, handle->imap.exit_hooks, flags));
        default:                break;
        }
        break;

    case RUMBLE_HOOK_FEED:
        return (rumble_server_execute_hooks(session, handle->_core.feed_hooks, flags));
        break;

    case RUMBLE_HOOK_PARSER:
        return (rumble_server_execute_hooks(session, handle->_core.parser_hooks, flags));
        break;

    default:
        break;
    }

    return (RUMBLE_RETURN_OKAY);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void rumble_imap_add_command(masterHandle *handle, const char *command, imapCommand func) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    imapCommandHook *hook = (imapCommandHook *) malloc(sizeof(imapCommandHook));
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    hook->cmd = command;
    hook->func = func;
    cvector_add(handle->imap.commands, hook);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void rumble_pop3_add_command(masterHandle *handle, const char *command, pop3Command func) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    pop3CommandHook *hook = (pop3CommandHook *) malloc(sizeof(pop3CommandHook));
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    hook->cmd = command;
    hook->func = func;
    cvector_add(handle->pop3.commands, hook);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void rumble_smtp_add_command(masterHandle *handle, const char *command, smtpCommand func) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    smtpCommandHook *hook = (smtpCommandHook *) malloc(sizeof(smtpCommandHook));
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    hook->cmd = command;
    hook->func = func;
    cvector_add(handle->smtp.commands, hook);
}
