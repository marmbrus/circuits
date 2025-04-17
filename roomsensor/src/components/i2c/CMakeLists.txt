idf_component_register(
    SRCS
        "i2c_master_ext.c"
        "lis2dh.c"
        "i2c_sensor.cpp"
        "lis2dh_sensor.cpp"
        "i2c.cpp"
        "bme280_sensor.cpp"
    INCLUDE_DIRS "." "../common"
    PRIV_REQUIRES driver mqtt json
) 