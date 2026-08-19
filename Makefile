PROJECT_DIR := mini_rtsp_server_project

.PHONY: all clean

all:
	$(MAKE) -C $(PROJECT_DIR)

clean:
	$(MAKE) -C $(PROJECT_DIR) clean
