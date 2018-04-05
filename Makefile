all: ext2_cp ext2_mkdir ext2_ln ext2_rm ext2_checker ext2_restore ext2_rm_bonus

ext2_cp: ext2_cp.o ext2_utils.o
	gcc -Wall -o $@ $^ -lm

ext2_mkdir: ext2_mkdir.o ext2_utils.o
	gcc -Wall -o $@ $^ -lm

ext2_ln: ext2_ln.o ext2_utils.o
	gcc -Wall -o $@ $^ -lm

ext2_rm: ext2_rm.o ext2_utils.o
	gcc -Wall -o $@ $^ -lm

ext2_rm_bonus: ext2_rm.o ext2_utils.o
	gcc -Wall -o $@ $^ -lm

ext2_restore: ext2_restore.o ext2_utils.o
	gcc -Wall -o $@ $^ -lm

ext2_checker: ext2_checker.o ext2_utils.o
	gcc -Wall -o $@ $^ -lm

%.o: %.c ext2.h
	gcc -Wall -c $< -lm

clean:
	rm -rf *.o
	rm -rf ext2_cp ext2_mkdir ext2_ln ext2_rm ext2_checker ext2_restore ext2_rm_bonus