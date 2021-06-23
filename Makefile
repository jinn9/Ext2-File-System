all : cp mkdir ln rm restore checker

cp : ext2_cp.o ext2_helper.o
	gcc -Wall -g -o ext2_cp $^ -lm

mkdir : ext2_mkdir.o ext2_helper.o
	gcc -Wall -g -o ext2_mkdir $^ -lm

ln : ext2_ln.o ext2_helper.o
	gcc -Wall -g -o ext2_ln $^ -lm

rm : ext2_rm.o ext2_helper.o
	gcc -Wall -g -o ext2_rm $^ -lm

restore : ext2_restore.o ext2_helper.o
	gcc -Wall -g -o ext2_restore $^ -lm

checker : ext2_checker.o ext2_helper.o
	gcc -Wall -g -o ext2_checker $^ -lm

%.o : %.c ext2.h ext2_helper.h
	gcc -Wall -g -c $<

clean : 
	rm -f *.o ext2_cp ext2_mkdir ext2_ln ext2_rm ext2_restore ext2_checker	
