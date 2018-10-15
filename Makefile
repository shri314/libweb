PROJECT        = libweb
CONTAINER_NAME = clang-libcpp-boost-env
DOCKER_ENV_CMD = docker exec -it $(PROJECT)_$(CONTAINER_NAME)_1
DOCKER_CXX     = $(DOCKER_ENV_CMD) clang++ -std=c++2a -fcoroutines-ts -stdlib=libc++
DOCKER_LINK    = $(DOCKER_ENV_CMD) clang++ -std=c++2a -fcoroutines-ts -stdlib=libc++ -lc++abi -lboost_system -lssl -lcrypto -pthread

.PHONY: up down clean one two all

all: two

up:
	docker-compose up -d

down:
	docker-compose down

#####################################################################

sample_one.o: sample_one.cpp
	$(MAKE) -s up
	$(DOCKER_CXX) -o $@ -c sample_one.cpp

sample_one: sample_one.o
	$(MAKE) -s up
	$(DOCKER_LINK) -o $@ $<

one: sample_one
	$(MAKE) -s up
	$(DOCKER_ENV_CMD) ./sample_one '0.0.0.0' 8080 1

#####################################################################

sample_two.o: sample_two.cpp
	$(MAKE) -s up
	$(DOCKER_CXX) -o $@ -c sample_two.cpp

sample_two: sample_two.o
	$(MAKE) -s up
	$(DOCKER_LINK) -o $@ $<

two: sample_two
	$(MAKE) -s up
	$(DOCKER_ENV_CMD) ./sample_two '0.0.0.0' 8443 1

#####################################################################

clean:
	rm -f sample_one sample_one.o
	rm -f sample_two sample_two.o
