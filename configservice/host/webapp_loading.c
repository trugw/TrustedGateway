#include "webapp_loading.h"

#include <stdlib.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <bstgw/web_stub_ta.h>

static const char *webapp_user_path = "/opt/bstgw/assets/webapp-user.html";
static const char *webapp_admin_path = "/opt/bstgw/assets/webapp-admin.html";
static const char *webapp_master_path = "/opt/bstgw/assets/webapp-master.html";
static const char *webapp_pending_path = "/opt/bstgw/assets/webapp-pending.html";

static int bootstrap(TEEC_Session *sess, const char *app, enum client_type type);

int bootstrap_webapps(TEEC_Session *sess) {
    if (sess == NULL) return -1;
    // TODO: type
    if (bootstrap(sess, webapp_user_path, USER) != 0)    return -1;
    if (bootstrap(sess, webapp_admin_path, ADMIN) != 0)   return -1;
    if (bootstrap(sess, webapp_master_path, MASTER) != 0)  return -1;
    if (bootstrap(sess, webapp_pending_path, PENDING) != 0) return -1;
    return 0;
}

static int bootstrap(TEEC_Session *sess, const char *app, enum client_type type) {
    if (sess == NULL || app == NULL) return -1;

    /* Fetch file from disk */
    int fd = open(app, O_RDONLY);
    if (fd < 0) return -1;

    struct stat info;
    if (fstat(fd, &info) != 0) goto error;
    if (info.st_size <= 0) goto error;

    char *buf = malloc(info.st_size);
    if (buf == NULL) goto error;

    int ret;
    char *b = buf;
    size_t rest = info.st_size;

    while (rest > 0) {
        while( (ret = read(fd, b, rest)) < 0 && errno == EINTR  );
        if (ret <= 0) { free(buf); goto error; }
        rest -= ret;
        b += ret;
    }
    close(fd);

    /* Push to SW */
    uint32_t err_origin;
    TEEC_Operation op;

    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_VALUE_INPUT,
        TEEC_MEMREF_TEMP_INPUT, TEEC_NONE);
    
    op.params[1].value.a = (uint8_t)type;

    op.params[0].tmpref.buffer = buf;
    op.params[0].tmpref.size = info.st_size;
    
    // TODO: signature
    char *sign_dummy = "0xdeadbeef";
    op.params[2].tmpref.buffer = sign_dummy;
    op.params[2].tmpref.size = strlen(sign_dummy);
    
    TEEC_Result sw_res = TEEC_InvokeCommand(sess, BSTGW_TA_WEB_STUB_CMD_LOAD_WEBAPP_FILE, &op, &err_origin);
    free(buf);

    if (sw_res != TEEC_SUCCESS) {
        printf("Failed to push webapp: 0x%x (errOrigin: %u)\n", sw_res, err_origin);
        return -1;
    }

    return 0;

error:
    close(fd);
    return -1;
}
