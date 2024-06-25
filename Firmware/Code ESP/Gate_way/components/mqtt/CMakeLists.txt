idf_component_register(SRCS "mqtt.c"
               REQUIRES driver log json datamanager
                    INCLUDE_DIRS "include")
