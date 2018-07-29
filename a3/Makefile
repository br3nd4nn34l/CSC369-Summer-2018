all: ext2_ls ext2_rm

ext2_ls :  ext2_ls.o
	gcc -Wall -g -o ext2_ls $^

ext2_rm : ext2_rm.o
	gcc -Wall -g -o ext2_rm $^

%.o : %.c ext2.h ext2_helper.h list.h
	gcc -Wall -g -c $<

clean :
	rm -f *.o ext2_ls ext2_rm *~

