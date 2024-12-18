

duplines_debug: duplines.c
	rm -f duplines_debug
	gcc -Wall -pedantic -B -g -Os -lc -o duplines_debug duplines.c

duplines_release: duplines.c
	rm -f duplines
	gcc -Os -B -lc -s  -o duplines duplines.c

duplines_info:
	nm --size-sort duplines_debug | tail -10
	wc -c duplines_debug

