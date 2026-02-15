CLANG ?= clang
BPFTOOL ?= bpftool
ARCH := x86_64

BPF_SRC := vm.bpf.c
BPF_OBJ := vm.bpf.o
SKEL_H := vm.skel.h
LOADER_SRC := bpf_lsm_policy_loader.c
LOADER_BIN := bpf_lsm_policy_loader
SERVICE_FILE := bpf_lsm_policy_loader.service

DESTDIR ?=
BINDIR ?= /usr/local/sbin
UNITDIR ?= /etc/systemd/system

CFLAGS := -g -O2 -Wall
LIBS := -lbpf

CLANG_BPF_SYS_INCLUDES := $(shell $(CLANG) -v -E - </dev/null 2>&1 \
    | sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }')

BPF_CFLAGS := -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(CLANG_BPF_SYS_INCLUDES)

.PHONY: all clean install uninstall load

all: $(LOADER_BIN)

vmlinux.h:
	@if [ -f "/sys/kernel/btf/vmlinux" ]; then \
		echo "Generating vmlinux.h..."; \
		$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h; \
	else \
		echo "ERROR: /sys/kernel/btf/vmlinux not found."; \
		exit 1; \
	fi

$(BPF_OBJ): $(BPF_SRC) vmlinux.h
	$(CLANG) $(BPF_CFLAGS) -I. -c $(BPF_SRC) -o $(BPF_OBJ)

$(SKEL_H): $(BPF_OBJ)
	$(BPFTOOL) gen skeleton $(BPF_OBJ) > $(SKEL_H)

$(LOADER_BIN): $(LOADER_SRC) $(SKEL_H)
	$(CLANG) $(CFLAGS) -I. $(LOADER_SRC) -o $(LOADER_BIN) $(LIBS)

clean:
	rm -f $(BPF_OBJ) $(SKEL_H) $(LOADER_BIN) vmlinux.h

install: $(LOADER_BIN)
	@echo "Installing binary to $(DESTDIR)$(BINDIR)..."
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(LOADER_BIN) $(DESTDIR)$(BINDIR)/$(LOADER_BIN)

	@echo "Installing service to $(DESTDIR)$(UNITDIR)..."
	install -d $(DESTDIR)$(UNITDIR)
	install -m 644 $(SERVICE_FILE) $(DESTDIR)$(UNITDIR)/$(SERVICE_FILE)

load: install
	@echo "Reloading systemd and starting service..."
	systemctl daemon-reload
	systemctl enable $(SERVICE_FILE)
	systemctl restart $(SERVICE_FILE)
	systemctl status $(SERVICE_FILE) --no-pager

uninstall:
	systemctl stop $(SERVICE_FILE) || true
	systemctl disable $(SERVICE_FILE) || true
	rm -f $(DESTDIR)$(BINDIR)/$(LOADER_BIN)
	rm -f $(DESTDIR)$(UNITDIR)/$(SERVICE_FILE)
	systemctl daemon-reload
