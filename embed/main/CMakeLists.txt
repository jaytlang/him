idf_component_register(SRCS	"button.c"
				"fs.c"
				"httpd.c"
				"main.c"
				"mdns.c"
				"led.c"
				"rmt.c"
				"sched.c"
				"wifi.c"
                    INCLUDE_DIRS "")

spiffs_create_partition_image(storage ../image FLASH_IN_PROJECT)
