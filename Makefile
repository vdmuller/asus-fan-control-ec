CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
LDFLAGS ?=

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
SYSCONFDIR ?= /etc
BASHCOMPDIR ?= $(DATADIR)/bash-completion/completions
ZSHCOMPDIR  ?= $(DATADIR)/zsh/site-functions

BIN     := asus-fan-control-ec
SRCDIR  := src
OBJDIR  := build
SRC     := $(wildcard $(SRCDIR)/*.c)
OBJ     := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRC))
DEP     := $(OBJ:.o=.d)

.PHONY: all clean install uninstall

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(BIN)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	[ -f $(DESTDIR)$(SYSCONFDIR)/asus-fan-curve.conf ] || \
		install -Dm644 asus-fan-curve.conf \
			$(DESTDIR)$(SYSCONFDIR)/asus-fan-curve.conf
	install -Dm644 asus-fan-control-ec.service \
		$(DESTDIR)$(PREFIX)/lib/systemd/system/asus-fan-control-ec.service
	install -Dm644 completions/asus-fan-control-ec.bash \
		$(DESTDIR)$(BASHCOMPDIR)/$(BIN)
	install -Dm644 completions/_asus-fan-control-ec \
		$(DESTDIR)$(ZSHCOMPDIR)/_$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	rm -f $(DESTDIR)$(PREFIX)/lib/systemd/system/asus-fan-control-ec.service
	rm -f $(DESTDIR)$(BASHCOMPDIR)/$(BIN)
	rm -f $(DESTDIR)$(ZSHCOMPDIR)/_$(BIN)

-include $(DEP)
