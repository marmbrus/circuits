idf_component_register(
    SRCS
        "sensors.cpp"
        "i2c_master_ext.c"
        "bq27441.cpp"
        "lis2dh.c"
    INCLUDE_DIRS "." "../common"
    PRIV_REQUIRES driver mqtt json
) 