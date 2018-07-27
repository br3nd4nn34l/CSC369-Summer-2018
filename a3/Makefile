sim :  ext2_ls.o
	gcc -Wall -g -o ext2_ls $^

%.o : %.c ext2.h ext2_helper.h list.h
	gcc -Wall -g -c $<

clean :
	rm -f *.o ext2_ls *~
