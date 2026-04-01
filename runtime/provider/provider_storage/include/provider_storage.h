#ifndef PROVIDER_STORAGE_H
#define PROVIDER_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROVIDER_TOKEN_MAX_LEN 256
#define PROVIDER_PAIR_CODE_MAX_LEN 64
#define PROVIDER_SEED_LEN 32

bool provider_storage_load_gateway_token(char *buf, size_t buf_len);
bool provider_storage_save_gateway_token(const char *token);
bool provider_storage_clear_gateway_token(void);

bool provider_storage_load_endpoint_host(char *buf, size_t buf_len);
bool provider_storage_save_endpoint_host(const char *host);
bool provider_storage_clear_endpoint_host(void);
bool provider_storage_load_endpoint_port(uint16_t *port);
bool provider_storage_save_endpoint_port(uint16_t port);
bool provider_storage_clear_endpoint_port(void);

bool provider_storage_load_device_token(char *buf, size_t buf_len);
bool provider_storage_save_device_token(const char *token);
bool provider_storage_clear_device_token(void);

bool provider_storage_load_device_seed(uint8_t *buf, size_t *buf_len);
bool provider_storage_save_device_seed(const uint8_t *seed, size_t seed_len);
bool provider_storage_clear_device_seed(void);

#endif
