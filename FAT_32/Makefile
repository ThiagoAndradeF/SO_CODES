INCLUDE = include
SOURCE  = source
BUILD   = build

CC    = cc
CARGS = -Wall -Wextra -g -O0 -I$(INCLUDE) -pedantic -std=c11

OBJS    = $(shell find $(SOURCE) -type f -name '*.c' | sed 's/\.c*$$/\.o/; s/$(SOURCE)\//$(BUILD)\//')
HEADERS = $(shell find $(INCLUDE) -type f -name '*.h')

NAME = obese32

.PHONY: builddir format

all: $(NAME)

builddir:
	@if [ ! -d "build" ] ; then mkdir build ; fi

resetimg:
	@cp -v backup.img disk.img

$(OBJS): $(BUILD)/%.o: $(SOURCE)/%.c $(HEADERS)
	@$(CC) -c $(CARGS) $< -o $@
	@echo 'CC   ' $<

clean:
	@rm -vf $(NAME) $(OBJS)

$(NAME): builddir $(OBJS)
	@$(CC) $(CARGS) $(OBJS) -o $@
	@echo 'CCLD ' $(NAME)

# Comando para criar a imagem FAT32
format: resetimg
	@mkfs.vfat -F 32 -n "DISKNAME" disk.img
	@status=$$?; \
	if [ $$status -eq 0 ]; then \
		echo 'Formatado para FAT32 com sucesso'; \
	else \
		echo 'Erro ao formatar para FAT32'; \
		exit 1; \
	fi
