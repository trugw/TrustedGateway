#include "sw_fileio.h"
#include "sw_crt_mng.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h> // memcpy()

#include <tee_internal_api.h> // *MSG()

static char *user_app = NULL, *admin_app = NULL, *master_app = NULL, *pending_app = NULL;

char *get_web_app(enum client_type type) {
    switch (type) {
    case USER: return user_app;
    case ADMIN: return admin_app;
    case MASTER: return master_app;
    case PENDING: return pending_app;
    default:
        EMSG("Invalid user type\n");
        return NULL;
    }
}

int sw_init_webapp_file(const void *file_buffer, uint32_t file_len, uint32_t file_id,
    const void *sign_buffer, uint32_t sign_len)
{
    // TODO: what kind of signature?! -- RSA 2048 results in 256B which is big !
    if (file_buffer == NULL || file_len == 0
        || sign_buffer == NULL) // || sign_len != 256) return -1; 
        return -1;

    const char *type_str;
    char **storage;
    switch ((enum client_type)file_id)
    {
    case USER:
        storage = &user_app;
        type_str = "USER";
        DMSG("User web app will be loaded");
        break;
    
    case ADMIN:
        storage = &admin_app;
        type_str = "ADMIN";
        DMSG("Admin web app will be loaded");
        break;

    case MASTER:
        storage = &master_app;
        type_str = "MASTER";
        DMSG("Master web app will be loaded");
        break;

    case PENDING:
        storage = &pending_app;
        type_str = "PENDING";
        DMSG("Pending Admin web app will be loaded");
        break;

    default:
        EMSG("Invalid file type!\n");
        return -1;
    }

    if (*storage != NULL) {
        EMSG("Webapp has already been initialized before\n");
        return -1;
    }

    // TODO: hash(type_str|file_buffer), check signature
    if (1 != 1) return -1;
    (void)type_str;
    (void)sign_len;
    //

    // Store
    if ((*storage = malloc(file_len+1)) == NULL) {
        EMSG("Out of memory!\n");
        return -1;
    }
    memcpy(*storage, file_buffer, file_len);
    storage[file_len] = '\0';
    return 0;
}

void cleanup_webapps(void) {
    if (user_app != NULL)    { free(user_app);    user_app = NULL; }
    if (admin_app != NULL)   { free(admin_app);   admin_app = NULL; }
    if (master_app != NULL)  { free(master_app);  master_app = NULL; }
    if (pending_app != NULL) { free(pending_app); pending_app = NULL; }
}