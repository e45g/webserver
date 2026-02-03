CC = gcc
CFLAGS = -Wall -Wextra -Isrc -O2
LDFLAGS = -lpq -I/usr/include/postgresql

SRC_DIR = src
SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(LIB_DIR)/*.c) $(wildcard $(SRC_DIR)/*/*.c)

SERVER = server
CXC = cxc

$(SERVER): $(SRCS)
	@make clean
	@make cxc
	@./$(CXC) && $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	@rm -f $(SERVER)
	@rm -f $(CXC)

cxc:
	@$(CC) $(CFLAGS) -o $(CXC) src_cxc/main.c $(LDFLAGS)

dev-serve:
	@docker exec -it simple-http-server sh -c "cd /home/dev && make && ./server"

dev-startup:
	@docker run -v "./:/home/dev/" -p 3000:3000 -d -t --name simple-http-server gcc:latest
