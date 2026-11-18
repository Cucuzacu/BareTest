PORTS = miyoo

help:
	@echo "available targets:"
	@echo "  make miyoo          - build minetest for miyoocfw"
	@echo "  make clean          - clean object files"
	@echo "  make distclean      - clean everything including binaries and packages"

miyoo:
	$(MAKE) -f Makefile.miyoo miyoo

all: miyoo

clean:
	$(MAKE) -f Makefile.miyoo clean

execlean:
	$(MAKE) -f Makefile.miyoo execlean

distclean:
	$(MAKE) -f Makefile.miyoo distclean
