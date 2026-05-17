.PHONY: all env-setup flash lint dist-clean

all:
	idf.py build

env-setup:
	echo Run these:
	echo source ~/.espressif/v6.0/esp-idf/export.sh
	echo idf.py set-target esp32c3

flash:
	idf.py build flash monitor # Exit w C-]

lint:
	find ./main/ -type f \( -name '*.c' -o -name '*.h' \) -print0 | \
		xargs -0 clang-format -i --style="{BasedOnStyle: LLVM, ColumnLimit: 120}"

dist-clean:
	rm -rf build/ managed_components/ sdkconfig sdkconfig.old
