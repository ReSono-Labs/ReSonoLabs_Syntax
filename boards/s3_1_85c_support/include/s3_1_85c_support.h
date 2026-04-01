#ifndef S3_1_85C_SUPPORT_H
#define S3_1_85C_SUPPORT_H

#include "esp_err.h"
#include "driver/i2c_master.h"

esp_err_t s3_1_85c_support_init(void);
i2c_master_bus_handle_t s3_1_85c_support_i2c_bus(void);

#endif
