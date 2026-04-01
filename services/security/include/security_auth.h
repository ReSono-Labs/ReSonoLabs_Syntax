#ifndef SECURITY_AUTH_H
#define SECURITY_AUTH_H

#include <stdbool.h>
#include <stddef.h>

bool security_auth_init(void);
bool security_auth_get_pin(char *buf, size_t buf_len);
bool security_auth_rotate_pin(char *buf, size_t buf_len);
bool security_auth_authorize_bearer(const char *bearer_value);
void security_auth_mark_activity(void);
bool security_auth_session_active(void);

#endif
