CONTIKI_PROJECT = new-rl-tsch
all: $(CONTIKI_PROJECT)

PLATFORMS_ONLY = cooja

CONTIKI=../..

MAKE_MAC = MAKE_MAC_TSCH

include $(CONTIKI)/Makefile.dir-variables
MODULES += $(CONTIKI_NG_SERVICES_DIR)/shell

include $(CONTIKI)/Makefile.include