# name of your application
APPLICATION = radio_test

#USEMODULE_INCLUDES += $(RIOTBASE)/drivers/cc112x/include

# If no BOARD is found in the environment, use this default:
BOARD ?= native

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../..

BOARD_INSUFFICIENT_MEMORY := airfy-beacon chronos msb-430 msb-430h nrf51dongle \
                          nrf6310 nucleo-f334 pca10000 pca10005 spark-core \
                          stm32f0discovery telosb weio wsn430-v1_3b wsn430-v1_4 \
                          yunjia-nrf51822 z1

# Add also the shell, some shell commands
USEMODULE += gnrc
USEMODULE += gnrc_netif_default
USEMODULE += auto_init_gnrc_netif

DISABLE_MODULE += ieee802154

ifneq (,$(filter gnrc_netif_default,$(USEMODULE)))
    USEMODULE += cc110x gnrc_netdev2 gnrc_cc110x
endif

USEMODULE += debug_t
USEMODULE += xtimer
USEMODULE += shell
USEMODULE += shell_commands
USEMODULE += ps

USEMODULE += random

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
#CFLAGS += -DDEVELHELP

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

include $(RIOTBASE)/Makefile.include
