idf_component_register(
    SRCS
        "i2c_master_ext.c"
        "lis2dh.c"
        "i2c_sensor.cpp"
        "lis2dh_sensor.cpp"
        "i2c.cpp"
        "bme280_sensor.cpp"
        "sen55_sensor.cpp"
        "scd4x_sensor.cpp"
        "ads1115_sensor.cpp"
        "opt3001_sensor.cpp"
        "mcp23008_sensor.cpp"
        "tas5825m_sensor.cpp"
        "i2c_telemetry.cpp"
        "metrics_tags.cpp"
    INCLUDE_DIRS "." "../common"
    PRIV_REQUIRES driver mqtt json configuration
)