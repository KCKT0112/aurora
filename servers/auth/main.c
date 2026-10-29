/* The only holder of session authority. Password verification stays in user space. */
#include <aurora/auth.h>
#include <aurora/user.h>
#include <string.h>
int umain(uint64_t argument)
{
    (void)argument;
    int64_t size = sys_boot_resource();
    const auth_database_t *db = (const void *)(uintptr_t)USER_RESOURCE_BASE;
    if (size < 0 || !aurora_auth_database_valid(db, (size_t)size)) {
        user_log("authd: invalid account database; login disabled");
        return 1;
    }
    user_log("authd: ready, salted password verifiers");
    uint64_t next_attempt = 0;
    unsigned failures = 0;
    for (;;) {
        ipc_message_t msg;
        while (sys_recv(&msg) == 0) {
            int64_t display = sys_service(IMAGE_DISPLAY);
            if (msg.type == MSG_AUTH_LOGOUT) {
                credentials_t peer, session;
                sys_session_get(&session);
                if (msg.from == (uint64_t)display ||
                    (msg.from == (uint64_t)sys_service(IMAGE_TERMINAL) &&
                     sys_peer_credentials((int)msg.from, &peer) == 0 &&
                     peer.session == session.session)) {
                    sys_session_set(AURORA_NO_USER);
                    user_log("authd: logged out");
                }
                continue;
            }
            if (msg.type != MSG_AUTH_LOGIN || msg.from != (uint64_t)display)
                continue;
            int64_t status = -AURORA_EPERM;
            uint32_t uid = AURORA_NO_USER;
            uint64_t now = (uint64_t)sys_get_ticks();
            if (now < next_attempt)
                status = -AURORA_EAGAIN;
            else {
                login_request_t request;
                memcpy(&request, msg.data, sizeof(request));
                bool valid = aurora_auth_verify(db, &request, &uid);
                aurora_erase(&request, sizeof(request));
                if (valid) {
                    status = sys_session_set(uid);
                    failures = 0;
                    next_attempt = 0;
                } else {
                    if (failures < 5)
                        failures++;
                    next_attempt = (uint64_t)sys_get_ticks() + (100u << failures);
                }
            }
            aurora_erase(msg.data, sizeof(msg.data));
            ipc_message_t reply = {
                .type = MSG_AUTH_RESULT, .arg0 = msg.arg0, .arg1 = (uint64_t)status};
            user_send(display, &reply);
            user_log(status == 0 ? "authd: login accepted" : "authd: login rejected");
        }
        sys_wait(100);
    }
}
