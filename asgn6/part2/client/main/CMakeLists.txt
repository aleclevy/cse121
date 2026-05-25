idf_component_register(SRCS "http_request_example_main.c"
                    INCLUDE_DIRS "."
	    	    PRIV_REQUIRES esp_driver_i2c esp_wifi nvs_flash)
