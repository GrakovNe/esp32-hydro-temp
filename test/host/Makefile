CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined
APPCONF := ../../components/appconf
CJSON   := $(HOME)/esp/esp-idf/components/json/cJSON
MAIN    := ../../main
TESTS   := test_appconf test_history_ring test_fresh test_config_merge

.PHONY: test clean

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

test_appconf: test_appconf.c tinytest.h $(APPCONF)/appconf.c $(APPCONF)/include/appconf.h
	$(CC) $(CFLAGS) -I$(APPCONF)/include -o $@ test_appconf.c $(APPCONF)/appconf.c

test_history_ring: test_history_ring.c tinytest.h $(MAIN)/history_ring.c $(MAIN)/history_ring.h $(APPCONF)/include/appconf.h
	$(CC) $(CFLAGS) -I$(MAIN) -I$(APPCONF)/include -o $@ test_history_ring.c $(MAIN)/history_ring.c

test_fresh: test_fresh.c tinytest.h $(MAIN)/fresh.h $(MAIN)/sensors.h
	$(CC) $(CFLAGS) -I$(MAIN) -I$(APPCONF)/include -o $@ test_fresh.c

# cJSON comes from the ESP-IDF checkout; the merge logic has no other IDF dependency.
test_config_merge: test_config_merge.c tinytest.h $(MAIN)/config_merge.c $(MAIN)/config_merge.h $(APPCONF)/appconf.c
	$(CC) $(CFLAGS) -Wno-deprecated-declarations -I$(MAIN) -I$(APPCONF)/include -I$(CJSON) -o $@ \
	    test_config_merge.c $(MAIN)/config_merge.c $(APPCONF)/appconf.c $(CJSON)/cJSON.c -lm

clean:
	rm -f $(TESTS)
