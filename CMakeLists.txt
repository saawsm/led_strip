idf_component_register(
    SRCS 
        "led_strip.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        driver
    PRIV_REQUIRES
        log
)